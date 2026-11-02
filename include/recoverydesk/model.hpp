#pragma once
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace rd {
namespace fs = std::filesystem;
inline constexpr int schema_version = 1;
inline constexpr const char* version = "0.1.0";
enum class Problem { signin, files, diagnostics };
enum class Account { unknown, microsoft, local, corporate, pin, bitlocker };
enum class Outcome { pending, confirmed_by_user, unavailable };
struct Issue { std::string code, message, next_step; };
struct Disk {
    unsigned number{};
    std::string id, model, serial, bus;
    std::uint64_t bytes{};
    bool usb{}, system{}, identity_verified{}, partitioned_gpt{};
};
struct Volume {
    std::string id, label, filesystem;
    std::vector<std::string> mounts;
    std::vector<unsigned> disks;
    std::uint64_t bytes{}, free_bytes{};
    bool accessible{};
    std::optional<bool> locked, encrypted;
    std::vector<Issue> issues;
};
struct Installation {
    std::string id, volume_id, windows_path;
    bool current{};
};
struct DeviceSnapshot {
    std::string machine, captured_at;
    bool winpe{}, elevated{}, complete{};
    std::optional<bool> secure_boot;
    std::vector<Disk> disks;
    std::vector<Volume> volumes;
    std::vector<Installation> installations;
    std::vector<Issue> issues;
};
struct Guide {
    std::string id, title, language, source, reviewed_on;
    std::vector<std::string> requirements, steps;
};
struct RecoveryAssessment {
    Account account{Account::unknown};
    std::string guide_id;
    std::vector<Issue> issues;
    Outcome outcome{Outcome::pending};
};
struct BackupEntry {
    std::string relative, status, sha256, code;
    std::uint64_t bytes{};
};
struct BackupJob {
    std::string id, status{"pending"}, started_at;
    fs::path source, destination;
    std::uint64_t estimated_bytes{}, verified_bytes{}, skipped{};
    std::vector<BackupEntry> entries;
};
struct SessionReport {
    std::string id, started_at, installation_id;
    DeviceSnapshot device;
    RecoveryAssessment assessment;
    std::optional<BackupJob> backup;
    std::vector<std::string> events;
};
struct Progress {
    std::string stage;
    std::uint64_t completed{}, total{};
};
using ProgressSink = std::function<void(const Progress&)>;
struct StorageIdentity {
    std::string volume_id, filesystem;
    std::vector<unsigned> disks;
    std::uint64_t free_bytes{}, max_file_bytes{};
    bool local{}, verified{};
};
using StorageResolver = std::function<StorageIdentity(const fs::path&)>;
}
