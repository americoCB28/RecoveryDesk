#include "recoverydesk/windows.hpp"
#include <windows.h>
#include <algorithm>
#include <fstream>

namespace rd {
namespace {
std::wstring quote(const std::wstring& text) {
    std::wstring out = L"\""; unsigned slashes = 0;
    for (wchar_t c : text) {
        if (c == L'\\') { ++slashes; continue; }
        if (c == L'\"') out.append(slashes * 2 + 1, L'\\'); else out.append(slashes, L'\\');
        out += c; slashes = 0;
    }
    out.append(slashes * 2, L'\\'); return out + L"\"";
}
void script(const fs::path& file, const std::vector<std::pair<std::wstring, std::wstring>>& arguments,
            std::atomic_bool& cancel, const ProgressSink& progress) {
    if (!is_elevated()) throw Failure("media_requires_admin", "Preparar medios requiere permiso de administrador.", "Utiliza Abrir como administrador y vuelve a seleccionar el USB.");
    if (cancel) throw Failure("cancelled", "Operación cancelada antes de iniciar.");
    wchar_t system[MAX_PATH]{}; GetSystemDirectoryW(system, MAX_PATH);
    const fs::path powershell = fs::path(system) / L"WindowsPowerShell\\v1.0\\powershell.exe";
    std::wstring command = quote(powershell.wstring()) + L" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File " + quote(file.wstring());
    for (const auto& [key, value] : arguments) command += L" -" + key + L" " + quote(value);
    STARTUPINFOW startup{}; startup.cb = sizeof(startup); PROCESS_INFORMATION process{};
    if (!CreateProcessW(powershell.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process))
        throw Failure("media_process", "No se pudo iniciar el constructor de medios.", "Comprueba Windows PowerShell y los permisos.");
    struct Cleanup { PROCESS_INFORMATION p; ~Cleanup() { CloseHandle(p.hThread); CloseHandle(p.hProcess); } } cleanup{process};
    while (WaitForSingleObject(process.hProcess, 250) == WAIT_TIMEOUT) {
        // Do not terminate DISM or a USB format midway. The script creates checkpoints between stages.
        if (progress) progress({"Herramientas ADK trabajando; no desconectes el USB", 0, 0});
    }
    DWORD exit_code{}; GetExitCodeProcess(process.hProcess, &exit_code);
    if (exit_code == 1223) throw Failure("cancelled", "La preparación se canceló entre etapas.");
    if (exit_code != 0) throw Failure("media_failed", "El constructor de medios no completó la operación.", "Revisa build.log en la carpeta de trabajo. No se declara el medio como listo.");
}
}
MediaPreflight media_preflight() {
    MediaPreflight p;
#ifndef _MSC_VER
    p.issues.push_back({"pe_requires_static_msvc", "Este binario LLVM-MinGW está validado solo para Windows completo.", "Para construir WinPE, utiliza la compilación MSVC con CRT estático /MT y valida sus dependencias en el laboratorio."});
#endif
    if (is_winpe()) { p.issues.push_back({"builder_windows_only", "El USB se prepara desde una sesión normal de Windows.", "Ejecuta RecoveryDesk en una PC auxiliar."}); return p; }
    wchar_t kits[32768]{}; DWORD bytes = sizeof(kits);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows Kits\\Installed Roots", L"KitsRoot10", RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, nullptr, kits, &bytes) == ERROR_SUCCESS)
        p.adk_root = fs::path(kits) / L"Assessment and Deployment Kit";
    else {
        wchar_t program_files[32768]{};
        if (GetEnvironmentVariableW(L"ProgramFiles(x86)", program_files, 32768))
            p.adk_root = fs::path(program_files) / L"Windows Kits\\10\\Assessment and Deployment Kit";
    }
    if (p.adk_root.empty() || !fs::exists(p.adk_root / L"Deployment Tools\\DandISetEnv.bat") ||
        !fs::exists(p.adk_root / L"Windows Preinstallation Environment\\copype.cmd") ||
        !fs::exists(p.adk_root / L"Windows Preinstallation Environment\\amd64\\en-us\\winpe.wim"))
        p.issues.push_back({"adk_missing", "Faltan Deployment Tools o Windows PE Add-on (amd64, en-us).", "Instala los componentes oficiales del ADK; RecoveryDesk no los descarga ni acepta licencias por ti."});
    if (!is_elevated()) p.issues.push_back({"media_requires_admin", "El constructor necesita permisos de administrador.", "Abre RecoveryDesk como administrador antes de preparar el medio."});
    p.ready = p.issues.empty(); return p;
}
void validate_media_identity(const Disk& expected, const Disk& current, const std::vector<Disk>& protected_disks) {
    if (!expected.usb || !current.usb || expected.system || current.system || !expected.identity_verified || !current.identity_verified ||
        expected.number != current.number || expected.id != current.id || expected.serial != current.serial || expected.model != current.model || expected.bytes != current.bytes)
        throw Failure("media_identity", "La unidad no es un USB verificable o su identidad cambió.", "Vuelve a detectar y seleccionar el dispositivo.");
    for (const auto& d : protected_disks) if (d.number == current.number || (!d.id.empty() && d.id == current.id))
        throw Failure("media_protected", "La unidad contiene el sistema, la aplicación o datos necesarios para esta sesión.", "Selecciona otro USB dedicado.");
}
void validate_media_target(const Disk& expected, const std::vector<Disk>& protected_disks) {
    validate_media_identity(expected, inspect_disk(expected.number), protected_disks);
}
void build_media(const fs::path& app_directory, const fs::path& work, const ProgressSink& progress, std::atomic_bool& cancel) {
    const auto p = media_preflight();
    if (!p.ready) throw Failure(p.issues.front().code, p.issues.front().message, p.issues.front().next_step);
    if (fs::exists(work)) throw Failure("media_work_exists", "La carpeta de trabajo debe ser nueva para evitar sobrescribir archivos.");
    script(app_directory / L"boot\\Build-WinPE.ps1", {{L"ApplicationDirectory", app_directory.wstring()},
           {L"WorkDirectory", work.wstring()}, {L"AdkRoot", p.adk_root.wstring()}}, cancel, progress);
    if (!fs::exists(work / L"media\\sources\\boot.wim") || !fs::exists(work / L"recoverydesk-ready.json"))
        throw Failure("media_incomplete", "La imagen no pasó la comprobación final.");
}
void write_media(const fs::path& work, const Disk& expected, const std::wstring& drive,
                 const ProgressSink& progress, std::atomic_bool& cancel, const std::vector<fs::path>& protected_paths) {
    if (drive.size() != 2 || drive[0] < L'A' || drive[0] > L'Z' || drive[1] != L':') throw Failure("media_drive", "La unidad de destino no es válida.");
    if (!fs::exists(work / L"recoverydesk-ready.json")) throw Failure("media_not_built", "Prepara primero la imagen WinPE de esta sesión.");
    const auto target = storage_identity(drive + L"\\");
    if (!target.verified || target.disks != std::vector<unsigned>{expected.number}) throw Failure("media_mapping", "La letra de unidad ya no corresponde al USB seleccionado.");
    std::atomic_bool detect_cancel{false}; const auto s = discover(detect_cancel);
    std::vector<Disk> protect;
    auto required_paths = protected_paths;
    required_paths.push_back(executable_directory()); required_paths.push_back(work);
    for (const auto& d : s.disks) {
        bool needed = d.system;
        for (const auto& i : s.installations) for (const auto& v : s.volumes)
            if (v.id == i.volume_id && std::find(v.disks.begin(), v.disks.end(), d.number) != v.disks.end()) needed = true;
        for (const auto& path : required_paths) {
            const auto identity = storage_identity(path);
            if (!identity.verified) throw Failure("media_protection_unknown", "No se pudo verificar la unidad de trabajo o de la aplicación.");
            if (std::find(identity.disks.begin(), identity.disks.end(), d.number) != identity.disks.end()) needed = true;
        }
        if (needed) protect.push_back(d);
    }
    validate_media_target(expected, protect);
    const auto p = media_preflight(); if (!p.ready) throw Failure("adk_missing", "El ADK ya no está disponible o faltan permisos.");
    script(executable_directory() / L"boot\\Write-USB.ps1", {{L"WorkDirectory", work.wstring()}, {L"AdkRoot", p.adk_root.wstring()},
        {L"DiskNumber", std::to_wstring(expected.number)}, {L"ExpectedSerial", wide(expected.serial)},
        {L"ExpectedIdentity", wide(expected.id)},
        {L"ExpectedBytes", std::to_wstring(expected.bytes)}, {L"DriveLetter", drive}}, cancel, progress);
}
}
