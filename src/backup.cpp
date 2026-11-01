#include "recoverydesk/windows.hpp"
#include <windows.h>
#include <bcrypt.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>

namespace rd {
namespace {
using nlohmann::json;
struct File {
    HANDLE h{INVALID_HANDLE_VALUE};
    explicit File(HANDLE value) : h(value) {}
    ~File() { if (h != INVALID_HANDLE_VALUE) CloseHandle(h); }
    explicit operator bool() const { return h != INVALID_HANDLE_VALUE; }
};
struct DirectoryLocks {
    std::vector<std::unique_ptr<File>> handles;
    explicit DirectoryLocks(const fs::path& path) {
        const auto absolute = fs::absolute(path).lexically_normal();
        auto partial = absolute.root_path();
        const auto lock = [&](const fs::path& directory) {
            auto handle = std::make_unique<File>(CreateFileW(directory.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ,
                nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
            BY_HANDLE_FILE_INFORMATION info{};
            if (!*handle || !GetFileInformationByHandle(handle->h, &info) || !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
                (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
                throw Failure("directory_unstable", "No se pudo fijar una carpeta independiente de enlaces.", "Cierra las aplicaciones que estén modificando las carpetas y vuelve a intentarlo.");
            handles.push_back(std::move(handle));
        };
        lock(partial);
        for (const auto& component : absolute.relative_path()) { partial /= component; lock(partial); }
    }
};
struct Hash {
    BCRYPT_ALG_HANDLE algorithm{}; BCRYPT_HASH_HANDLE hash{};
    Hash() {
        if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
            throw Failure("hash_provider", "No se pudo abrir SHA-256.");
        if (BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) < 0) {
            BCryptCloseAlgorithmProvider(algorithm, 0); algorithm = nullptr;
            throw Failure("hash_create", "No se pudo iniciar SHA-256.");
        }
    }
    ~Hash() { if (hash) BCryptDestroyHash(hash); if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0); }
    void add(const void* data, DWORD length) {
        if (BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<void*>(data)), length, 0) < 0)
            throw Failure("hash_read", "No se pudo calcular la integridad.");
    }
    std::string finish() {
        std::array<unsigned char, 32> digest{};
        if (BCryptFinishHash(hash, digest.data(), DWORD(digest.size()), 0) < 0) throw Failure("hash_finish", "No se pudo finalizar SHA-256.");
        std::ostringstream out; for (auto b : digest) out << std::hex << std::setw(2) << std::setfill('0') << unsigned(b); return out.str();
    }
};
void check_cancel(std::atomic_bool& cancel) { if (cancel) throw Failure("cancelled", "Operación cancelada; se conserva el progreso verificado."); }
std::string hash_handle(HANDLE file, std::atomic_bool& cancel) {
    Hash hash; std::vector<char> buffer(1024 * 1024);
    while (true) {
        check_cancel(cancel); DWORD read{};
        if (!ReadFile(file, buffer.data(), DWORD(buffer.size()), &read, nullptr)) throw Failure("file_read", "No se pudo leer un archivo.");
        if (!read) break; hash.add(buffer.data(), read);
    }
    return hash.finish();
}
bool contains_reparse(const fs::path& path) {
    fs::path partial;
    for (const auto& component : fs::absolute(path).lexically_normal()) {
        partial /= component;
        const auto attr = GetFileAttributesW(partial.c_str());
        if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_REPARSE_POINT)) return true;
    }
    return false;
}
bool credential_or_system_path(const fs::path& path) {
    std::vector<std::wstring> components;
    for (const auto& part : fs::absolute(path).lexically_normal()) {
        auto name = part.wstring();
        std::transform(name.begin(), name.end(), name.begin(), [](wchar_t c) { return c >= L'A' && c <= L'Z' ? wchar_t(c + (L'a' - L'A')) : c; });
        components.push_back(std::move(name));
    }
    for (std::size_t i = 0; i < components.size(); ++i) {
        const auto& name = components[i];
        if (name == L"windows" || name == L".ssh" || name == L".gnupg" || name == L".aws" || name == L".azure" || name == L".kube") return true;
        if (name == L"system32" && i + 1 < components.size() && components[i + 1] == L"config") return true;
        if (name == L"appdata" && i + 3 < components.size()) {
            if (components[i + 2] == L"google" || components[i + 2] == L"mozilla") return true;
            if (components[i + 2] == L"microsoft") {
                const auto& area = components[i + 3];
                if (area == L"credentials" || area == L"protect" || area == L"vault" || area == L"ngc" || area == L"tokenbroker" || area == L"identitycache" || area == L"edge") return true;
            }
        }
    }
    return false;
}
bool same_or_child(const fs::path& child, const fs::path& parent) {
    const auto c = fs::weakly_canonical(child), p = fs::weakly_canonical(parent);
    auto ci = c.begin(), pi = p.begin();
    for (; pi != p.end(); ++pi, ++ci) if (ci == c.end() || _wcsicmp(ci->c_str(), pi->c_str()) != 0) return false;
    return true;
}
std::string path_text(const fs::path& path) { return utf8(path.generic_wstring()); }
struct ReadGuard {
    File file;
    BY_HANDLE_FILE_INFORMATION info{};
    explicit ReadGuard(const fs::path& path)
        : file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)) {
        if (!file || !GetFileInformationByHandle(file.h, &info)) throw Failure("source_in_use", "El archivo está en uso o no es accesible.");
        if (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_ENCRYPTED | FILE_ATTRIBUTE_OFFLINE | FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS | FILE_ATTRIBUTE_RECALL_ON_OPEN))
            throw Failure("protected_or_placeholder", "Se omitió un enlace, archivo EFS o archivo pendiente de descarga.");
        if (info.nNumberOfLinks > 1) throw Failure("hardlink", "Se omitió un archivo con enlaces físicos.");
    }
};
bool same_file_info(const BY_HANDLE_FILE_INFORMATION& a, const BY_HANDLE_FILE_INFORMATION& b) {
    return a.nFileSizeHigh == b.nFileSizeHigh && a.nFileSizeLow == b.nFileSizeLow &&
           CompareFileTime(&a.ftLastWriteTime, &b.ftLastWriteTime) == 0 &&
           a.nFileIndexHigh == b.nFileIndexHigh && a.nFileIndexLow == b.nFileIndexLow;
}
void write_manifest(const BackupJob& job, const std::string& source_id, const fs::path& path) {
    json j = {{"schema_version", schema_version}, {"source_id", source_id}, {"job_id", job.id}, {"status", job.status}, {"entries", json::array()}};
    for (const auto& e : job.entries) j["entries"].push_back({{"relative", e.relative}, {"status", e.status}, {"sha256", e.sha256}, {"bytes", e.bytes}, {"code", e.code}});
    if (contains_reparse(path)) throw Failure("manifest_link", "El registro de respaldo contiene un enlace no permitido.");
    const auto temporary = path.wstring() + L"." + wide(new_id()) + L".tmp";
    {
        std::ofstream out(fs::path(temporary), std::ios::binary); out << j.dump(2); out.close();
        if (!out) throw Failure("checkpoint_write", "No se pudo guardar el progreso de respaldo.", "Comprueba el destino antes de reanudar.");
    }
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str()); throw Failure("checkpoint_commit", "No se pudo confirmar el registro de respaldo.");
    }
}
}
std::string sha256_file(const fs::path& path, std::atomic_bool& cancel) {
    File file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (!file) throw Failure("hash_open", "No se pudo abrir un archivo para verificarlo.");
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(file.h, &info) || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) || info.nNumberOfLinks > 1)
        throw Failure("hash_link", "No se puede verificar un enlace como copia independiente.");
    return hash_handle(file.h, cancel);
}
void validate_backup_locations(const fs::path& source, const fs::path& destination, const StorageResolver& storage) {
    if (!fs::is_directory(source) || !fs::is_directory(destination)) throw Failure("backup_directory", "Selecciona dos carpetas existentes.");
    if (credential_or_system_path(source)) throw Failure("sensitive_source", "El respaldo selectivo excluye directorios de sistema y almacenes de credenciales.", "Selecciona una carpeta de documentos personales accesibles.");
    if (contains_reparse(source) || contains_reparse(destination)) throw Failure("backup_reparse", "Las carpetas no pueden atravesar enlaces o puntos de unión.");
    if (same_or_child(destination, source) || same_or_child(source, destination)) throw Failure("backup_nested", "El origen y destino no pueden contenerse entre sí.");
    const auto from = storage(source), to = storage(destination);
    if (!from.verified || !to.verified || !from.local || !to.local || from.disks.empty() || to.disks.empty())
        throw Failure("storage_unknown", "No se pudo verificar la unidad física de origen o destino.", "Selecciona unidades locales compatibles; RAID y red no están incluidos.");
    for (auto n : from.disks) if (std::find(to.disks.begin(), to.disks.end(), n) != to.disks.end())
        throw Failure("same_physical_disk", "El respaldo debe estar en otra unidad física.", "Conecta un disco externo; otra partición del mismo disco no protege tus datos.");
}
BackupJob run_backup(const fs::path& source, const fs::path& destination, const StorageResolver& storage,
                     std::atomic_bool& cancel, const ProgressSink& progress) {
    validate_backup_locations(source, destination, storage);
    DirectoryLocks source_lock(source), destination_lock(destination);
    const auto from = storage(source), to = storage(destination);
    const auto source_id = from.volume_id + ":" + path_text(fs::canonical(source));
    const auto manifest = destination / L".recoverydesk-manifest.json";
    BackupJob job; job.id = new_id(); job.started_at = utc_now(); job.source = source; job.destination = destination;
    std::map<std::string, BackupEntry> previous;
    if (fs::exists(manifest)) {
        if (contains_reparse(manifest) || fs::file_size(manifest) > 64 * 1024 * 1024) throw Failure("manifest_invalid", "El registro previo no es válido.");
        std::ifstream file(manifest, std::ios::binary); const auto old = json::parse(file);
        if (old.at("schema_version") != schema_version || old.at("source_id") != source_id)
            throw Failure("backup_other_source", "Este destino ya contiene un respaldo de otro origen.", "Selecciona una carpeta dedicada diferente.");
        for (const auto& e : old.at("entries")) if (e.at("status") == "verified") {
            const std::string relative = e.at("relative");
            previous[relative] = {relative, "verified", e.at("sha256"), {}, e.at("bytes")};
        }
    } else if (!fs::is_empty(destination)) {
        throw Failure("destination_not_empty", "Elige una carpeta vacía para el primer respaldo.", "Crea una carpeta dedicada en el disco externo.");
    }
    const auto checkpoint = [&] {
        // Preserve verified entries not yet revisited when cancellation or a power loss interrupts a resumed job.
        BackupJob saved = job;
        for (const auto& [name, entry] : previous)
            if (std::none_of(saved.entries.begin(), saved.entries.end(), [&](const auto& e) { return e.relative == name; })) saved.entries.push_back(entry);
        write_manifest(saved, source_id, manifest);
    };
    std::vector<fs::path> files;
    try {
        std::error_code error;
        fs::recursive_directory_iterator iterator(source, fs::directory_options::none, error), end;
        if (error) throw Failure("source_unreadable", "No se pudo examinar la carpeta de origen.");
        while (iterator != end) {
            check_cancel(cancel);
            const auto path = iterator->path(); const auto relative = path.lexically_relative(source);
            const auto name = path_text(relative); const auto attr = GetFileAttributesW(path.c_str());
            if (credential_or_system_path(path)) {
                iterator.disable_recursion_pending(); job.entries.push_back({name, "skipped", {}, "system_or_credentials", 0}); ++job.skipped;
            } else if (attr == INVALID_FILE_ATTRIBUTES || (attr & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_ENCRYPTED | FILE_ATTRIBUTE_OFFLINE | FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS | FILE_ATTRIBUTE_RECALL_ON_OPEN))) {
                iterator.disable_recursion_pending(); job.entries.push_back({name, "skipped", {}, "protected_or_placeholder", 0}); ++job.skipped;
            } else if (!(attr & FILE_ATTRIBUTE_DIRECTORY)) {
                std::error_code size_error; const auto size = fs::file_size(path, size_error);
                if (size_error || size > to.max_file_bytes) { job.entries.push_back({name, "skipped", {}, "size_or_filesystem", 0}); ++job.skipped; }
                else { files.push_back(path); job.estimated_bytes += size; }
            }
            iterator.increment(error);
            if (error) { ++job.skipped; job.entries.push_back({name, "skipped", {}, "enumeration_denied", 0}); error.clear(); }
            if (progress) progress({"Estimando archivos y espacio", files.size(), 0});
        }
        // Conservative upper bound also reserves space for a replacement while the previous copy still exists.
        if (job.estimated_bytes > to.free_bytes || to.free_bytes - job.estimated_bytes < 1024 * 1024)
            throw Failure("backup_space", "El destino no tiene espacio suficiente para una copia verificada.", "Libera espacio o selecciona otra unidad.");
        job.status = "running"; checkpoint();
        std::uint64_t done = 0;
        for (const auto& path : files) {
            check_cancel(cancel);
            const auto relative_path = path.lexically_relative(source); const auto relative = path_text(relative_path);
            BackupEntry entry{relative, "skipped", {}, {}, 0};
            const auto target = destination / relative_path;
            const auto temporary = target.wstring() + L".rd-partial-" + wide(new_id());
            try {
                validate_backup_locations(source, destination, storage);
                const auto current_from = storage(source), current_to = storage(destination);
                if (current_from.volume_id != from.volume_id || current_to.volume_id != to.volume_id || current_from.disks != from.disks || current_to.disks != to.disks)
                    throw Failure("storage_changed", "La identidad del almacenamiento cambió durante el respaldo.");
                if (contains_reparse(path) || contains_reparse(target)) throw Failure("path_changed", "La ruta ahora contiene un enlace.");
                DirectoryLocks source_parents(path.parent_path());
                ReadGuard input(path);
                entry.bytes = (std::uint64_t(input.info.nFileSizeHigh) << 32) | input.info.nFileSizeLow;
                if (entry.bytes > to.max_file_bytes) throw Failure("file_grew", "El archivo supera el tamaño admitido por el destino.");
                const auto source_hash = hash_handle(input.file.h, cancel);
                if (fs::exists(target)) {
                    const auto prior = previous.find(relative);
                    if (prior == previous.end() || sha256_file(target, cancel) != prior->second.sha256)
                        throw Failure("destination_conflict", "El archivo de destino fue creado o modificado fuera de este respaldo.");
                    if (source_hash == prior->second.sha256) {
                        entry.status = "verified"; entry.sha256 = source_hash; job.verified_bytes += entry.bytes;
                        job.entries.push_back(entry); ++done; checkpoint();
                        if (progress) progress({"Copia previa verificada", done, files.size()});
                        continue;
                    }
                }
                fs::create_directories(target.parent_path());
                DirectoryLocks target_parents(target.parent_path());
                if (contains_reparse(target.parent_path())) throw Failure("destination_link", "El destino contiene un enlace no permitido.");
                LARGE_INTEGER zero{};
                if (!SetFilePointerEx(input.file.h, zero, nullptr, FILE_BEGIN)) throw Failure("source_seek", "No se pudo volver a leer el archivo.");
                {
                    File output(CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr));
                    if (!output) throw Failure("destination_open", "No se pudo crear el archivo de destino.");
                    std::vector<char> buffer(1024 * 1024);
                    while (true) {
                        check_cancel(cancel); DWORD read{}, written{};
                        if (!ReadFile(input.file.h, buffer.data(), DWORD(buffer.size()), &read, nullptr)) throw Failure("source_read", "El origen dejó de estar disponible.");
                        if (!read) break;
                        if (!WriteFile(output.h, buffer.data(), read, &written, nullptr) || written != read) throw Failure("destination_write", "No se pudo completar la escritura.");
                    }
                    if (!FlushFileBuffers(output.h)) throw Failure("destination_flush", "No se pudo confirmar la escritura en el dispositivo.");
                }
                BY_HANDLE_FILE_INFORMATION after{};
                if (!GetFileInformationByHandle(input.file.h, &after) || !same_file_info(input.info, after)) throw Failure("source_changed", "El archivo cambió durante la copia.");
                if (sha256_file(temporary, cancel) != source_hash) throw Failure("hash_mismatch", "La copia no coincide con el archivo de origen.");
                // Only replace a destination that belonged to the previously verified manifest.
                const DWORD flags = MOVEFILE_WRITE_THROUGH | (fs::exists(target) ? MOVEFILE_REPLACE_EXISTING : 0);
                if (!MoveFileExW(temporary.c_str(), target.c_str(), flags)) throw Failure("copy_commit", "No se pudo confirmar el archivo verificado.");
                entry.status = "verified"; entry.sha256 = source_hash; job.verified_bytes += entry.bytes;
            } catch (const Failure& e) {
                DeleteFileW(temporary.c_str());
                if (e.code == "cancelled" || e.code == "storage_changed") throw;
                entry.code = e.code; ++job.skipped;
            } catch (const fs::filesystem_error&) {
                DeleteFileW(temporary.c_str()); entry.code = "filesystem_error"; ++job.skipped;
            }
            job.entries.push_back(std::move(entry)); ++done; checkpoint();
            if (progress) progress({"Copiando y verificando SHA-256", done, files.size()});
        }
        const auto verified_count = std::count_if(job.entries.begin(), job.entries.end(), [](const auto& e) { return e.status == "verified"; });
        job.status = job.skipped ? (verified_count ? "partial" : "failed") : "complete";
    } catch (const Failure& e) {
        if (e.code != "cancelled") throw;
        job.status = "cancelled";
    }
    checkpoint();
    return job;
}
}
