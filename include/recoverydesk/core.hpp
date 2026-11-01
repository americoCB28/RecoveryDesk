#pragma once
#include "model.hpp"
#include <stdexcept>

namespace rd {
class Failure : public std::runtime_error {
public:
    std::string code, next_step;
    Failure(std::string c, std::string message, std::string next = {});
};
std::string utc_now();
std::string new_id();
std::vector<Guide> load_guides(const fs::path& path);
RecoveryAssessment assess(Account account);
std::string outcome_text(Outcome outcome);
std::string html_escape(const std::string& text);
std::string report_json(const SessionReport& report, bool technical = false);
std::string report_html(const SessionReport& report, bool technical = false);
void export_reports(const SessionReport& report, const fs::path& directory, bool technical);
std::string sha256_file(const fs::path& path, std::atomic_bool& cancel);
void validate_backup_locations(const fs::path& source, const fs::path& destination, const StorageResolver& storage);
BackupJob run_backup(const fs::path& source, const fs::path& destination,
                     const StorageResolver& storage, std::atomic_bool& cancel, const ProgressSink& progress = {});
bool valid_recovery_key(const std::wstring& key);
}
