#include "recoverydesk/core.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <random>
#include <set>
#include <sstream>
#include <ctime>

namespace rd {
using nlohmann::json;
Failure::Failure(std::string c, std::string message, std::string next)
    : std::runtime_error(std::move(message)), code(std::move(c)), next_step(std::move(next)) {}
std::string utc_now() {
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{}; gmtime_s(&tm, &now);
    std::ostringstream out; out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ"); return out.str();
}
std::string new_id() {
    std::random_device random;
    std::ostringstream out;
    for (int i = 0; i < 4; ++i) out << std::hex << std::setw(8) << std::setfill('0') << random();
    return out.str();
}
std::vector<Guide> load_guides(const fs::path& path) {
    if (fs::file_size(path) > 1024 * 1024) throw Failure("guides_size", "El catálogo de guías es demasiado grande.");
    std::ifstream file(path, std::ios::binary);
    const auto root = json::parse(file);
    if (root.at("schema_version") != schema_version || root.at("guides").size() > 32)
        throw Failure("guides_schema", "El catálogo de guías no es compatible.");
    std::vector<Guide> result;
    std::set<std::string> ids;
    for (const auto& item : root.at("guides")) {
        Guide guide{item.at("id"), item.at("title"), item.at("language"), item.at("source"), item.at("reviewed_on"),
                    item.at("requirements").get<std::vector<std::string>>(), item.at("steps").get<std::vector<std::string>>()};
        if (!(guide.source.starts_with("https://support.microsoft.com/") ||
              guide.source.starts_with("https://learn.microsoft.com/")) || guide.source.find_first_of("\r\n\t") != std::string::npos)
            throw Failure("guide_source", "La guía no apunta a documentación oficial permitida.");
        if (!ids.insert(guide.id).second || guide.steps.empty() || guide.steps.size() > 20 || guide.language != "es")
            throw Failure("guide_invalid", "El catálogo contiene una guía inválida.");
        result.push_back(std::move(guide));
    }
    for (const auto* required : {"unknown", "microsoft", "local", "corporate", "pin", "bitlocker"})
        if (!ids.contains(required)) throw Failure("guide_missing", "Falta una guía necesaria para la recuperación.");
    return result;
}
RecoveryAssessment assess(Account account) {
    RecoveryAssessment result; result.account = account;
    switch (account) {
    case Account::microsoft: result.guide_id = "microsoft"; break;
    case Account::local: result.guide_id = "local"; break;
    case Account::corporate: result.guide_id = "corporate"; break;
    case Account::pin: result.guide_id = "pin"; break;
    case Account::bitlocker: result.guide_id = "bitlocker"; break;
    default: result.guide_id = "unknown";
        result.issues.push_back({"account_unknown", "El tipo de cuenta no se ha determinado.", "Selecciona el problema que ves en la pantalla de inicio de sesión."});
    }
    return result;
}
std::string outcome_text(Outcome outcome) {
    if (outcome == Outcome::confirmed_by_user) return "Acceso confirmado por el usuario";
    if (outcome == Outcome::unavailable) return "Recuperación no disponible con los medios actuales";
    return "Pendiente de comprobar en Windows";
}
std::string html_escape(const std::string& text) {
    std::string out;
    for (char c : text) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        case '\'': out += "&#39;"; break;
        default: out += c;
        }
    }
    return out;
}
static json issue_json(const Issue& issue) { return {{"code", issue.code}, {"message", issue.message}, {"next_step", issue.next_step}}; }
std::string report_json(const SessionReport& r, bool technical) {
    json j = {{"schema_version", schema_version}, {"app_version", version}, {"session_id", r.id},
        {"started_at", r.started_at}, {"technical_details", technical},
        {"environment", r.device.winpe ? "WinPE" : "Windows"}, {"discovery_complete", r.device.complete},
        {"disk_count", r.device.disks.size()}, {"installation_count", r.device.installations.size()},
        {"assessment", {{"guide_id", r.assessment.guide_id}, {"outcome", outcome_text(r.assessment.outcome)}}}};
    // Deliberately whitelist fields. Personal exports contain no host names, serials, file names or paths.
    j["events"] = json::array();
    static const std::set<std::string> allowed_events{
        "simulation_only", "outcome_selected_by_user", "discovery_completed", "discovery_failed", "discovery_cancelled",
        "backup_completed", "backup_failed", "backup_cancelled", "unlock_completed", "unlock_failed",
        "media_prepare_completed", "media_prepare_failed", "media_write_completed", "media_write_failed", "report_completed", "report_failed"};
    for (const auto& event : r.events) if (allowed_events.contains(event)) j["events"].push_back(event);
    if (r.backup) {
        const auto& b = *r.backup;
        j["backup"] = {{"status", b.status}, {"estimated_bytes", b.estimated_bytes}, {"verified_bytes", b.verified_bytes},
                        {"skipped", b.skipped}, {"files", b.entries.size()}};
        if (technical) {
            const auto source = b.source.u8string(), destination = b.destination.u8string();
            j["backup"]["source"] = std::string(source.begin(), source.end());
            j["backup"]["destination"] = std::string(destination.begin(), destination.end());
            j["backup"]["entries"] = json::array();
            for (const auto& e : b.entries) j["backup"]["entries"].push_back({{"relative", e.relative}, {"status", e.status},
                {"sha256", e.sha256}, {"bytes", e.bytes}, {"code", e.code}});
        }
    }
    if (technical) {
        j["machine"] = r.device.machine;
        j["selected_installation"] = r.installation_id;
        j["disks"] = json::array(); j["volumes"] = json::array(); j["issues"] = json::array();
        for (const auto& d : r.device.disks) j["disks"].push_back({{"id", d.id}, {"model", d.model}, {"serial", d.serial},
            {"bus", d.bus}, {"bytes", d.bytes}, {"usb", d.usb}, {"identity_verified", d.identity_verified}});
        for (const auto& v : r.device.volumes) {
            json item = {{"id", v.id}, {"mounts", v.mounts}, {"filesystem", v.filesystem}, {"accessible", v.accessible}, {"disks", v.disks}};
            item["locked"] = v.locked ? json(*v.locked) : json(nullptr);
            item["encrypted"] = v.encrypted ? json(*v.encrypted) : json(nullptr);
            j["volumes"].push_back(item);
        }
        for (const auto& issue : r.device.issues) j["issues"].push_back(issue_json(issue));
    }
    return j.dump(2, ' ', false, json::error_handler_t::replace);
}
std::string report_html(const SessionReport& r, bool technical) {
    std::ostringstream o;
    o << "<!doctype html><html lang=\"es\"><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width\">"
      << "<meta http-equiv=\"Content-Security-Policy\" content=\"default-src 'none'; style-src 'unsafe-inline'\">"
      << "<title>Informe RecoveryDesk</title><style>body{font:16px/1.6 system-ui,sans-serif;background:#f8fafc;color:#172033;max-width:900px;margin:40px auto;padding:24px}"
      << "h1{color:#1d4ed8}section{background:white;padding:24px;border:1px solid #cbd5e1;border-radius:12px;margin:16px 0}pre{white-space:pre-wrap;overflow-wrap:anywhere;font-size:13px}</style>"
      << "<h1>RecoveryDesk</h1><p>Informe de recuperación · " << html_escape(r.started_at) << "</p><section><h2>Resultado</h2><p>"
      << html_escape(outcome_text(r.assessment.outcome)) << "</p><p>Instalaciones detectadas: " << r.device.installations.size() << "</p></section>";
    if (r.backup) o << "<section><h2>Respaldo</h2><p>Estado: " << html_escape(r.backup->status) << "</p><p>Bytes verificados: "
                   << r.backup->verified_bytes << " · Archivos omitidos: " << r.backup->skipped << "</p></section>";
    o << "<section><h2>Registro " << (technical ? "técnico" : "anonimizado") << "</h2><pre>" << html_escape(report_json(r, technical))
      << "</pre></section><p>Este informe no certifica una recuperación de acceso salvo confirmación del usuario.</p></html>";
    return o.str();
}
void export_reports(const SessionReport& report, const fs::path& directory, bool technical) {
    if (report.id.empty() || report.id.size() > 64 || std::any_of(report.id.begin(), report.id.end(), [](unsigned char c) {
        return !(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z') && !(c >= '0' && c <= '9') && c != '-';
    })) throw Failure("report_id", "El identificador del informe no es válido.");
    fs::create_directories(directory);
    for (const auto& pair : std::vector<std::pair<std::string, std::string>>{{"json", report_json(report, technical)}, {"html", report_html(report, technical)}}) {
        const auto filename = "RecoveryDesk-" + report.id + "." + pair.first;
        const auto target = directory / fs::path(std::u8string(filename.begin(), filename.end()));
        if (fs::exists(target)) throw Failure("report_exists", "Ya existe un informe de esta sesión en el destino.", "Selecciona una carpeta nueva.");
        const auto temporary = directory / (".rd-report-" + new_id() + ".tmp");
        std::ofstream file(temporary, std::ios::binary); file << pair.second; file.close();
        if (!file) throw Failure("report_write", "No se pudo escribir el informe.", "Comprueba permisos y espacio libre.");
        try { fs::rename(temporary, target); }
        catch (...) { std::error_code ignored; fs::remove(temporary, ignored); throw Failure("report_commit", "No se pudo confirmar el informe; el destino no se sobrescribió."); }
    }
}
bool valid_recovery_key(const std::wstring& key) {
    unsigned digits = 0, group_digits = 0, group = 0;
    for (const auto c : key) {
        if (c >= L'0' && c <= L'9') {
            if (++digits > 48) return false;
            group = group * 10 + unsigned(c - L'0');
            if (++group_digits == 6) {
                if (group % 11 != 0 || group / 11 >= 65536) return false;
                group = group_digits = 0;
            }
        } else if (c != L'-' && c != L' ') return false;
    }
    return digits == 48 && group_digits == 0;
}
}
