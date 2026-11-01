#include "recoverydesk/windows.hpp"
#include <windows.h>
#include <winioctl.h>
#include <wbemidl.h>
#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <sstream>

namespace rd {
namespace {
struct Handle {
    HANDLE value{INVALID_HANDLE_VALUE};
    explicit Handle(HANDLE v) : value(v) {}
    ~Handle() { if (value != INVALID_HANDLE_VALUE && value != nullptr) CloseHandle(value); }
    explicit operator bool() const { return value != INVALID_HANDLE_VALUE && value != nullptr; }
};
template<class T> struct Com {
    T* p{}; ~Com() { if (p) p->Release(); }
    Com() = default; Com(const Com&) = delete; Com& operator=(const Com&) = delete;
    T** put() { if (p) { p->Release(); p = nullptr; } return &p; }
    T* operator->() const { return p; }
};
struct Bstr {
    BSTR p{};
    explicit Bstr(const wchar_t* v) : p(SysAllocString(v)) {}
    ~Bstr() { if (p) { SecureZeroMemory(p, SysStringLen(p) * sizeof(wchar_t)); SysFreeString(p); } }
};
struct Apartment {
    HRESULT status{CoInitializeEx(nullptr, COINIT_MULTITHREADED)};
    ~Apartment() { if (SUCCEEDED(status)) CoUninitialize(); }
};
std::wstring property_string(IWbemClassObject* object, const wchar_t* name) {
    if (!object) return {};
    VARIANT v; VariantInit(&v); std::wstring result;
    if (SUCCEEDED(object->Get(name, 0, &v, nullptr, nullptr)) && v.vt == VT_BSTR && v.bstrVal) result = v.bstrVal;
    VariantClear(&v); return result;
}
std::optional<unsigned> property_number(IWbemClassObject* object, const wchar_t* name) {
    if (!object) return {};
    VARIANT v; VariantInit(&v); std::optional<unsigned> result;
    if (SUCCEEDED(object->Get(name, 0, &v, nullptr, nullptr)) && (v.vt == VT_I4 || v.vt == VT_UI4)) result = v.ulVal;
    VariantClear(&v); return result;
}
struct EncryptionProvider {
    Apartment apartment;
    Com<IWbemLocator> locator;
    Com<IWbemServices> service;
    HRESULT status{E_FAIL};
    EncryptionProvider() {
        if (FAILED(apartment.status) && apartment.status != RPC_E_CHANGED_MODE) { status = apartment.status; return; }
        status = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_IWbemLocator, reinterpret_cast<void**>(locator.put()));
        if (FAILED(status)) return;
        Bstr space(L"ROOT\\CIMV2\\Security\\MicrosoftVolumeEncryption");
        status = locator->ConnectServer(space.p, nullptr, nullptr, nullptr, 0, nullptr, nullptr, service.put());
        if (FAILED(status)) return;
        status = CoSetProxyBlanket(service.p, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
                                  RPC_C_AUTHN_LEVEL_PKT_PRIVACY, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
    }
    bool query(Com<IEnumWbemClassObject>& output) {
        if (FAILED(status)) return false;
        Bstr lang(L"WQL"), query_text(L"SELECT * FROM Win32_EncryptableVolume");
        status = service->ExecQuery(lang.p, query_text.p, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, output.put());
        return SUCCEEDED(status);
    }
    std::optional<unsigned> method(const std::wstring& path, const wchar_t* method_name, const wchar_t* property) {
        Bstr object(path.c_str()), name(method_name); Com<IWbemClassObject> output;
        if (FAILED(service->ExecMethod(object.p, name.p, 0, nullptr, nullptr, output.put(), nullptr))) return {};
        auto rc = property_number(output.p, L"ReturnValue");
        if (!rc || *rc != 0) return {};
        return property_number(output.p, property);
    }
};
std::wstring trim_slash(std::wstring s) { if (!s.empty() && s.back() == L'\\') s.pop_back(); return s; }
std::string normalized_id(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return c >= 'A' && c <= 'Z' ? char(c + ('a' - 'A')) : char(c); });
    return value;
}
std::vector<unsigned> volume_disks(const std::wstring& guid) {
    Handle handle(CreateFileW(trim_slash(guid).c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
    if (!handle) return {};
    alignas(VOLUME_DISK_EXTENTS) std::array<std::byte, 4096> buffer{}; DWORD read{};
    if (!DeviceIoControl(handle.value, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, nullptr, 0, buffer.data(), DWORD(buffer.size()), &read, nullptr)) return {};
    const auto* extents = reinterpret_cast<VOLUME_DISK_EXTENTS*>(buffer.data());
    if (extents->NumberOfDiskExtents > 32) return {};
    std::set<unsigned> disks;
    for (DWORD i = 0; i < extents->NumberOfDiskExtents; ++i) disks.insert(extents->Extents[i].DiskNumber);
    return {disks.begin(), disks.end()};
}
std::string descriptor_string(const std::array<char, 65536>& buffer, DWORD offset, DWORD valid_bytes) {
    if (offset == 0 || offset >= valid_bytes || offset >= buffer.size()) return {};
    const auto available = std::min<std::size_t>(valid_bytes - offset, buffer.size() - offset);
    const auto* begin = buffer.data() + offset;
    auto end = std::find(begin, begin + available, '\0');
    std::string s(begin, end);
    while (!s.empty() && s.back() == ' ') s.pop_back();
    while (!s.empty() && s.front() == ' ') s.erase(s.begin());
    return s;
}
}
std::wstring wide(const std::string& value) {
    if (value.empty()) return {};
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), int(value.size()), nullptr, 0);
    if (!length) return L"[texto no válido]";
    std::wstring result(length, 0); MultiByteToWideChar(CP_UTF8, 0, value.data(), int(value.size()), result.data(), length); return result;
}
std::string utf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int length = WideCharToMultiByte(CP_UTF8, 0, value.data(), int(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(length, 0); WideCharToMultiByte(CP_UTF8, 0, value.data(), int(value.size()), result.data(), length, nullptr, nullptr); return result;
}
bool is_winpe() {
    HKEY key{};
    const auto result = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\MiniNT", 0, KEY_READ, &key);
    if (result == ERROR_SUCCESS) RegCloseKey(key);
    return result == ERROR_SUCCESS;
}
bool is_elevated() {
    HANDLE token{}; if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    Handle handle(token); TOKEN_ELEVATION elevation{}; DWORD bytes{};
    return GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &bytes) && elevation.TokenIsElevated;
}
fs::path executable_directory() {
    std::wstring path(32768, 0); const DWORD len = GetModuleFileNameW(nullptr, path.data(), DWORD(path.size()));
    if (!len || len >= path.size()) throw Failure("exe_path", "No se pudo determinar la carpeta de la aplicación.");
    path.resize(len); return fs::path(path).parent_path();
}
StorageIdentity storage_identity(const fs::path& path) {
    StorageIdentity s;
    std::array<wchar_t, 32768> root{};
    if (!GetVolumePathNameW(fs::absolute(path).c_str(), root.data(), DWORD(root.size()))) return s;
    const auto drive_type = GetDriveTypeW(root.data());
    s.local = drive_type == DRIVE_FIXED || drive_type == DRIVE_REMOVABLE;
    if (!s.local) return s;
    std::array<wchar_t, 1024> guid{}, filesystem{};
    if (!GetVolumeNameForVolumeMountPointW(root.data(), guid.data(), DWORD(guid.size()))) return s;
    s.volume_id = utf8(guid.data()); s.disks = volume_disks(guid.data());
    if (!GetVolumeInformationW(root.data(), nullptr, 0, nullptr, nullptr, nullptr, filesystem.data(), DWORD(filesystem.size()))) return s;
    s.filesystem = utf8(filesystem.data());
    ULARGE_INTEGER available{}, total{}, free{};
    if (!GetDiskFreeSpaceExW(root.data(), &available, &total, &free)) return s;
    s.free_bytes = available.QuadPart;
    s.max_file_bytes = s.filesystem == "FAT32" ? 0xffffffffULL : UINT64_MAX;
    s.verified = s.disks.size() == 1 && (s.filesystem == "NTFS" || s.filesystem == "exFAT" || s.filesystem == "FAT32");
    if (s.verified) s.verified = inspect_disk(s.disks.front()).identity_verified;
    return s;
}
Disk inspect_disk(unsigned number) {
    Disk d; d.number = number;
    const auto path = L"\\\\.\\PhysicalDrive" + std::to_wstring(number);
    Handle h(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
    if (!h) { d.model = "Requiere permiso o dispositivo no disponible"; return d; }
    DWORD returned{}; GET_LENGTH_INFORMATION length{};
    if (DeviceIoControl(h.value, IOCTL_DISK_GET_LENGTH_INFO, nullptr, 0, &length, sizeof(length), &returned, nullptr)) d.bytes = length.Length.QuadPart;
    STORAGE_PROPERTY_QUERY query{}; query.PropertyId = StorageDeviceProperty; query.QueryType = PropertyStandardQuery;
    std::array<char, 65536> buffer{};
    if (DeviceIoControl(h.value, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), buffer.data(), DWORD(buffer.size()), &returned, nullptr) && returned >= sizeof(STORAGE_DEVICE_DESCRIPTOR)) {
        const auto* descriptor = reinterpret_cast<STORAGE_DEVICE_DESCRIPTOR*>(buffer.data());
        d.model = descriptor_string(buffer, descriptor->VendorIdOffset, returned) + " " + descriptor_string(buffer, descriptor->ProductIdOffset, returned);
        d.serial = descriptor_string(buffer, descriptor->SerialNumberOffset, returned);
        d.usb = descriptor->BusType == BusTypeUsb;
        d.bus = d.usb ? "USB" : descriptor->BusType == BusTypeNvme ? "NVMe" : descriptor->BusType == BusTypeSata ? "SATA" : "Otro / desconocido";
        if (descriptor->BusType == BusTypeRAID || descriptor->BusType == BusTypeSpaces) return d;
    }
    alignas(DRIVE_LAYOUT_INFORMATION_EX) std::array<std::byte, 65536> layout_buffer{};
    if (DeviceIoControl(h.value, IOCTL_DISK_GET_DRIVE_LAYOUT_EX, nullptr, 0, layout_buffer.data(), DWORD(layout_buffer.size()), &returned, nullptr)) {
        const auto* layout = reinterpret_cast<DRIVE_LAYOUT_INFORMATION_EX*>(layout_buffer.data());
        d.partitioned_gpt = layout->PartitionStyle == PARTITION_STYLE_GPT;
        if (d.partitioned_gpt) {
            wchar_t id[64]{}; StringFromGUID2(layout->Gpt.DiskId, id, 64); d.id = utf8(id);
        } else if (layout->PartitionStyle == PARTITION_STYLE_MBR && layout->Mbr.Signature != 0) d.id = "mbr-" + std::to_string(layout->Mbr.Signature);
    }
    if (d.id.empty() && !d.serial.empty()) d.id = "serial-" + d.serial;
    d.identity_verified = !d.id.empty() && d.bytes > 0 && !d.model.empty();
    return d;
}
DeviceSnapshot discover(std::atomic_bool& cancel, const ProgressSink& progress) {
    DeviceSnapshot s; s.captured_at = utc_now(); s.winpe = is_winpe(); s.elevated = is_elevated();
    std::array<wchar_t, 256> computer{}; DWORD size = DWORD(computer.size());
    if (GetComputerNameW(computer.data(), &size)) s.machine = utf8(computer.data());
    if (!s.elevated) s.issues.push_back({"requires_elevation", "Algunos diagnósticos requieren permiso de administrador.", "Puedes seguir las guías sin elevar o volver a abrir con permisos."});
    // Query encryption before inspecting any Windows installation.
    struct Encryption { std::optional<bool> locked, encrypted; };
    std::map<std::string, Encryption> encryption;
    EncryptionProvider provider; Com<IEnumWbemClassObject> enumeration;
    const bool encryption_available = provider.query(enumeration);
    if (encryption_available) {
        while (!cancel) {
            Com<IWbemClassObject> object; ULONG count{};
            const HRESULT hr = enumeration->Next(3000, 1, object.put(), &count);
            if (hr == WBEM_S_TIMEDOUT) { s.issues.push_back({"encryption_timeout", "BitLocker no respondió a tiempo.", "Vuelve a detectar el equipo."}); break; }
            if (FAILED(hr) || count == 0) break;
            Encryption state; const auto path = property_string(object.p, L"__PATH");
            if (auto lock = provider.method(path, L"GetLockStatus", L"LockStatus")) state.locked = *lock != 0;
            if (auto conversion = provider.method(path, L"GetConversionStatus", L"ConversionStatus")) state.encrypted = *conversion != 0;
            encryption[normalized_id(utf8(property_string(object.p, L"DeviceID")))] = state;
        }
    } else s.issues.push_back({"encryption_unknown", "No se pudo determinar BitLocker con el proveedor disponible.", "Revisa los permisos o los componentes de WinPE; no implica que el disco esté sin cifrar."});
    std::array<wchar_t, 1024> name{};
    const HANDLE find = FindFirstVolumeW(name.data(), DWORD(name.size()));
    if (find == INVALID_HANDLE_VALUE) throw Failure("volumes_unavailable", "No se pudieron enumerar los volúmenes.", "Comprueba los controladores de almacenamiento y permisos.");
    struct FindCloser { HANDLE h; ~FindCloser() { FindVolumeClose(h); } } closer{find};
    std::array<wchar_t, 32768> windows{}; GetWindowsDirectoryW(windows.data(), DWORD(windows.size()));
    std::set<unsigned> disk_numbers;
    do {
        if (cancel) break;
        Volume v; const std::wstring guid(name.data()); v.id = utf8(guid); v.disks = volume_disks(guid);
        for (auto n : v.disks) disk_numbers.insert(n);
        std::array<wchar_t, 32768> mounts{}; DWORD required{};
        if (GetVolumePathNamesForVolumeNameW(guid.c_str(), mounts.data(), DWORD(mounts.size()), &required))
            for (auto p = mounts.data(); *p; p += wcslen(p) + 1) v.mounts.push_back(utf8(p));
        if (auto it = encryption.find(normalized_id(v.id)); it != encryption.end()) { v.locked = it->second.locked; v.encrypted = it->second.encrypted; }
        std::array<wchar_t, 1024> label{}, filesystem{};
        if (!v.locked.value_or(false)) {
            v.accessible = GetVolumeInformationW(guid.c_str(), label.data(), DWORD(label.size()), nullptr, nullptr, nullptr, filesystem.data(), DWORD(filesystem.size())) != 0;
            v.label = utf8(label.data()); v.filesystem = utf8(filesystem.data());
            ULARGE_INTEGER available{}, total{}, free{};
            if (v.accessible && GetDiskFreeSpaceExW(guid.c_str(), &available, &total, &free)) { v.bytes = total.QuadPart; v.free_bytes = available.QuadPart; }
        }
        if (v.locked.value_or(false)) v.issues.push_back({"bitlocker_locked", "Volumen bloqueado por BitLocker.", "Localiza la clave legítima antes de examinar sus archivos."});
        if (v.accessible && (v.filesystem == "NTFS" || v.filesystem == "ReFS")) {
            const auto candidate = fs::path(guid) / L"Windows";
            const DWORD attrs = GetFileAttributesW(candidate.c_str());
            if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) && !(attrs & FILE_ATTRIBUTE_REPARSE_POINT) &&
                GetFileAttributesW((candidate / L"System32\\config\\SYSTEM").c_str()) != INVALID_FILE_ATTRIBUTES &&
                GetFileAttributesW((candidate / L"System32\\ntoskrnl.exe").c_str()) != INVALID_FILE_ATTRIBUTES) {
                const auto current_storage = storage_identity(windows.data());
                const bool current = current_storage.volume_id == v.id;
                if (!(s.winpe && current)) {
                    const std::string display_root = v.mounts.empty() ? v.id : v.mounts.front();
                    s.installations.push_back({v.id + "Windows", v.id, display_root + "Windows", current});
                }
            }
        }
        s.volumes.push_back(v);
        if (progress) progress({"Detectando volúmenes e instalaciones", s.volumes.size(), 0});
    } while (FindNextVolumeW(find, name.data(), DWORD(name.size())));
    // Include physical disks even when they have no mountable volumes (missing disks are never declared healthy).
    for (unsigned n = 0; n < 64 && !cancel; ++n) {
        const auto path = L"\\\\.\\PhysicalDrive" + std::to_wstring(n);
        Handle h(CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
        if (h) disk_numbers.insert(n);
    }
    const auto current_storage = storage_identity(windows.data());
    for (auto n : disk_numbers) { auto d = inspect_disk(n); d.system = std::find(current_storage.disks.begin(), current_storage.disks.end(), n) != current_storage.disks.end(); s.disks.push_back(std::move(d)); }
    // Windows itself is authoritative for the running installation, even when a normal user cannot inspect offline registry hives.
    if (!s.winpe && !current_storage.volume_id.empty() && std::none_of(s.installations.begin(), s.installations.end(), [](const auto& i) { return i.current; }))
        s.installations.push_back({current_storage.volume_id + "Windows", current_storage.volume_id, utf8(windows.data()), true});
    if (s.installations.empty()) s.issues.push_back({"no_windows_detected", "No se detectó una instalación de Windows accesible.", "Comprueba BitLocker, permisos y controladores; no demuestra que Windows haya desaparecido."});
    s.complete = !cancel;
    return s;
}
void unlock_volume(const std::string& volume_id, std::wstring& key) {
    struct Wipe { std::wstring& value; ~Wipe() { if (!value.empty()) SecureZeroMemory(value.data(), value.size() * sizeof(wchar_t)); value.clear(); } } wipe{key};
    if (!valid_recovery_key(key)) throw Failure("key_format", "La clave no tiene un formato válido de BitLocker.", "Comprueba los 48 dígitos de la clave correspondiente.");
    EncryptionProvider provider; Com<IEnumWbemClassObject> list;
    if (!provider.query(list)) throw Failure("bitlocker_unavailable", "BitLocker no está disponible o requiere permisos.", "Revisa la elevación y el componente WinPE-SecureStartup.");
    while (true) {
        Com<IWbemClassObject> object; ULONG count{};
        const auto hr = list->Next(3000, 1, object.put(), &count);
        if (FAILED(hr) || hr == WBEM_S_TIMEDOUT || count == 0) break;
        if (normalized_id(utf8(property_string(object.p, L"DeviceID"))) != normalized_id(volume_id)) continue;
        if (provider.method(property_string(object.p, L"__PATH"), L"GetLockStatus", L"LockStatus") != std::optional<unsigned>(1))
            throw Failure("volume_not_locked", "El volumen no está bloqueado o su estado no se pudo confirmar.", "Actualiza el diagnóstico.");
        Bstr class_name(L"Win32_EncryptableVolume"), method_name(L"UnlockWithNumericalPassword");
        Com<IWbemClassObject> type, parameters, input, output;
        if (FAILED(provider.service->GetObject(class_name.p, 0, nullptr, type.put(), nullptr)) ||
            FAILED(type->GetMethod(method_name.p, 0, parameters.put(), nullptr)) || FAILED(parameters->SpawnInstance(0, input.put())))
            throw Failure("bitlocker_method", "No se pudo preparar el desbloqueo oficial.");
        VARIANT secret; VariantInit(&secret); secret.vt = VT_BSTR; secret.bstrVal = SysAllocStringLen(key.data(), UINT(key.size()));
        const auto put_result = input->Put(L"NumericalPassword", 0, &secret, 0);
        if (secret.bstrVal) SecureZeroMemory(secret.bstrVal, SysStringLen(secret.bstrVal) * sizeof(wchar_t));
        VariantClear(&secret);
        if (FAILED(put_result)) throw Failure("bitlocker_parameter", "No se pudo transmitir la clave al proveedor local.");
        Bstr object_path(property_string(object.p, L"__PATH").c_str());
        const auto execution = provider.service->ExecMethod(object_path.p, method_name.p, 0, nullptr, input.p, output.put(), nullptr);
        // Clear the provider input object's copy as soon as the call completes.
        VARIANT empty; VariantInit(&empty); empty.vt = VT_BSTR; empty.bstrVal = SysAllocString(L""); input->Put(L"NumericalPassword", 0, &empty, 0); VariantClear(&empty);
        if (FAILED(execution) || property_number(output.p, L"ReturnValue") != std::optional<unsigned>(0))
            throw Failure("unlock_failed", "La clave no permitió desbloquear el volumen.", "Comprueba que corresponde al identificador de recuperación de ese volumen.");
        if (provider.method(property_string(object.p, L"__PATH"), L"GetLockStatus", L"LockStatus") != std::optional<unsigned>(0))
            throw Failure("unlock_unverified", "El proveedor no confirmó que el volumen esté desbloqueado.", "Actualiza el diagnóstico.");
        return;
    }
    throw Failure("volume_missing", "El volumen seleccionado ya no está disponible.", "Vuelve a detectar los dispositivos.");
}
}
