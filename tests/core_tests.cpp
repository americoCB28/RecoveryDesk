#include "recoverydesk/windows.hpp"
#include <windows.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <functional>

using namespace rd;
using nlohmann::json;
namespace {
unsigned passed = 0, failed = 0;
void require(bool condition, const std::string& message) { if (!condition) throw std::runtime_error(message); }
void test(const char* name, const std::function<void()>& run) {
    try { run(); ++passed; std::cout << "PASS " << name << '\n'; }
    catch (const std::exception& e) { ++failed; std::cout << "FAIL " << name << ": " << e.what() << '\n'; }
}
void rejects(const std::function<void()>& run, const std::string& expected) {
    try { run(); } catch (const Failure& e) { require(e.code == expected, "wrong error: " + e.code + " expected " + expected); return; }
    throw std::runtime_error("expected rejection: " + expected);
}
void put(const fs::path& path, const std::string& bytes) { fs::create_directories(path.parent_path()); std::ofstream f(path, std::ios::binary); f << bytes; }
std::string get(const fs::path& path) { std::ifstream f(path, std::ios::binary); return {std::istreambuf_iterator<char>(f), {}}; }
struct Fixture {
    fs::path root = fs::temp_directory_path() / (L"RecoveryDesk-test-" + wide(new_id()));
    fs::path source = root / L"source", destination = root / L"destination";
    std::atomic_bool cancel{false};
    Fixture() { fs::create_directories(source); fs::create_directories(destination); }
    ~Fixture() {
        // Delete only the unique synthetic test directory under the system temporary directory.
        std::error_code ec;
        if (root.parent_path() == fs::temp_directory_path() && root.filename().wstring().starts_with(L"RecoveryDesk-test-")) fs::remove_all(root, ec);
    }
    StorageResolver storage() {
        return [this](const fs::path& p) {
            StorageIdentity s; s.local = s.verified = true; s.free_bytes = 1024ULL * 1024 * 1024;
            s.filesystem = "NTFS"; s.max_file_bytes = UINT64_MAX;
            if (p == source) { s.volume_id = "synthetic-source"; s.disks = {10001}; }
            else { s.volume_id = "synthetic-destination"; s.disks = {10002}; }
            return s;
        };
    }
};
SessionReport sample_report() {
    SessionReport r; r.id = "sample-session"; r.started_at = "2026-11-01T00:00:00Z";
    r.device.machine = "PRIVATE-HOST"; r.device.disks.push_back({0, "PRIVATE-ID", "Example", "PRIVATE-SERIAL", "NVMe"});
    r.assessment = assess(Account::microsoft);
    r.backup = BackupJob{}; r.backup->source = L"C:\\PRIVATE-USER\\Documents";
    r.backup->entries.push_back({"PRIVATE-FILE.txt", "verified", "synthetic-hash", "", 10}); return r;
}
}
int wmain(int argc, wchar_t** argv) {
    if (argc < 2 || argc > 3) return 2;
    test("six official guides validate", [&] { const auto guides = load_guides(argv[1]); require(guides.size() == 6, "guide count"); });
    test("all account categories have pending official routes", [] {
        for (int i = 0; i < 6; ++i) { const auto a = assess(static_cast<Account>(i)); require(!a.guide_id.empty() && a.outcome == Outcome::pending, "route or false success"); }
    });
    test("guide catalog rejects external executable links", [&] {
        Fixture f; auto j = json::parse(get(argv[1])); j["guides"][0]["source"] = "https://support.microsoft.com.evil.invalid/run";
        put(f.root / L"guides.json", j.dump()); rejects([&] { load_guides(f.root / L"guides.json"); }, "guide_source");
    });
    test("guide catalog requires every route", [&] {
        Fixture f; auto j = json::parse(get(argv[1])); j["guides"].erase(0); put(f.root / L"guides.json", j.dump());
        rejects([&] { load_guides(f.root / L"guides.json"); }, "guide_missing");
    });
    test("personal report strips identifiers and file paths", [] {
        auto r = sample_report(); r.events = {"PRIVATE-RAW-EVENT", "discovery_completed"}; const auto data = report_json(r);
        require(data.find("PRIVATE-") == std::string::npos, "private field leaked");
        require(json::parse(data).at("schema_version") == 1, "version");
    });
    test("technical report opts in to readable UTF8 paths", [] {
        const auto r = sample_report(); const auto j = json::parse(report_json(r, true));
        require(j.at("machine") == "PRIVATE-HOST", "machine absent");
        require(j.at("backup").at("source").is_string(), "path must be a string");
    });
    test("report escapes script injection", [] {
        auto r = sample_report(); r.device.machine = "<script>alert('x')</script>";
        const auto html = report_html(r, true);
        require(html.find("<script>") == std::string::npos && html.find("&lt;script&gt;") != std::string::npos, "HTML injection");
    });
    test("report never infers recovered access", [] { require(outcome_text(Outcome::pending).find("Pendiente") != std::string::npos, "false recovery"); });
    test("export writes both formats and rejects overwrite", [] {
        Fixture f; auto r = sample_report(); export_reports(r, f.destination, false);
        require(fs::exists(f.destination / L"RecoveryDesk-sample-session.html"), "HTML absent");
        require(fs::exists(f.destination / L"RecoveryDesk-sample-session.json"), "JSON absent");
        rejects([&] { export_reports(r, f.destination, false); }, "report_exists");
    });
    test("report identifiers cannot traverse directories", [] {
        Fixture f; auto r = sample_report(); r.id = "x/../../escape";
        rejects([&] { export_reports(r, f.destination, false); }, "report_id");
        require(fs::is_empty(f.destination), "unsafe report wrote files");
    });
    test("recovery key checks length characters and group checksum", [] {
        require(!valid_recovery_key(L"123"), "short accepted");
        require(!valid_recovery_key(L"000012-000011-000011-000011-000011-000011-000011-000011"), "checksum accepted");
        require(valid_recovery_key(L"000011-000011-000011-000011-000011-000011-000011-000011"), "valid synthetic key rejected");
        require(!valid_recovery_key(L"999999-999999-999999-999999-999999-999999-999999-999999"), "invalid range");
    });
    test("SHA256 known vector", [] {
        Fixture f; put(f.source / L"a", "abc");
        require(sha256_file(f.source / L"a", f.cancel) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "wrong digest");
    });
    test("same physical disk rejected even across volumes", [] {
        Fixture f; auto storage = f.storage();
        rejects([&] { validate_backup_locations(f.source, f.destination, [&](const auto& p) { auto s = storage(p); s.disks = {4}; return s; }); }, "same_physical_disk");
        require(fs::is_empty(f.destination), "wrote before safety check");
    });
    test("unknown storage identity rejected", [] {
        Fixture f; rejects([&] { validate_backup_locations(f.source, f.destination, [](const auto&) { return StorageIdentity{}; }); }, "storage_unknown");
    });
    test("nested backup rejected", [] {
        Fixture f; fs::create_directory(f.source / L"inside");
        rejects([&] { validate_backup_locations(f.source, f.source / L"inside", f.storage()); }, "backup_nested");
    });
    test("verified copy supports unicode and nested files", [] {
        Fixture f; put(f.source / L"niño\\información.txt", "synthetic documents"); put(f.source / L"zero", "");
        const auto job = run_backup(f.source, f.destination, f.storage(), f.cancel);
        require(job.status == "complete" && job.entries.size() == 2 && job.verified_bytes == 19, "copy summary");
        require(get(f.destination / L"niño\\información.txt") == "synthetic documents", "contents differ");
    });
    test("resume revalidates destination and source", [] {
        Fixture f; put(f.source / L"a", "first");
        run_backup(f.source, f.destination, f.storage(), f.cancel);
        const auto again = run_backup(f.source, f.destination, f.storage(), f.cancel); require(again.status == "complete", "resume failed");
        put(f.source / L"a", "updated source");
        const auto changed = run_backup(f.source, f.destination, f.storage(), f.cancel);
        require(changed.status == "complete" && get(f.destination / L"a") == "updated source", "new source not copied");
    });
    test("modified backup is never silently overwritten", [] {
        Fixture f; put(f.source / L"a", "source"); run_backup(f.source, f.destination, f.storage(), f.cancel);
        put(f.destination / L"a", "user edited backup");
        const auto job = run_backup(f.source, f.destination, f.storage(), f.cancel);
        require(job.status == "failed" && job.skipped == 1, "altered backup accepted");
        require(get(f.destination / L"a") == "user edited backup", "user file overwritten");
    });
    test("cancellation preserves verified resume checkpoints", [] {
        Fixture f; put(f.source / L"a", "a"); put(f.source / L"b", "b");
        const auto partial = run_backup(f.source, f.destination, f.storage(), f.cancel, [&](const Progress& p) { if (p.stage == "Copiando y verificando SHA-256") f.cancel = true; });
        require(partial.status == "cancelled", "cancel ignored"); f.cancel = false;
        const auto resumed = run_backup(f.source, f.destination, f.storage(), f.cancel); require(resumed.status == "complete", "resume lost checkpoint");
        f.cancel = true; const auto cancelled = run_backup(f.source, f.destination, f.storage(), f.cancel); require(cancelled.status == "cancelled", "early cancel ignored");
        f.cancel = false; require(run_backup(f.source, f.destination, f.storage(), f.cancel).status == "complete", "early cancellation erased manifest");
    });
    test("source locked by writer is skipped", [] {
        Fixture f; put(f.source / L"busy", "busy");
        const auto handle = CreateFileW((f.source / L"busy").c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        require(handle != INVALID_HANDLE_VALUE, "fixture open");
        const auto job = run_backup(f.source, f.destination, f.storage(), f.cancel); CloseHandle(handle);
        require(job.skipped == 1 && job.status == "failed", "locked file copied");
    });
    test("hardlinked files are skipped", [] {
        Fixture f; put(f.source / L"a", "a"); require(CreateHardLinkW((f.source / L"b").c_str(), (f.source / L"a").c_str(), nullptr), "fixture hardlink");
        const auto job = run_backup(f.source, f.destination, f.storage(), f.cancel); require(job.skipped == 2, "hardlink copied");
    });
    test("backup excludes system and credential stores", [] {
        Fixture f; put(f.source / L"AppData\\Roaming\\Microsoft\\Protect\\opaque", "synthetic secret");
        put(f.source / L"Windows\\System32\\config\\SAM", "synthetic hive");
        put(f.source / L"notes.txt", "documents");
        const auto job = run_backup(f.source, f.destination, f.storage(), f.cancel);
        require(job.status == "partial" && job.skipped == 2, "sensitive stores not reported");
        require(!fs::exists(f.destination / L"Windows") && !fs::exists(f.destination / L"AppData\\Roaming\\Microsoft\\Protect"), "credential store exported");
        require(fs::exists(f.destination / L"notes.txt"), "ordinary files skipped");
    });
    test("filesystem file size limit is enforced", [] {
        Fixture f; put(f.source / L"large", "12345"); auto storage = f.storage();
        const auto job = run_backup(f.source, f.destination, [&](const auto& p) { auto s = storage(p); s.max_file_bytes = 4; return s; }, f.cancel);
        require(job.skipped == 1 && !fs::exists(f.destination / L"large"), "oversize copied");
    });
    test("low space fails before file creation", [] {
        Fixture f; put(f.source / L"a", "a"); auto storage = f.storage();
        rejects([&] { run_backup(f.source, f.destination, [&](const auto& p) { auto s = storage(p); s.free_bytes = 0; return s; }, f.cancel); }, "backup_space");
        require(fs::is_empty(f.destination), "wrote with no space");
    });
    test("first backup rejects a nonempty destination", [] {
        Fixture f; put(f.destination / L"precious", "keep");
        rejects([&] { run_backup(f.source, f.destination, f.storage(), f.cancel); }, "destination_not_empty"); require(get(f.destination / L"precious") == "keep", "user data changed");
    });
    test("resume rejects another source", [] {
        Fixture f; put(f.source / L"a", "a"); run_backup(f.source, f.destination, f.storage(), f.cancel);
        const auto other = f.root / L"other"; fs::create_directory(other);
        auto storage = f.storage();
        rejects([&] { run_backup(other, f.destination, [&](const auto& p) { auto s = storage(p); if (p == other) { s.disks = {10003}; s.volume_id = "another-source"; } return s; }, f.cancel); }, "backup_other_source");
    });
    test("USB identity rejects swapped disk", [] {
        Disk expected{9, "usb-identity", "Synthetic USB", "ABC", "USB", 16ULL*1024*1024*1024, true, false, true};
        auto current = expected; current.id = "different-usb";
        rejects([&] { validate_media_identity(expected, current, {}); }, "media_identity");
    });
    test("USB identity rejects internal and system disks", [] {
        Disk disk{9, "usb-identity", "Synthetic USB", "ABC", "USB", 16ULL*1024*1024*1024, true, false, true};
        auto internal = disk; internal.usb = false;
        rejects([&] { validate_media_identity(internal, internal, {}); }, "media_identity");
        auto system = disk; system.system = true;
        rejects([&] { validate_media_identity(system, system, {}); }, "media_identity");
        rejects([&] { validate_media_identity(disk, disk, {disk}); }, "media_protected");
        validate_media_identity(disk, disk, {});
    });
    test("storage identity changes stop the job", [] {
        Fixture f; put(f.source / L"a", "a"); put(f.source / L"b", "b");
        auto storage = f.storage(); bool swapped = false;
        rejects([&] { run_backup(f.source, f.destination, [&](const auto& path) { auto s = storage(path); if (swapped && path == f.destination) s.volume_id = "swapped-volume"; return s; },
            f.cancel, [&](const auto& p) { if (p.stage == "Copiando y verificando SHA-256") swapped = true; }); }, "storage_changed");
        const auto manifest = json::parse(get(f.destination / L".recoverydesk-manifest.json"));
        require(manifest.at("entries").size() == 1, "lost verified progress after disconnect");
    });
    if (argc == 3 && std::wstring(argv[2]) == L"--probe") test("real Windows adapter read-only probe", [] {
        std::atomic_bool cancel{false}; const auto snapshot = discover(cancel);
        require(snapshot.complete && !snapshot.machine.empty() && !snapshot.volumes.empty(), "real inventory incomplete");
        if (!snapshot.winpe) require(std::any_of(snapshot.installations.begin(), snapshot.installations.end(), [](const auto& i) { return i.current; }), "running Windows installation missing");
        std::cout << "  Probe counts only: disks=" << snapshot.disks.size() << " volumes=" << snapshot.volumes.size()
            << " installations=" << snapshot.installations.size() << " issues=" << snapshot.issues.size() << '\n';
    });
    std::cout << "\n" << passed << " passed, " << failed << " failed\n"; return failed ? 1 : 0;
}
