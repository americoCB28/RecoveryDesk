#pragma once
#include "core.hpp"
#include <memory>

namespace rd {
std::wstring wide(const std::string& value);
std::string utf8(const std::wstring& value);
bool is_winpe();
bool is_elevated();
fs::path executable_directory();
DeviceSnapshot discover(std::atomic_bool& cancel, const ProgressSink& progress = {});
StorageIdentity storage_identity(const fs::path& path);
Disk inspect_disk(unsigned number);
void unlock_volume(const std::string& volume_id, std::wstring& key);
struct MediaPreflight {
    bool ready{};
    fs::path adk_root;
    std::vector<Issue> issues;
};
MediaPreflight media_preflight();
void validate_media_identity(const Disk& expected, const Disk& current, const std::vector<Disk>& protected_disks);
void validate_media_target(const Disk& expected, const std::vector<Disk>& protected_disks);
void build_media(const fs::path& app_directory, const fs::path& work,
                 const ProgressSink& progress, std::atomic_bool& cancel);
void write_media(const fs::path& work, const Disk& expected, const std::wstring& drive,
                 const ProgressSink& progress, std::atomic_bool& cancel, const std::vector<fs::path>& protected_paths = {});
}
