#include "recoverydesk/windows.hpp"
#include <windows.h>
#include <nlohmann/json.hpp>
#include <fstream>

namespace rd {
namespace {
std::optional<bool> secure_boot_state() {
    HANDLE token{};
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) return {};
    TOKEN_PRIVILEGES request{}, previous{}; DWORD previous_bytes = sizeof(previous);
    request.PrivilegeCount = 1; request.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    std::optional<bool> result;
    if (LookupPrivilegeValueW(nullptr, SE_SYSTEM_ENVIRONMENT_NAME, &request.Privileges[0].Luid)) {
        SetLastError(ERROR_SUCCESS);
        const bool adjusted = AdjustTokenPrivileges(token, FALSE, &request, sizeof(previous), &previous, &previous_bytes) != FALSE;
        if (adjusted && GetLastError() == ERROR_SUCCESS) {
            BYTE enabled{};
            if (GetFirmwareEnvironmentVariableW(L"SecureBoot", L"{8be4df61-93ca-11d2-aa0d-00e098032b8c}", &enabled, sizeof(enabled)) == sizeof(enabled))
                result = enabled == 1;
            AdjustTokenPrivileges(token, FALSE, &previous, 0, nullptr, nullptr);
        }
    }
    CloseHandle(token); return result;
}
}
void write_boot_evidence(const fs::path& output) {
    std::atomic_bool cancel{false}; const auto device = discover(cancel);
    const auto secure = secure_boot_state();
    bool bitlocker_available = true;
    for (const auto& issue : device.issues)
        if (issue.code == "encryption_unknown" || issue.code == "encryption_timeout") bitlocker_available = false;
    nlohmann::json evidence = {{"schema_version", 1}, {"app_version", version}, {"captured_at", utc_now()},
        {"stage", "native_gui_started"}, {"winpe", device.winpe}, {"discovery_complete", device.complete},
        {"bitlocker_provider_available", bitlocker_available}, {"volumes", device.volumes.size()},
        {"physical_hardware_certified", false}};
#ifdef _MSC_VER
    evidence["compiler"] = "MSVC";
#else
    evidence["compiler"] = "experimental";
#endif
    evidence["secure_boot"] = secure ? nlohmann::json(*secure) : nlohmann::json(nullptr);
    const auto content = evidence.dump() + "\r\n";
    { std::ofstream file(output, std::ios::binary); file << content; }
    // Only an explicitly requested validation boot emits evidence. No identifiers or secrets are included.
    // In an isolated VM this serial device is backed by a host-side evidence file.
    if (!device.winpe) return;
    HANDLE serial = CreateFileW(L"\\\\.\\COM1", GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (serial == INVALID_HANDLE_VALUE) return;
    DCB state{}; state.DCBlength = sizeof(state);
    if (GetCommState(serial, &state)) {
        state.BaudRate = CBR_115200; state.ByteSize = 8; state.Parity = NOPARITY; state.StopBits = ONESTOPBIT;
        state.fOutxCtsFlow = FALSE; state.fOutxDsrFlow = FALSE; state.fOutX = FALSE; state.fInX = FALSE;
        SetCommState(serial, &state);
        COMMTIMEOUTS timeouts{}; timeouts.WriteTotalTimeoutConstant = 3000; SetCommTimeouts(serial, &timeouts);
        DWORD written{}; WriteFile(serial, content.data(), DWORD(content.size()), &written, nullptr); FlushFileBuffers(serial);
    }
    CloseHandle(serial);
}
}
