#include "recoverydesk/windows.hpp"
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shellapi.h>
#include <uxtheme.h>
#include <algorithm>
#include <array>
#include <functional>
#include <memory>
#include <sstream>
#include <thread>

namespace {
using namespace rd;
constexpr UINT progress_message = WM_APP + 1, finished_message = WM_APP + 2, render_message = WM_APP + 3;
enum Page { home, detection, guides, backup, media, report };
enum Control {
    nav_home = 100, nav_detect, nav_guides, nav_backup, nav_media, nav_report,
    action_detect = 200, action_signin, action_files, action_diagnostics, action_admin, action_technical_view,
    account_choice, installation_choice, action_official, volume_choice, action_unlock,
    source_choose, destination_choose, action_backup, action_cancel,
    media_prepare, media_write, media_target, action_adk, outcome_choice, technical_check, action_export
};
struct Completion {
    std::string operation, error, next_step;
    std::optional<DeviceSnapshot> snapshot;
    std::optional<BackupJob> backup_job;
    fs::path directory;
};
struct State {
    HWND window{}, status{}, progress{}, cancel_button{};
    HINSTANCE instance{};
    HFONT regular{}, small_font{}, heading{}, title{}, brand{};
    HBRUSH background{}, white{}, sidebar{};
    std::vector<HWND> children;
    SessionReport session;
    std::vector<Guide> catalogue;
    Page page{home};
    Account account{Account::unknown};
    fs::path source, destination, media_work;
    std::vector<std::pair<Disk, std::wstring>> usb_choices;
    std::vector<std::string> locked_volumes;
    std::atomic_bool cancel{false};
    std::thread worker;
    bool busy{}, cancellable{}, closing{}, demo{}, technical{}, high_contrast{}, rendering{};
    bool diagnostic_technical{};
    int selected_usb{}, selected_locked{}, selected_installation{};
    int dpi{96};
    int scroll_y{}, scroll_max{};
    int client_width{}, client_height{};
    std::wstring status_text{L"Listo · Tus datos permanecen en este equipo"};
} app;
int px(int value) { return MulDiv(value, app.dpi, 96); }
std::wstring text(const std::string& value) { return wide(value); }
std::wstring bytes(std::uint64_t n) {
    std::wostringstream out; out.setf(std::ios::fixed); out.precision(1);
    out << double(n) / (1024.0 * 1024 * 1024) << L" GB"; return out.str();
}
COLORREF foreground() { return app.high_contrast ? GetSysColor(COLOR_WINDOWTEXT) : RGB(27, 39, 60); }
COLORREF surface() { return app.high_contrast ? GetSysColor(COLOR_WINDOW) : RGB(248, 250, 253); }
void set_font(HWND control, HFONT font) { SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE); }
HWND control(const wchar_t* type, const std::wstring& value, DWORD style, int x, int y, int w, int h, int id = 0, HFONT font = nullptr, DWORD ex = 0) {
    const auto handle = CreateWindowExW(ex, type, value.c_str(), WS_CHILD | WS_VISIBLE | style,
        px(x), px(y - app.scroll_y), px(w), px(h), app.window, reinterpret_cast<HMENU>(INT_PTR(id)), app.instance, nullptr);
    if (!handle) throw Failure("ui_control", "No se pudo crear un componente de interfaz.");
    set_font(handle, font ? font : app.regular); app.children.push_back(handle); return handle;
}
HWND label(const std::wstring& value, int x, int y, int w, int h, HFONT font = nullptr) {
    return control(L"STATIC", value, SS_LEFT | SS_NOPREFIX, x, y, w, h, 0, font);
}
HWND button(const std::wstring& value, int id, int x, int y, int w = 210, int h = 38) {
    return control(L"BUTTON", value, WS_TABSTOP | BS_PUSHBUTTON, x, y, w, h, id);
}
HWND combo(int id, int x, int y, int w, const std::vector<std::wstring>& items, int selected) {
    auto c = control(WC_COMBOBOXW, L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, x, y, w, 300, id);
    for (const auto& item : items) SendMessageW(c, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item.c_str()));
    SendMessageW(c, CB_SETCURSEL, selected, 0); return c;
}
HWND readonly(const std::wstring& value, int x, int y, int w, int h, bool multiline = true) {
    return control(L"EDIT", value, WS_TABSTOP | ES_READONLY | (multiline ? ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL : ES_AUTOHSCROLL),
                   x, y, w, h, 0, app.regular, WS_EX_CLIENTEDGE);
}
std::wstring folder(const wchar_t* description) {
    // The legacy native picker is available in WinPE; modern shell dialogs are not assumed.
    BROWSEINFOW info{}; info.hwndOwner = app.window; info.lpszTitle = description;
    info.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    auto item = SHBrowseForFolderW(&info); if (!item) return {};
    std::array<wchar_t, 32768> path{};
    const bool ok = SHGetPathFromIDListEx(item, path.data(), DWORD(path.size()), GPFIDL_DEFAULT) != 0;
    CoTaskMemFree(item); return ok ? std::wstring(path.data()) : std::wstring();
}
void error_dialog(const std::string& message, const std::string& next = {}) {
    const auto detail = text(message) + (next.empty() ? L"" : L"\n\nSiguiente paso: " + text(next));
    MessageBoxW(app.window, detail.c_str(), L"RecoveryDesk · Revisa este paso", MB_OK | MB_ICONINFORMATION);
}
void init_fonts() {
    HIGHCONTRASTW hc{}; hc.cbSize = sizeof(hc);
    SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0); app.high_contrast = (hc.dwFlags & HCF_HIGHCONTRASTON) != 0;
    const auto make = [](int height, int weight) { return CreateFontW(-px(height), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI"); };
    for (auto font : {app.regular, app.small_font, app.heading, app.title, app.brand}) if (font) DeleteObject(font);
    app.regular = make(15, FW_NORMAL); app.small_font = make(12, FW_NORMAL); app.heading = make(21, FW_SEMIBOLD);
    app.title = make(31, FW_SEMIBOLD); app.brand = make(22, FW_BOLD);
    for (auto brush : {app.background, app.white, app.sidebar}) if (brush) DeleteObject(brush);
    app.background = CreateSolidBrush(surface()); app.white = CreateSolidBrush(app.high_contrast ? GetSysColor(COLOR_WINDOW) : RGB(255,255,255));
    app.sidebar = CreateSolidBrush(app.high_contrast ? GetSysColor(COLOR_WINDOW) : RGB(239,244,252));
}
void clear_controls() { for (auto child : app.children) DestroyWindow(child); app.children.clear(); }
void render();
void begin(std::string operation, bool cancellable, std::function<Completion()> task) {
    if (app.busy) return;
    if (app.worker.joinable()) app.worker.join();
    app.busy = true; app.cancellable = cancellable; app.cancel = false;
    app.status_text = cancellable ? L"Trabajando · Puedes cancelar de forma controlada" : L"Preparando el medio · Espera a que termine la etapa; no desconectes unidades";
    render();
    app.worker = std::thread([operation = std::move(operation), task = std::move(task)] {
        auto result = std::make_unique<Completion>(); result->operation = operation;
        try { *result = task(); result->operation = operation; }
        catch (const Failure& e) { result->error = e.what(); result->next_step = e.next_step; }
        catch (const std::exception&) { result->error = "La operación no pudo completarse. No se declara como correcta."; result->next_step = "Comprueba los permisos, el dispositivo y el espacio disponible."; }
        if (PostMessageW(app.window, finished_message, 0, reinterpret_cast<LPARAM>(result.get()))) result.release();
    });
}
ProgressSink progress_sink() {
    return [](const Progress& progress) {
        static thread_local ULONGLONG last = 0;
        const auto now = GetTickCount64();
        if (now - last < 120 && !(progress.total && progress.completed == progress.total)) return;
        last = now; auto value = std::make_unique<Progress>(progress);
        if (PostMessageW(app.window, progress_message, 0, reinterpret_cast<LPARAM>(value.get()))) value.release();
    };
}
void detect() {
    if (app.demo) return;
    begin("discovery", true, [] { Completion c; c.snapshot = discover(app.cancel, progress_sink()); return c; });
}
std::wstring diagnostic_text() {
    std::wostringstream out;
    if (!app.diagnostic_technical) {
        out << L"TU EQUIPO, EN POCAS PALABRAS\r\n\r\n";
        if (!app.session.device.complete) out << L"El diagnóstico está pendiente o incompleto. Selecciona Detectar de nuevo.\r\n\r\n";
        out << app.session.device.disks.size() << L" discos y " << app.session.device.installations.size() << L" instalaciones de Windows detectadas.\r\n\r\n";
        const auto locked = std::count_if(app.session.device.volumes.begin(), app.session.device.volumes.end(), [](const auto& v) { return v.locked.value_or(false); });
        out << (locked ? L"Hay volúmenes bloqueados por BitLocker. Necesitarás su clave legítima para leerlos." : L"No se han confirmado volúmenes BitLocker bloqueados. Los estados desconocidos requieren revisión.") << L"\r\n\r\n";
        for (const auto& issue : app.session.device.issues) out << text(issue.message) << L"\r\n" << text(issue.next_step) << L"\r\n\r\n";
        out << L"Siguiente paso\r\nAbre Opciones de acceso para resolver un problema de inicio de sesión, o Proteger archivos para respaldar una carpeta accesible.\r\n\r\nLa vista técnica muestra las evidencias disponibles. Este inventario no certifica la salud física de los discos.";
        return out.str();
    }
    out << L"ESTADO DE LA SESIÓN\r\n" << (app.demo ? L"Datos simulados para demostración" : L"Detección real de este equipo")
        << L" · " << (app.session.device.elevated ? L"Administrador" : L"Permisos normales") << L"\r\n\r\n";
    if (!app.session.device.complete) out << L"Inventario pendiente o incompleto. No se ha terminado la detección.\r\n\r\n";
    for (const auto& d : app.session.device.disks) out << L"Disco " << d.number << L" · " << text(d.model) << L" · " << bytes(d.bytes)
        << L" · " << text(d.bus) << (d.system ? L" · Sistema" : L"") << (d.identity_verified ? L"" : L" · Identidad no verificada") << L"\r\n";
    out << L"\r\nVOLÚMENES\r\n";
    for (const auto& v : app.session.device.volumes) {
        out << (v.mounts.empty() ? text(v.id) : text(v.mounts.front())) << L" · " << text(v.filesystem)
            << L" · " << (v.accessible ? L"Accesible" : L"No accesible") << L" · BitLocker: ";
        if (v.locked.value_or(false)) out << L"BLOQUEADO";
        else if (v.encrypted) out << (*v.encrypted ? L"cifrado" : L"sin cifrar") << (v.locked ? L" / desbloqueado" : L" / bloqueo desconocido");
        else out << L"no se pudo determinar";
        out << L"\r\n";
    }
    out << L"\r\nINSTALACIONES\r\n";
    for (const auto& i : app.session.device.installations) out << text(i.windows_path) << (i.current ? L" · En ejecución" : L" · Offline") << L"\r\n";
    if (app.session.device.installations.empty()) out << L"Sin instalaciones accesibles detectadas. Revisa cifrado, permisos y controladores.\r\n";
    for (const auto& issue : app.session.device.issues) out << L"\r\n[" << text(issue.code) << L"] " << text(issue.message) << L"\r\n→ " << text(issue.next_step) << L"\r\n";
    out << L"\r\nEl inventario no certifica la salud física del disco. No se realizan reparaciones ni cambios de cuentas.";
    return out.str();
}
void render() {
    if (!app.window || app.rendering) return;
    app.rendering = true;
    struct RenderingGuard { ~RenderingGuard() { app.rendering = false; } } guard;
    const auto previous_focus = GetDlgCtrlID(GetFocus());
    clear_controls();
    RECT rect{}; GetClientRect(app.window, &rect);
    const int width = MulDiv(rect.right, 96, app.dpi), actual_height = MulDiv(rect.bottom, 96, app.dpi);
    const int height = std::max(800, actual_height);
    app.scroll_max = std::max(0, height - actual_height);
    app.scroll_y = std::clamp(app.scroll_y, 0, app.scroll_max);
    SCROLLINFO scroll_info{sizeof(scroll_info), SIF_RANGE | SIF_PAGE | SIF_POS, 0, height - 1, UINT(actual_height), app.scroll_y, 0};
    SetScrollInfo(app.window, SB_VERT, &scroll_info, TRUE);
    const int x = 274, w = width - x - 36;
    label(L"RecoveryDesk", 26, 32, 214, 32, app.brand);
    label(L"RECUPERA CON CONFIANZA", 28, 72, 210, 20, app.small_font);
    const std::array<std::wstring, 6> names{L"&Inicio", L"&1   Detectar equipo", L"&2   Opciones de acceso", L"&3   Proteger archivos", L"&4   Preparar USB", L"&5   Resultado e informe"};
    for (int i = 0; i < 6; ++i) {
        auto nav = button(names[i], nav_home + i, 20, 132 + i * 52, 222, 42);
        if (i == app.page) SendMessageW(nav, BM_SETSTYLE, BS_DEFPUSHBUTTON, TRUE);
        EnableWindow(nav, !app.busy);
    }
    label(L"OFFLINE POR DISEÑO", 28, height - 150, 208, 20, app.small_font);
    label(L"Código abierto · Apache-2.0\r\nVersión 0.1.0 · Alpha", 28, height - 122, 208, 52, app.small_font);
    const auto machine = app.session.device.machine.empty() ? L"Equipo pendiente de detectar" : text(app.session.device.machine);
    label(machine + L"  /  " + (app.demo ? L"DEMOSTRACIÓN" : app.session.device.winpe ? L"USB · WinPE" : L"Windows · Este equipo"), x, 26, w, 25, app.small_font);
    std::wstring selected = L"Instalación: ninguna seleccionada";
    if (!app.session.device.installations.empty()) selected = L"Instalación: " + text(app.session.device.installations.at(app.selected_installation).windows_path);
    label(selected, x, 55, w, 24, app.small_font);
    const std::array<std::wstring,6> headings{L"Vuelve a tener el control.", L"Conoce el equipo antes de actuar.", L"Elige una ruta de recuperación.", L"Primero, protege lo importante.", L"Tu ayuda, lista para arrancar.", L"Un resultado claro y verificable."};
    label(headings[app.page], x, 108, w, 48, app.title);
    const int bottom = height - 94;
    switch (app.page) {
    case home: {
        label(L"Diagnóstico, orientación oficial y copias verificadas.\r\nEmpieza por lo que necesitas resolver hoy.", x, 165, w, 56);
        const auto card = [&](const wchar_t* title, const wchar_t* note, int id, int y) {
            auto b = control(L"BUTTON", title, WS_TABSTOP | BS_COMMANDLINK, x, y, w, 105, id, app.heading);
            SendMessageW(b, BCM_SETNOTE, 0, reinterpret_cast<LPARAM>(note));
        };
        card(L"No puedo iniciar sesión", L"Distingue PIN, contraseña y BitLocker. Sigue la opción oficial adecuada.", action_signin, 245);
        card(L"Necesito proteger mis archivos", L"Copia a otro disco físico y comprueba cada archivo con SHA-256.", action_files, 365);
        card(L"Quiero diagnosticar el equipo", L"Consulta discos, cifrado e instalaciones de Windows sin modificar cuentas.", action_diagnostics, 485);
        label(L"¿Tu laptop está bloqueada? Consulta la guía desde otra PC o prepara un USB para examinarla.\r\nUtiliza RecoveryDesk solo en equipos propios o con autorización de su propietario.", x, 620, w, 62, app.small_font);
        break;
    }
    case detection: {
        label(L"La detección corresponde al equipo indicado arriba. No se presupone que Windows esté en C:.", x, 165, w, 45);
        button(L"Detectar de nuevo", action_detect, x, 218, 200);
        button(L"Abrir como administrador", action_admin, x + 218, 218, 240);
        button(app.diagnostic_technical ? L"Vista sencilla" : L"Vista técnica", action_technical_view, x + 476, 218, 180);
        std::vector<std::wstring> items;
        for (const auto& i : app.session.device.installations) items.push_back(text(i.windows_path));
        if (!items.empty()) combo(installation_choice, x, 272, w, items, app.selected_installation);
        readonly(diagnostic_text(), x, items.empty() ? 277 : 315, w, bottom - (items.empty() ? 277 : 315));
        break;
    }
    case guides: {
        label(L"¿Qué te pide Windows? Elige según la pantalla del equipo afectado. No introduzcas contraseñas aquí.", x, 165, w, 45);
        combo(account_choice, x, 218, w, {L"No estoy seguro", L"Contraseña de cuenta Microsoft", L"Contraseña de cuenta local", L"Cuenta de trabajo o institución", L"PIN de Windows Hello", L"Clave de recuperación de BitLocker"}, int(app.account));
        const auto selected_guide = std::find_if(app.catalogue.begin(), app.catalogue.end(), [](const auto& g) { return g.id == app.session.assessment.guide_id; });
        if (selected_guide != app.catalogue.end()) {
            label(text(selected_guide->title), x, 269, w, 36, app.heading);
            std::wstring content;
            for (const auto& requirement : selected_guide->requirements) content += L"• " + text(requirement) + L"\r\n";
            content += L"\r\n";
            for (std::size_t i = 0; i < selected_guide->steps.size(); ++i) content += std::to_wstring(i + 1) + L". " + text(selected_guide->steps[i]) + L"\r\n\r\n";
            readonly(content, x, 310, w, std::max(155, bottom - 470));
            button(L"Abrir documentación oficial", action_official, x, bottom - 150, 270);
            label(L"Revisada: " + text(selected_guide->reviewed_on) + L" · Necesita navegador e Internet", x + 286, bottom - 145, w - 286, 42, app.small_font);
            readonly(text(selected_guide->source), x, bottom - 101, w, 30, false);
        }
        app.locked_volumes.clear(); std::vector<std::wstring> volumes;
        for (const auto& v : app.session.device.volumes) if (v.locked.value_or(false)) {
            app.locked_volumes.push_back(v.id); volumes.push_back((v.mounts.empty() ? text(v.id) : text(v.mounts.front())) + L" · BitLocker bloqueado");
        }
        if (!volumes.empty()) {
            app.selected_locked = std::min(app.selected_locked, int(volumes.size()) - 1);
            combo(volume_choice, x, bottom - 51, w - 250, volumes, app.selected_locked);
            button(L"Desbloquear con mi clave", action_unlock, x + w - 236, bottom - 55, 236);
        } else label(L"El diagnóstico no ha confirmado volúmenes BitLocker bloqueados.", x, bottom - 48, w, 42, app.small_font);
        break;
    }
    case backup: {
        label(L"Selecciona carpetas concretas. El destino debe ser una carpeta vacía o un respaldo previo del mismo origen, en otro disco físico.", x, 165, w, 60);
        label(L"ORIGEN · Solo archivos accesibles", x, 248, w, 24, app.small_font);
        readonly(app.source.empty() ? L"Elige la carpeta que quieres proteger" : app.source.wstring(), x, 280, w - 182, 36, false);
        button(L"Elegir origen…", source_choose, x + w - 168, 279, 168);
        label(L"DESTINO · Otra unidad física", x, 344, w, 24, app.small_font);
        readonly(app.destination.empty() ? L"Elige una carpeta dedicada en tu disco externo" : app.destination.wstring(), x, 376, w - 182, 36, false);
        button(L"Elegir destino…", destination_choose, x + w - 168, 375, 168);
        label(L"Copia verificada con SHA-256\r\nLos archivos incompletos no se aceptan como correctos. Una sesión interrumpida puede reanudarse eligiendo las mismas carpetas.", x, 447, w, 80, app.heading);
        label(L"Se omiten almacenes de credenciales, enlaces, EFS, archivos en uso y contenido cloud no descargado.\r\nEl manifiesto del respaldo contiene nombres de archivos: consérvalo junto a la copia.", x, 548, w, 70, app.small_font);
        button(L"Iniciar / reanudar respaldo", action_backup, x, bottom - 50, 285, 42);
        break;
    }
    case media: {
        label(L"Prepara el USB desde una PC con Windows. Necesitas ADK Deployment Tools, WinPE Add-on y un USB dedicado con una partición accesible de hasta 32 GB.", x, 165, w, 65);
        const auto p = media_preflight();
        std::wstring requirements = p.ready ? L"✓ ADK, WinPE amd64 y permisos disponibles" : L"REQUISITOS PENDIENTES\r\n";
        for (const auto& issue : p.issues) requirements += text(issue.message) + L"\r\n" + text(issue.next_step) + L"\r\n";
        readonly(requirements, x, 245, w, 112);
        button(L"Consultar instalación del ADK", action_adk, x, 375, 290);
        button(L"1. Preparar imagen local", media_prepare, x, 435, 280);
        if (!app.media_work.empty()) label(L"Imagen preparada en:\r\n" + app.media_work.wstring(), x + 300, 434, w - 300, 64, app.small_font);
        app.usb_choices.clear(); std::vector<std::wstring> usb;
        for (const auto& d : app.session.device.disks) if (d.usb && d.identity_verified && !d.system) {
            for (const auto& v : app.session.device.volumes) if (v.disks == std::vector<unsigned>{d.number})
                for (const auto& mount : v.mounts) if (mount.size() == 3 && mount[1] == ':') {
                    app.usb_choices.emplace_back(d, wide(mount.substr(0,2)));
                    usb.push_back(wide(mount.substr(0,2)) + L" · " + text(d.model) + L" · " + bytes(d.bytes) + L" · Disco " + std::to_wstring(d.number));
                }
        }
        label(L"USB DE DESTINO · Se volverá a verificar antes de escribir", x, 518, w, 28, app.small_font);
        if (usb.empty()) label(L"No hay un USB verificable. Conéctalo y utiliza Detectar equipo.", x, 555, w, 50);
        else {
            app.selected_usb = std::min(app.selected_usb, int(usb.size()) - 1);
            combo(media_target, x, 552, w, usb, app.selected_usb);
        }
        label(L"UEFI CA 2023 · Comprueba la compatibilidad del firmware.\r\nEl asistente no desactiva Secure Boot ni modifica las claves del firmware.", x, 609, w, 56, app.small_font);
        auto write = button(L"2. Revisar y grabar USB…", media_write, x, bottom - 50, 285, 42);
        EnableWindow(write, !usb.empty() && !app.media_work.empty() && !app.demo);
        break;
    }
    case report: {
        label(L"Marca acceso confirmado únicamente después de comprobarlo en la laptop. Completar una guía no demuestra que el acceso haya sido recuperado.", x, 165, w, 60);
        combo(outcome_choice, x, 244, w, {L"Pendiente de comprobar en Windows", L"Acceso confirmado por mí en la laptop", L"No disponible con los medios actuales"}, int(app.session.assessment.outcome));
        std::wstring summary = L"Sesión: " + text(app.session.id.substr(0, 12)) + L"\r\nDiscos: " + std::to_wstring(app.session.device.disks.size()) +
            L"  ·  Instalaciones: " + std::to_wstring(app.session.device.installations.size()) + L"\r\n\r\n" + text(outcome_text(app.session.assessment.outcome));
        if (app.session.backup) summary += L"\r\n\r\nRespaldo: " + text(app.session.backup->status) + L"\r\nVerificado: " + bytes(app.session.backup->verified_bytes) + L"\r\nOmitidos: " + std::to_wstring(app.session.backup->skipped);
        readonly(summary, x, 305, w, 188);
        auto check = control(L"BUTTON", L"Incluir detalles técnicos (equipo, rutas, archivos e identificadores)", WS_TABSTOP | BS_AUTOCHECKBOX, x, 526, w, 44, technical_check);
        SendMessageW(check, BM_SETCHECK, app.technical ? BST_CHECKED : BST_UNCHECKED, 0);
        label(L"Por defecto, el informe JSON + HTML está anonimizado.\r\nNo incluye contraseñas, claves de recuperación ni tokens. Tú eliges dónde guardarlo y con quién compartirlo.", x, 587, w, 70, app.small_font);
        button(L"Exportar informe JSON + HTML", action_export, x, bottom - 50, 320, 42);
        break;
    }
    }
    app.status = label(app.status_text, x, height - 64, w - 162, 44, app.small_font);
    app.cancel_button = button(L"Cancelar", action_cancel, width - 178, height - 69, 142, 36);
    EnableWindow(app.cancel_button, app.busy && app.cancellable);
    app.progress = control(PROGRESS_CLASSW, L"", PBS_MARQUEE, x, height - 14, w, 5);
    if (app.busy) {
        SendMessageW(app.progress, PBM_SETMARQUEE, TRUE, 35);
        for (auto child : app.children) if (GetClassLongPtrW(child, GCW_ATOM) && child != app.cancel_button && child != app.progress && child != app.status) EnableWindow(child, FALSE);
    }
    if (app.demo) for (int id : {action_detect, action_admin, action_backup, action_unlock, media_prepare, media_write}) EnableWindow(GetDlgItem(app.window, id), FALSE);
    if (auto focus = GetDlgItem(app.window, previous_focus); focus && IsWindowEnabled(focus)) SetFocus(focus);
    InvalidateRect(app.window, nullptr, TRUE);
}

struct Secret { std::wstring value; ~Secret() { if (!value.empty()) SecureZeroMemory(value.data(), value.size() * sizeof(wchar_t)); } };
struct KeyPrompt { std::shared_ptr<Secret> secret; bool accepted{}; HWND edit{}; };
LRESULT CALLBACK key_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* prompt = reinterpret_cast<KeyPrompt*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_CREATE) {
        prompt = reinterpret_cast<KeyPrompt*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(prompt));
        auto message_control = CreateWindowW(L"STATIC", L"Introduce la clave BitLocker de 48 dígitos correspondiente al volumen.\nSe usa en memoria y no se añade al informe.", WS_CHILD | WS_VISIBLE, px(24), px(24), px(550), px(60), window, nullptr, app.instance, nullptr);
        set_font(message_control, app.regular);
        prompt->edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_PASSWORD | ES_AUTOHSCROLL, px(24), px(102), px(550), px(38), window, reinterpret_cast<HMENU>(10), app.instance, nullptr);
        set_font(prompt->edit, app.regular); SendMessageW(prompt->edit, EM_SETLIMITTEXT, 64, 0);
        auto ok = CreateWindowW(L"BUTTON", L"Desbloquear", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, px(250), px(169), px(150), px(38), window, reinterpret_cast<HMENU>(IDOK), app.instance, nullptr);
        auto cancel = CreateWindowW(L"BUTTON", L"Cancelar", WS_CHILD | WS_VISIBLE | WS_TABSTOP, px(420), px(169), px(150), px(38), window, reinterpret_cast<HMENU>(IDCANCEL), app.instance, nullptr);
        set_font(ok, app.regular); set_font(cancel, app.regular); return 0;
    }
    if (message == WM_COMMAND && LOWORD(wparam) == IDOK && prompt) {
        prompt->secret = std::make_shared<Secret>(); prompt->secret->value.resize(65);
        const auto length = GetWindowTextW(prompt->edit, prompt->secret->value.data(), 65); prompt->secret->value.resize(length);
        SetWindowTextW(prompt->edit, L"");
        if (!valid_recovery_key(prompt->secret->value)) {
            prompt->secret.reset(); MessageBoxW(window, L"Revisa los 48 dígitos de la clave de recuperación.", L"Formato de clave", MB_OK); SetFocus(prompt->edit); return 0;
        }
        prompt->accepted = true; DestroyWindow(window); return 0;
    }
    if (message == WM_CLOSE || (message == WM_COMMAND && LOWORD(wparam) == IDCANCEL)) { DestroyWindow(window); return 0; }
    return DefWindowProcW(window, message, wparam, lparam);
}
void prompt_unlock() {
    if (app.locked_volumes.empty() || app.demo) return;
    KeyPrompt prompt; const auto volume = app.locked_volumes.at(app.selected_locked);
    RECT parent{}; GetWindowRect(app.window, &parent);
    const auto dialog = CreateWindowExW(WS_EX_DLGMODALFRAME, L"RecoveryDeskKey", L"Desbloqueo legítimo de BitLocker", WS_CAPTION | WS_SYSMENU | WS_POPUP,
        parent.left + px(180), parent.top + px(170), px(610), px(270), app.window, nullptr, app.instance, &prompt);
    if (!dialog) return;
    EnableWindow(app.window, FALSE); ShowWindow(dialog, SW_SHOW); SetFocus(prompt.edit);
    MSG message{};
    while (IsWindow(dialog) && GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(dialog, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    EnableWindow(app.window, TRUE); SetForegroundWindow(app.window);
    if (prompt.accepted) begin("unlock", false, [volume, secret = prompt.secret] {
        Completion c; unlock_volume(volume, secret->value); c.snapshot = discover(app.cancel, progress_sink()); return c;
    });
}
void open_official() {
    const auto guide = std::find_if(app.catalogue.begin(), app.catalogue.end(), [](const auto& g) { return g.id == app.session.assessment.guide_id; });
    if (guide == app.catalogue.end()) return;
    if (reinterpret_cast<INT_PTR>(ShellExecuteW(app.window, L"open", wide(guide->source).c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32)
        error_dialog("No hay un navegador disponible en este entorno.", "Copia el enlace que aparece en la pantalla y ábrelo desde otra PC o teléfono.");
}
void write_usb() {
    if (app.usb_choices.empty() || app.media_work.empty() || app.demo) return;
    const auto [disk, drive] = app.usb_choices.at(app.selected_usb);
    std::wstring detail = L"Se borrará la partición " + drive + L" del dispositivo:\n\n" + text(disk.model) + L"\nDisco " + std::to_wstring(disk.number)
        + L" · " + bytes(disk.bytes) + L"\nIdentidad: " + text(disk.id) + L"\n\nContenido visible (máximo 8 elementos):\n";
    try {
        unsigned count = 0;
        for (const auto& entry : fs::directory_iterator(drive + L"\\")) {
            if (count++ == 8) { detail += L"…\n"; break; }
            auto name = entry.path().filename().wstring(); if (name.size() > 80) name.resize(80);
            detail += L"• " + name + L"\n";
        }
    } catch (...) { error_dialog("No se pudo revisar el contenido del USB.", "Actualiza la detección y comprueba que la unidad esté accesible."); return; }
    detail += L"\nConfirma que es un USB dedicado y que ya protegiste todo su contenido. No desconectes unidades durante la escritura.";
    TASKDIALOG_BUTTON buttons[]{{1, L"Borrar este USB y grabar WinPE"}, {2, L"Cancelar"}};
    TASKDIALOGCONFIG config{}; config.cbSize = sizeof(config); config.hwndParent = app.window; config.dwFlags = TDF_SIZE_TO_CONTENT;
    config.pszWindowTitle = L"RecoveryDesk · Confirmar dispositivo"; config.pszMainIcon = TD_WARNING_ICON;
    config.pszMainInstruction = L"Esta operación borra datos del USB seleccionado"; config.pszContent = detail.c_str();
    config.cButtons = 2; config.pButtons = buttons; config.nDefaultButton = 2;
    int chosen{};
    if (FAILED(TaskDialogIndirect(&config, &chosen, nullptr, nullptr)) || chosen != 1) return;
    const auto work = app.media_work;
    std::vector<fs::path> protect;
    if (!app.source.empty()) protect.push_back(app.source);
    if (!app.destination.empty()) protect.push_back(app.destination);
    begin("media_write", false, [disk, drive, work, protect] { Completion c; write_media(work, disk, drive, progress_sink(), app.cancel, protect); return c; });
}
void on_command(int id, int notification) {
    if (app.rendering) return;
    if (id == action_cancel) { app.cancel = true; app.status_text = L"Cancelación solicitada · Esperando un punto seguro"; SetWindowTextW(app.status, app.status_text.c_str()); return; }
    if (app.busy) return;
    if (id >= nav_home && id <= nav_report) {
        if (notification == BN_CLICKED) { app.page = static_cast<Page>(id - nav_home); app.scroll_y = 0; render(); }
        return;
    }
    switch (id) {
    case action_signin: app.page = guides; render(); break;
    case action_files: app.page = backup; render(); break;
    case action_diagnostics: app.page = detection; render(); if (app.session.device.captured_at.empty()) detect(); break;
    case action_detect: detect(); break;
    case action_technical_view: app.diagnostic_technical = !app.diagnostic_technical; render(); break;
    case action_admin: {
        if (app.demo) break;
        const auto exe = executable_directory() / L"RecoveryDesk.exe";
        if (reinterpret_cast<INT_PTR>(ShellExecuteW(app.window, L"runas", exe.c_str(), nullptr, executable_directory().c_str(), SW_SHOWNORMAL)) <= 32)
            error_dialog("No se concedió la elevación o no se pudo abrir la aplicación.", "Puedes seguir consultando las guías con permisos normales.");
        break;
    }
    case installation_choice:
        if (notification == CBN_SELCHANGE) { app.selected_installation = int(SendDlgItemMessageW(app.window, installation_choice, CB_GETCURSEL, 0, 0)); app.session.installation_id = app.session.device.installations.at(app.selected_installation).id; render(); }
        break;
    case account_choice:
        if (notification == CBN_SELCHANGE) { app.account = static_cast<Account>(SendDlgItemMessageW(app.window, account_choice, CB_GETCURSEL, 0, 0)); app.session.assessment = assess(app.account); PostMessageW(app.window, render_message, 0, 0); }
        break;
    case action_official: open_official(); break;
    case volume_choice: if (notification == CBN_SELCHANGE) app.selected_locked = int(SendDlgItemMessageW(app.window, volume_choice, CB_GETCURSEL, 0, 0)); break;
    case action_unlock: prompt_unlock(); break;
    case source_choose: { const auto value = folder(L"Selecciona la carpeta de origen (no se modificará)"); if (!value.empty()) app.source = value; render(); break; }
    case destination_choose: { const auto value = folder(L"Selecciona una carpeta vacía o un respaldo previo, en otro disco físico"); if (!value.empty()) app.destination = value; render(); break; }
    case action_backup: {
        if (app.demo) break;
        if (app.source.empty() || app.destination.empty()) { error_dialog("Selecciona el origen y el destino del respaldo."); break; }
        const auto from = app.source, to = app.destination;
        begin("backup", true, [from, to] { Completion c; c.backup_job = run_backup(from, to, storage_identity, app.cancel, progress_sink()); return c; }); break;
    }
    case action_adk:
        ShellExecuteW(app.window, L"open", L"https://learn.microsoft.com/en-us/windows-hardware/get-started/adk-install", nullptr, nullptr, SW_SHOWNORMAL); break;
    case media_target: if (notification == CBN_SELCHANGE) app.selected_usb = int(SendDlgItemMessageW(app.window, media_target, CB_GETCURSEL, 0, 0)); break;
    case media_prepare: {
        if (app.demo) break;
        const auto p = media_preflight();
        if (!p.ready) { error_dialog(p.issues.front().message, p.issues.front().next_step); break; }
        const auto parent = folder(L"Elige dónde crear una nueva carpeta de trabajo WinPE (varios GB libres)"); if (parent.empty()) break;
        const auto work = fs::path(parent) / (L"RecoveryDesk-WinPE-" + wide(new_id().substr(0, 12)));
        begin("media_prepare", false, [work] { Completion c; build_media(executable_directory(), work, progress_sink(), app.cancel); c.directory = work; return c; }); break;
    }
    case media_write: write_usb(); break;
    case outcome_choice:
        if (notification == CBN_SELCHANGE) { app.session.assessment.outcome = static_cast<Outcome>(SendDlgItemMessageW(app.window, outcome_choice, CB_GETCURSEL, 0, 0)); app.session.events.push_back("outcome_selected_by_user"); } break;
    case technical_check: app.technical = SendDlgItemMessageW(app.window, technical_check, BM_GETCHECK, 0, 0) == BST_CHECKED; break;
    case action_export: {
        const auto directory = folder(L"Selecciona dónde guardar el informe JSON y HTML"); if (directory.empty()) break;
        const auto session = app.session; const bool technical = app.technical;
        begin("report", false, [session, directory, technical] { Completion c; export_reports(session, directory, technical); c.directory = directory; return c; }); break;
    }
    }
}
LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    try {
        switch (message) {
        case WM_CREATE: app.window = window; app.dpi = int(GetDpiForWindow(window)); init_fonts(); return 0;
        case WM_SIZE:
            if (wparam != SIZE_MINIMIZED && (app.client_width != LOWORD(lparam) || app.client_height != HIWORD(lparam))) {
                app.client_width = LOWORD(lparam); app.client_height = HIWORD(lparam); render();
            }
            return 0;
        case render_message: render(); return 0;
        case WM_GETMINMAXINFO: {
            RECT work{}; SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
            auto* min = reinterpret_cast<MINMAXINFO*>(lparam);
            min->ptMinTrackSize = {std::min(px(980), int(work.right-work.left)), std::min(px(600), int(work.bottom-work.top))}; return 0;
        }
        case WM_VSCROLL: {
            SCROLLINFO info{}; info.cbSize = sizeof(info); info.fMask = SIF_ALL; GetScrollInfo(window, SB_VERT, &info);
            int next = app.scroll_y;
            switch (LOWORD(wparam)) {
                case SB_LINEUP: next -= 36; break; case SB_LINEDOWN: next += 36; break;
                case SB_PAGEUP: next -= int(info.nPage) - 40; break; case SB_PAGEDOWN: next += int(info.nPage) - 40; break;
                case SB_THUMBTRACK: next = info.nTrackPos; break; case SB_TOP: next = 0; break; case SB_BOTTOM: next = app.scroll_max; break;
            }
            next = std::clamp(next, 0, app.scroll_max);
            ScrollWindowEx(window, 0, px(app.scroll_y - next), nullptr, nullptr, nullptr, nullptr, SW_SCROLLCHILDREN | SW_INVALIDATE);
            app.scroll_y = next; SetScrollPos(window, SB_VERT, next, TRUE); return 0;
        }
        case WM_MOUSEWHEEL:
            if (app.scroll_max) { SendMessageW(window, WM_VSCROLL, GET_WHEEL_DELTA_WPARAM(wparam) > 0 ? SB_LINEUP : SB_LINEDOWN, 0); return 0; } break;
        case WM_DPICHANGED: {
            app.dpi = HIWORD(wparam); clear_controls(); init_fonts(); const auto* r = reinterpret_cast<RECT*>(lparam);
            SetWindowPos(window, nullptr, r->left, r->top, r->right-r->left, r->bottom-r->top, SWP_NOZORDER | SWP_NOACTIVATE); render(); return 0;
        }
        case WM_SETTINGCHANGE: clear_controls(); init_fonts(); render(); return 0;
        case WM_CTLCOLORSTATIC: {
            const auto dc = reinterpret_cast<HDC>(wparam); SetTextColor(dc, foreground()); SetBkMode(dc, TRANSPARENT);
            RECT r{}; GetWindowRect(reinterpret_cast<HWND>(lparam), &r); POINT point{r.left, r.top}; ScreenToClient(window, &point);
            return reinterpret_cast<LRESULT>(point.x < px(250) ? app.sidebar : app.background);
        }
        case WM_CTLCOLOREDIT: SetTextColor(reinterpret_cast<HDC>(wparam), foreground()); SetBkColor(reinterpret_cast<HDC>(wparam), surface()); return reinterpret_cast<LRESULT>(app.background);
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps{}; const auto dc = BeginPaint(window, &ps); RECT r{}; GetClientRect(window, &r); FillRect(dc, &r, app.background);
            RECT rail = r; rail.right = px(250); FillRect(dc, &rail, app.sidebar);
            auto pen = CreatePen(PS_SOLID, 1, app.high_contrast ? GetSysColor(COLOR_WINDOWTEXT) : RGB(218,226,239)); const auto old = SelectObject(dc, pen);
            MoveToEx(dc, px(250), 0, nullptr); LineTo(dc, px(250), r.bottom); SelectObject(dc, old); DeleteObject(pen); EndPaint(window, &ps); return 0;
        }
        case WM_COMMAND: on_command(LOWORD(wparam), HIWORD(wparam)); return 0;
        case progress_message: {
            std::unique_ptr<Progress> p(reinterpret_cast<Progress*>(lparam));
            if (app.busy) {
                const auto value = text(p->stage) + (p->total ? L" · " + std::to_wstring(p->completed) + L" / " + std::to_wstring(p->total) : L"");
                SetWindowTextW(app.status, value.c_str());
                if (p->total) { SendMessageW(app.progress, PBM_SETMARQUEE, FALSE, 0); SetWindowLongPtrW(app.progress, GWL_STYLE, GetWindowLongPtrW(app.progress, GWL_STYLE) & ~PBS_MARQUEE); SendMessageW(app.progress, PBM_SETRANGE32, 0, 100); SendMessageW(app.progress, PBM_SETPOS, WPARAM(p->completed * 100 / p->total), 0); }
            }
            return 0;
        }
        case finished_message: {
            std::unique_ptr<Completion> c(reinterpret_cast<Completion*>(lparam)); if (app.worker.joinable()) app.worker.join();
            app.busy = false;
            if (c->snapshot) {
                const auto previous_id = app.session.installation_id;
                app.session.device = std::move(*c->snapshot); app.selected_installation = 0;
                for (std::size_t i = 0; i < app.session.device.installations.size(); ++i) if (app.session.device.installations[i].id == previous_id) app.selected_installation = int(i);
                app.session.installation_id = app.session.device.installations.empty() ? "" : app.session.device.installations[app.selected_installation].id;
            }
            if (c->backup_job) { app.session.backup = std::move(c->backup_job); app.page = report; }
            if (c->operation == "media_prepare" && c->error.empty()) app.media_work = c->directory;
            const bool cancelled = (app.cancel && c->operation == "discovery") || (app.session.backup && c->operation == "backup" && app.session.backup->status == "cancelled");
            app.session.events.push_back(c->operation + (cancelled ? "_cancelled" : c->error.empty() ? "_completed" : "_failed"));
            app.status_text = c->error.empty() ? L"Operación terminada · Revisa el resultado" : L"La operación no se completó · Revisa el siguiente paso";
            if (app.cancel && c->operation == "discovery") app.status_text = L"Detección cancelada · El inventario puede estar incompleto";
            if (app.closing) { DestroyWindow(window); return 0; }
            render();
            if (!c->error.empty()) error_dialog(c->error, c->next_step);
            else if (c->operation == "media_write") MessageBoxW(window, L"Los archivos del USB se verificaron. Falta comprobar el arranque en el equipo de destino con Secure Boot.", L"USB preparado · Arranque pendiente de validar", MB_OK | MB_ICONINFORMATION);
            else if (c->operation == "report") MessageBoxW(window, L"Se guardaron los informes JSON y HTML en la carpeta elegida.", L"Informe exportado", MB_OK | MB_ICONINFORMATION);
            return 0;
        }
        case WM_CLOSE:
            if (app.busy) { app.closing = true; if (app.cancellable) app.cancel = true; SetWindowTextW(app.status, L"Cerrando cuando la operación alcance un punto seguro…"); return 0; }
            DestroyWindow(window); return 0;
        case WM_DESTROY: clear_controls(); PostQuitMessage(0); return 0;
        }
    } catch (const Failure& e) { error_dialog(e.what(), e.next_step); }
      catch (const std::exception&) { error_dialog("No se pudo completar esta acción.", "Comprueba los datos y vuelve a intentarlo."); }
    return DefWindowProcW(window, message, wparam, lparam);
}
void seed_demo() {
    app.session.device.machine = "LAPTOP-DEMO"; app.session.device.captured_at = utc_now();
    app.session.device.complete = true;
    app.session.device.disks.push_back({0,"demo-ssd","SSD de ejemplo 512 GB","SIMULADO","NVMe",512ULL*1024*1024*1024,false,true,true,true});
    app.session.device.disks.push_back({1,"demo-usb","USB de ejemplo 16 GB","SIMULADO","USB",16ULL*1024*1024*1024,true,false,true,false});
    Volume v; v.id = "demo-volume"; v.mounts = {"W:\\"}; v.filesystem = "NTFS"; v.disks = {0}; v.accessible = true; v.locked = false; v.encrypted = true;
    app.session.device.volumes.push_back(v);
    app.session.device.installations.push_back({"demo-installation", v.id, "W:\\Windows", false});
    app.session.installation_id = "demo-installation"; app.session.events.push_back("simulation_only");
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    app.instance = instance;
    app.dpi = int(GetDpiForSystem());
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32 | LOAD_LIBRARY_SEARCH_APPLICATION_DIR);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    CoInitializeSecurity(nullptr, -1, nullptr, nullptr, RPC_C_AUTHN_LEVEL_PKT_PRIVACY, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE, nullptr);
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS}; InitCommonControlsEx(&controls);
    app.session.id = new_id(); app.session.started_at = utc_now(); app.session.assessment = assess(Account::unknown);
    bool boot_check = false;
    int argc{}; auto args = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; i < argc; ++i) if (std::wstring(args[i]) == L"--demo") app.demo = true;
    for (int i = 1; i < argc; ++i) if (std::wstring(args[i]) == L"--boot-check") boot_check = true;
    LocalFree(args);
    app.session.device.winpe = is_winpe(); app.session.device.elevated = is_elevated();
    if (app.demo) seed_demo();
    try { app.catalogue = load_guides(executable_directory() / L"data\\guides.es.json"); }
    catch (const std::exception&) { MessageBoxW(nullptr, L"No se pudo validar data/guides.es.json. Restaura el catálogo original antes de abrir RecoveryDesk.", L"RecoveryDesk", MB_OK | MB_ICONERROR); if (SUCCEEDED(com)) CoUninitialize(); return 1; }
    WNDCLASSEXW klass{}; klass.cbSize = sizeof(klass); klass.hInstance = instance; klass.lpfnWndProc = window_proc;
    klass.lpszClassName = L"RecoveryDeskMain"; klass.hCursor = LoadCursorW(nullptr, IDC_ARROW); klass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    RegisterClassExW(&klass);
    WNDCLASSEXW key_class = klass; key_class.lpszClassName = L"RecoveryDeskKey"; key_class.lpfnWndProc = key_proc; key_class.hbrBackground = GetSysColorBrush(COLOR_WINDOW); RegisterClassExW(&key_class);
    RECT work_area{}; SystemParametersInfoW(SPI_GETWORKAREA, 0, &work_area, 0);
    app.window = CreateWindowExW(WS_EX_CONTROLPARENT, klass.lpszClassName, L"RecoveryDesk · Recuperación y diagnóstico", WS_OVERLAPPEDWINDOW | WS_VSCROLL,
        CW_USEDEFAULT, CW_USEDEFAULT, std::min(px(1240), int(work_area.right-work_area.left)), std::min(px(880), int(work_area.bottom-work_area.top)), nullptr, nullptr, instance, nullptr);
    if (!app.window) { if (SUCCEEDED(com)) CoUninitialize(); return 2; }
    ShowWindow(app.window, show); UpdateWindow(app.window);
    if (boot_check) {
        try { write_boot_evidence(executable_directory() / L"boot-evidence.json"); }
        catch (...) { app.status_text = L"La comprobación de arranque no pudo completarse; no se declara validada."; render(); }
    }
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(app.window, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    if (app.worker.joinable()) app.worker.join();
    for (auto font : {app.regular, app.small_font, app.heading, app.title, app.brand}) if (font) DeleteObject(font);
    for (auto brush : {app.background, app.white, app.sidebar}) if (brush) DeleteObject(brush);
    if (SUCCEEDED(com)) CoUninitialize(); return int(message.wParam);
}
