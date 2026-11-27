#include "gui_common.hpp"
#include "gui_config.hpp"
#include "gui_dialogs.hpp"
#include "gui_toolbar.hpp"
#include "toolbar_icons.hpp"

#include "openrar/archive_manager.hpp"

#include <uxtheme.h>

#include <algorithm>
#include <iomanip>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace openrar;
using namespace openrar::gui;

namespace {

struct ListItemData {
    std::string name;
    std::string full_path;
    std::wstring type_label;
    bool is_parent = false;
    bool is_dir = false;
    bool is_archive = false;
    uint64_t size = 0;
    uint64_t packed = 0;
    int64_t modified = 0;
    uint32_t crc32 = 0;
    int icon_index = 0;
};

struct AppState {
    HWND hwnd = nullptr;
    HWND toolbar = nullptr;
    HWND btn_up = nullptr;
    HWND combo_path = nullptr;
    HWND listview = nullptr;
    HWND statusbar = nullptr;
    HMENU fav_menu = nullptr;
    HFONT font = nullptr;
    HICON app_icon_big = nullptr;
    HICON app_icon_small = nullptr;
    HIMAGELIST img_list = nullptr;
    int icon_folder = 0;
    int icon_archive = 0;
    int icon_file = 0;

    bool in_archive = false;
    fs::path current_dir;
    fs::path current_archive;
    std::string archive_subdir;
    std::string default_password;
    ArchiveInfo current_archive_info;
    std::vector<EntryInfo> archive_entries;
    std::vector<ListItemData> items;
    std::vector<fs::path> favorites;
    int sort_col = 0;
    bool sort_asc = true;
};

AppState g_app;
WNDPROC g_orig_combo_edit_proc = nullptr;

// ============================================================================
// Status Bar, Icons, Address Bar, & ListView Rendering
// ============================================================================
void update_status_bar() {
    uint64_t total_bytes = 0, sel_bytes = 0;
    size_t total_files = 0, sel_files = 0;
    for (const auto& it : g_app.items) {
        if (!it.is_parent && !it.is_dir) { total_files++; total_bytes += it.size; }
    }
    int idx = -1;
    while ((idx = ListView_GetNextItem(g_app.listview, idx, LVNI_SELECTED)) != -1) {
        if (idx >= 0 && static_cast<size_t>(idx) < g_app.items.size()) {
            const auto& it = g_app.items[idx];
            if (!it.is_parent && !it.is_dir) { sel_files++; sel_bytes += it.size; }
        }
    }
    std::wstring right = sel_files > 0
        ? (tr_w(i18n::StrKey::StatusSelected) + L" " + format_commas(sel_bytes) + L" " + tr_w(i18n::StrKey::StatusBytesIn) + L" " + std::to_wstring(sel_files) + L" " + tr_w(sel_files == 1 ? i18n::StrKey::StatusFile : i18n::StrKey::StatusFiles))
        : (tr_w(i18n::StrKey::StatusTotal) + L" " + format_commas(total_bytes) + L" " + tr_w(i18n::StrKey::StatusBytesIn) + L" " + std::to_wstring(total_files) + L" " + tr_w(total_files == 1 ? i18n::StrKey::StatusFile : i18n::StrKey::StatusFiles));
    std::wstring left = g_app.default_password.empty() ? L"  \xD83D\xDD11" : (L"  \xD83D\xDD12 " + tr_w(i18n::StrKey::StatusPasswordSet));
    SendMessageW(g_app.statusbar, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(left.c_str()));
    SendMessageW(g_app.statusbar, SB_SETTEXTW, 1, reinterpret_cast<LPARAM>(right.c_str()));
}

int get_file_icon_index(const fs::path& p, bool is_dir, bool is_archive, std::wstring& out_type) {
    if (is_archive) {
        out_type = utf8_to_wide(format_name(detect_format_from_path(p)) + " archive");
        return g_app.icon_archive;
    }
    SHFILEINFOW sfi{};
    DWORD flags = SHGFI_ICON | SHGFI_SMALLICON | SHGFI_TYPENAME | SHGFI_USEFILEATTRIBUTES;
    DWORD attr = is_dir ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
    if (SHGetFileInfoW(p.c_str(), attr, &sfi, sizeof(sfi), flags)) {
        out_type = sfi.szTypeName[0] ? sfi.szTypeName : (is_dir ? L"File folder" : L"File");
        if (sfi.hIcon) {
            int idx = ImageList_AddIcon(g_app.img_list, sfi.hIcon);
            DestroyIcon(sfi.hIcon);
            return idx;
        }
    }
    out_type = is_dir ? L"File folder" : L"File";
    return is_dir ? g_app.icon_folder : g_app.icon_file;
}

void refresh_address_dropdown(const fs::path& active) {
    SendMessageW(g_app.combo_path, CB_RESETCONTENT, 0, 0);
    std::wstring cur_str = active.wstring();
    SendMessageW(g_app.combo_path, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(cur_str.c_str()));
    DWORD mask = GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if (mask & (1u << i)) {
            std::wstring drv = std::wstring(1, static_cast<wchar_t>(L'A' + i)) + L":\\";
            if (drv != cur_str) SendMessageW(g_app.combo_path, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(drv.c_str()));
        }
    }
    SetWindowTextW(g_app.combo_path, cur_str.c_str());
}

void sort_current_items() {
    if (g_app.items.size() <= 2) return;
    auto begin_it = g_app.items[0].is_parent ? g_app.items.begin() + 1 : g_app.items.begin();
    std::stable_sort(begin_it, g_app.items.end(), [](const ListItemData& a, const ListItemData& b) {
        if (a.is_dir != b.is_dir) return a.is_dir > b.is_dir;
        int cmp = 0;
        switch (g_app.sort_col) {
            case 1: cmp = (a.size < b.size) ? -1 : (a.size > b.size ? 1 : 0); break;
            case 2: cmp = a.type_label.compare(b.type_label); break;
            case 3: cmp = (a.modified < b.modified) ? -1 : (a.modified > b.modified ? 1 : 0); break;
            default: cmp = _stricmp(a.name.c_str(), b.name.c_str()); break;
        }
        return g_app.sort_asc ? (cmp < 0) : (cmp > 0);
    });
}

void render_items_to_listview() {
    ListView_DeleteAllItems(g_app.listview);
    for (size_t i = 0; i < g_app.items.size(); ++i) {
        const auto& it = g_app.items[i];
        std::wstring wname = utf8_to_wide(it.name);
        LVITEMW lvi{};
        lvi.mask = LVIF_TEXT | LVIF_IMAGE;
        lvi.iItem = static_cast<int>(i);
        lvi.pszText = wname.data();
        lvi.iImage = it.icon_index;
        ListView_InsertItem(g_app.listview, &lvi);

        if (!it.is_dir && !it.is_parent) {
            auto wsz = format_commas(it.size);
            ListView_SetItemText(g_app.listview, static_cast<int>(i), 1, wsz.data());
        }
        std::wstring wtype = it.type_label;
        ListView_SetItemText(g_app.listview, static_cast<int>(i), 2, wtype.data());

        if (!it.is_parent) {
            auto wmod = format_short_date(it.modified);
            ListView_SetItemText(g_app.listview, static_cast<int>(i), 3, wmod.data());
        }
        if (g_app.in_archive && !it.is_dir && !it.is_parent) {
            auto wpk = format_commas(it.packed);
            ListView_SetItemText(g_app.listview, static_cast<int>(i), 4, wpk.data());
            if (it.crc32 != 0) {
                std::ostringstream crc;
                crc << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << it.crc32;
                auto wcrc = utf8_to_wide(crc.str());
                ListView_SetItemText(g_app.listview, static_cast<int>(i), 5, wcrc.data());
            }
        }
    }
    if (!g_app.items.empty()) {
        ListView_SetItemState(g_app.listview, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    }
    update_status_bar();
}

// ============================================================================
// Filesystem & Hierarchical Archive Navigation
// ============================================================================
void populate_listview_filesystem(const fs::path& dir) {
    g_app.in_archive = false;
    g_app.current_dir = dir;
    g_app.current_archive.clear();
    g_app.archive_subdir.clear();
    g_app.items.clear();

    std::wstring title = dir.filename().empty() ? dir.wstring() : dir.filename().wstring();
    SetWindowTextW(g_app.hwnd, title.c_str());
    refresh_address_dropdown(dir);

    if (dir.has_parent_path() && dir.parent_path() != dir) {
        g_app.items.push_back({"..", path_to_utf8(dir.parent_path()), tr_w(i18n::StrKey::TypeSystemFolder), true, true, false, 0, 0, 0, 0, g_app.icon_folder});
    }

    std::error_code ec;
    for (const auto& it : fs::directory_iterator(dir, fs::directory_options::skip_permission_denied, ec)) {
        ListItemData item;
        item.name = path_to_utf8(it.path().filename());
        item.full_path = path_to_utf8(it.path());
        item.is_dir = it.is_directory(ec);
        item.is_archive = !item.is_dir && is_archive_path(it.path());
        if (!item.is_dir) { item.size = it.file_size(ec); item.packed = item.size; }
        item.modified = file_time_to_unix(it.last_write_time(ec));
        item.icon_index = get_file_icon_index(it.path(), item.is_dir, item.is_archive, item.type_label);
        g_app.items.push_back(std::move(item));
    }

    sort_current_items();
    render_items_to_listview();
}

void render_archive_subdir(const std::string& subdir) {
    g_app.archive_subdir = normalize_entry_path(subdir);
    g_app.items.clear();

    std::wstring addr = g_app.current_archive.wstring();
    if (!g_app.archive_subdir.empty()) {
        std::string sub_win = g_app.archive_subdir;
        std::replace(sub_win.begin(), sub_win.end(), '/', '\\');
        addr += L"\\" + utf8_to_wide(sub_win);
    }
    SetWindowTextW(g_app.hwnd, (g_app.current_archive.filename().wstring() + L" - OpenRAR").c_str());
    refresh_address_dropdown(fs::path(addr));

    g_app.items.push_back({"..", "", tr_w(i18n::StrKey::TypeSystemFolder), true, true, false, 0, 0, 0, 0, g_app.icon_folder});

    std::string prefix = g_app.archive_subdir.empty() ? "" : (g_app.archive_subdir + "/");
    std::set<std::string> seen_dirs;

    for (const auto& e : g_app.archive_entries) {
        std::string norm = normalize_entry_path(e.full_path);
        if (norm.empty()) continue;
        if (!prefix.empty()) {
            if (!norm.starts_with(prefix)) continue;
            norm = norm.substr(prefix.size());
        }
        if (norm.empty()) continue;

        auto slash = norm.find('/');
        if (slash != std::string::npos || e.is_directory) {
            std::string dir_name = (slash != std::string::npos) ? norm.substr(0, slash) : norm;
            std::string full_dir = prefix + dir_name;
            if (seen_dirs.insert(full_dir).second) {
                g_app.items.push_back({dir_name, full_dir, tr_w(i18n::StrKey::TypeFolder), false, true, false, 0, 0, e.modified_time, 0, g_app.icon_folder});
            }
        } else {
            std::wstring dummy;
            int icon = get_file_icon_index(fs::path(e.name), false, false, dummy);
            g_app.items.push_back({
                norm, e.full_path, utf8_to_wide(e.method + (e.is_encrypted ? " *" : "")),
                false, false, false, e.size, e.compressed_size, e.modified_time, e.crc32, icon
            });
        }
    }

    sort_current_items();
    render_items_to_listview();
}

void populate_listview_archive(const fs::path& archive_path) {
    std::vector<EntryInfo> entries;
    ArchiveInfo info;
    std::string err;
    if (!ArchiveManager::list(archive_path, entries, g_app.default_password, &info, &err)) {
        std::wstring wpwd;
        if (show_input_dialog(g_app.hwnd, g_app.font, L"Enter password",
                              L"Enter password for the encrypted archive:", wpwd, true)) {
            g_app.default_password = wide_to_utf8(wpwd);
            if (!ArchiveManager::list(archive_path, entries, g_app.default_password, &info, &err)) {
                MessageBoxW(g_app.hwnd, utf8_to_wide("Failed to open archive:\n" + err).c_str(), L"OpenRAR", MB_ICONERROR);
                return;
            }
        } else {
            return;
        }
    }
    g_app.in_archive = true;
    g_app.current_archive = archive_path;
    g_app.current_archive_info = info;
    g_app.archive_entries = std::move(entries);
    render_archive_subdir("");
}

std::vector<ListItemData> get_selected_items() {
    std::vector<ListItemData> sel;
    int idx = -1;
    while ((idx = ListView_GetNextItem(g_app.listview, idx, LVNI_SELECTED)) != -1) {
        if (idx >= 0 && static_cast<size_t>(idx) < g_app.items.size() && !g_app.items[idx].is_parent)
            sel.push_back(g_app.items[idx]);
    }
    return sel;
}

void collect_selected_archive_entries(std::set<std::string>& out) {
    for (const auto& s : get_selected_items()) {
        if (s.is_dir) {
            std::string pref = normalize_entry_path(s.full_path) + "/";
            for (const auto& e : g_app.archive_entries) {
                std::string n = normalize_entry_path(e.full_path);
                if (n == normalize_entry_path(s.full_path) || n.starts_with(pref)) out.insert(n);
            }
        } else {
            out.insert(normalize_entry_path(s.full_path));
        }
    }
}

void go_up_one_level() {
    if (g_app.in_archive) {
        if (!g_app.archive_subdir.empty()) {
            auto slash = g_app.archive_subdir.rfind('/');
            render_archive_subdir(slash == std::string::npos ? "" : g_app.archive_subdir.substr(0, slash));
        } else {
            populate_listview_filesystem(g_app.current_archive.parent_path());
        }
    } else if (g_app.current_dir.has_parent_path()) {
        populate_listview_filesystem(g_app.current_dir.parent_path());
    }
}

// ============================================================================
// Commands
// ============================================================================
void cmd_add(const std::vector<fs::path>& explicit_inputs = {}) {
    std::vector<fs::path> inputs = explicit_inputs;
    if (inputs.empty()) {
        if (g_app.in_archive) {
            fs::path fp;
            if (!open_file_dialog(g_app.hwnd, L"All Files\0*.*\0", fp)) return;
            CreateOptions co; co.password = g_app.default_password;
            auto res = run_with_progress(g_app.hwnd, g_app.font, L"Adding files to archive...",
                [&](ProgressCallback cb, std::atomic<bool>* cancel) {
                    co.progress = std::move(cb); co.cancel_flag = cancel;
                    return ArchiveManager::add_entries(g_app.current_archive, {fp}, co);
                });
            MessageBoxW(g_app.hwnd, utf8_to_wide(res.message).c_str(), L"OpenRAR", res.success ? MB_ICONINFORMATION : MB_ICONERROR);
            populate_listview_archive(g_app.current_archive);
            return;
        }
        auto sel = get_selected_items();
        if (sel.empty()) { MessageBoxW(g_app.hwnd, L"Select files or folders to archive.", L"OpenRAR", MB_ICONINFORMATION); return; }
        for (const auto& s : sel) inputs.push_back(path_from_utf8(s.full_path));
    }
    fs::path parent = inputs[0].parent_path();
    std::string base_stem = (inputs.size() == 1)
        ? path_to_utf8(inputs[0].stem())
        : (parent.filename().empty() ? "Archive" : path_to_utf8(parent.filename()));
    fs::path default_out = parent / path_from_utf8(base_stem + ".orar");
    CreateOptions opts;
    if (!show_add_dialog(g_app.hwnd, g_app.font, default_out, opts)) return;
    auto res = run_with_progress(g_app.hwnd, g_app.font, L"Creating " + default_out.filename().wstring(),
        [&](ProgressCallback cb, std::atomic<bool>* cancel) {
            opts.progress = std::move(cb); opts.cancel_flag = cancel;
            return ArchiveManager::create(default_out, inputs, opts);
        });
    MessageBoxW(g_app.hwnd, utf8_to_wide(res.message).c_str(), L"OpenRAR", res.success ? MB_ICONINFORMATION : MB_ICONERROR);
    if (!g_app.in_archive) populate_listview_filesystem(g_app.current_dir);
}

void cmd_extract() {
    fs::path archive = g_app.in_archive ? g_app.current_archive : fs::path{};
    if (archive.empty()) {
        auto sel = get_selected_items();
        if (!sel.empty() && sel[0].is_archive) archive = path_from_utf8(sel[0].full_path);
        else if (!open_file_dialog(g_app.hwnd, kAllArchivesFilter, archive)) return;
    }

    ExtractOptions eo;
    eo.destination = archive.parent_path() / archive.stem();
    eo.password = g_app.default_password;
    eo.overwrite = OverwriteMode::OverwriteAll;
    eo.preserve_paths = true;
    bool open_after = true;
    if (!show_extract_dialog(g_app.hwnd, g_app.font, eo, open_after)) return;

    if (!eo.password.empty()) g_app.default_password = eo.password;
    if (g_app.in_archive) collect_selected_archive_entries(eo.selected_entries);

    auto do_extract = [&] {
        return run_with_progress(g_app.hwnd, g_app.font, L"Extracting " + archive.filename().wstring(),
            [&](ProgressCallback cb, std::atomic<bool>* cancel) {
                eo.progress = std::move(cb); eo.cancel_flag = cancel;
                return ArchiveManager::extract(archive, eo);
            });
    };

    auto res = do_extract();
    if (!res.success && res.message.find("Password") != std::string::npos) {
        std::wstring wpwd;
        if (show_input_dialog(g_app.hwnd, g_app.font, L"Enter password", L"Enter password:", wpwd, true)) {
            eo.password = wide_to_utf8(wpwd); g_app.default_password = eo.password;
            res = do_extract();
        }
    }
    if (res.success && open_after) {
        ShellExecuteW(g_app.hwnd, L"open", eo.destination.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    } else {
        std::string msg = res.message;
        for (const auto& e : res.errors) msg += "\n- " + e;
        MessageBoxW(g_app.hwnd, utf8_to_wide(msg).c_str(), L"OpenRAR Extract", res.success ? MB_ICONINFORMATION : MB_ICONERROR);
    }
    if (!g_app.in_archive) populate_listview_filesystem(g_app.current_dir);
}

void cmd_extract_quick(bool to_subfolder) {
    fs::path archive = g_app.in_archive ? g_app.current_archive : fs::path{};
    if (archive.empty()) {
        auto sel = get_selected_items();
        if (!sel.empty() && sel[0].is_archive) archive = path_from_utf8(sel[0].full_path);
        else return;
    }
    ExtractOptions eo;
    eo.destination = to_subfolder ? (archive.parent_path() / archive.stem()) : archive.parent_path();
    eo.password = g_app.default_password;
    eo.overwrite = OverwriteMode::OverwriteAll;
    if (g_app.in_archive) collect_selected_archive_entries(eo.selected_entries);

    auto res = run_with_progress(g_app.hwnd, g_app.font, L"Extracting " + archive.filename().wstring(),
        [&](ProgressCallback cb, std::atomic<bool>* cancel) {
            eo.progress = std::move(cb); eo.cancel_flag = cancel;
            return ArchiveManager::extract(archive, eo);
        });
    if (!res.success) MessageBoxW(g_app.hwnd, utf8_to_wide(res.message).c_str(), L"OpenRAR Extract", MB_ICONERROR);
    if (!g_app.in_archive) populate_listview_filesystem(g_app.current_dir);
}

void cmd_quick_archive(ArchiveFormat fmt) {
    if (g_app.in_archive) return;
    auto sel = get_selected_items();
    if (sel.empty()) return;
    std::vector<fs::path> inputs;
    for (const auto& s : sel) inputs.push_back(path_from_utf8(s.full_path));

    fs::path parent = inputs[0].parent_path();
    std::string base_stem = (inputs.size() == 1)
        ? path_to_utf8(inputs[0].stem())
        : (parent.filename().empty() ? "Archive" : path_to_utf8(parent.filename()));
    fs::path out = parent / path_from_utf8(base_stem + default_extension(fmt));
    CreateOptions co;
    co.format = fmt;
    co.level = CompressionLevel::Normal;
    if (fmt == ArchiveFormat::Orar) { co.add_recovery_record = true; co.recovery_percent = 5; }

    auto res = run_with_progress(g_app.hwnd, g_app.font, L"Creating " + out.filename().wstring(),
        [&](ProgressCallback cb, std::atomic<bool>* cancel) {
            co.progress = std::move(cb); co.cancel_flag = cancel;
            return ArchiveManager::create(out, inputs, co);
        });
    if (!res.success) MessageBoxW(g_app.hwnd, utf8_to_wide(res.message).c_str(), L"OpenRAR", MB_ICONERROR);
    populate_listview_filesystem(g_app.current_dir);
}

void cmd_test() {
    fs::path archive = g_app.in_archive ? g_app.current_archive : fs::path{};
    if (archive.empty()) {
        auto sel = get_selected_items();
        if (!sel.empty() && sel[0].is_archive) archive = path_from_utf8(sel[0].full_path);
        else { MessageBoxW(g_app.hwnd, L"Select or open an archive to test.", L"OpenRAR", MB_ICONINFORMATION); return; }
    }
    auto res = run_with_progress(g_app.hwnd, g_app.font, L"Testing " + archive.filename().wstring(),
        [&](ProgressCallback cb, std::atomic<bool>* cancel) {
            return ArchiveManager::test(archive, g_app.default_password, std::move(cb), cancel);
        });
    std::string msg = res.message;
    for (const auto& e : res.errors) msg += "\n- " + e;
    MessageBoxW(g_app.hwnd, utf8_to_wide(msg).c_str(), L"Diagnostic messages - OpenRAR", res.success ? MB_ICONINFORMATION : MB_ICONERROR);
}

void cmd_view() {
    auto sel = get_selected_items();
    if (sel.empty()) return;
    if (!g_app.in_archive) {
        ShellExecuteW(g_app.hwnd, L"open", utf8_to_wide(sel[0].full_path).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return;
    }
    std::vector<uint8_t> data; std::string err;
    if (!ArchiveManager::extract_entry_to_memory(g_app.current_archive, sel[0].full_path, g_app.default_password, data, &err)) {
        MessageBoxW(g_app.hwnd, utf8_to_wide(err).c_str(), L"OpenRAR View", MB_ICONERROR); return;
    }
    std::error_code ec;
    fs::path tmp = fs::temp_directory_path(ec) / path_from_utf8("openrar_view_" + path_to_utf8(path_from_utf8(sel[0].full_path).filename()));
    write_file_bytes(tmp, data.data(), data.size());
    ShellExecuteW(g_app.hwnd, L"open", tmp.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void cmd_delete() {
    auto sel = get_selected_items();
    if (sel.empty()) return;
    if (MessageBoxW(g_app.hwnd, tr_w(i18n::StrKey::DlgConfirmDelete).c_str(), L"OpenRAR", MB_YESNO | MB_ICONWARNING) != IDYES) return;
    if (g_app.in_archive) {
        std::set<std::string> targets;
        collect_selected_archive_entries(targets);
        auto res = ArchiveManager::delete_entries(g_app.current_archive, targets, g_app.default_password);
        MessageBoxW(g_app.hwnd, utf8_to_wide(res.message).c_str(), L"OpenRAR", res.success ? MB_ICONINFORMATION : MB_ICONERROR);
        populate_listview_archive(g_app.current_archive);
    } else {
        std::error_code ec;
        for (const auto& s : sel) fs::remove_all(path_from_utf8(s.full_path), ec);
        populate_listview_filesystem(g_app.current_dir);
    }
}

void cmd_find() {
    std::wstring pat = L"*";
    if (!show_input_dialog(g_app.hwnd, g_app.font, L"Find files", L"File names to find (e.g. *.txt, *report*):", pat)) return;
    std::wstring txt;
    show_input_dialog(g_app.hwnd, g_app.font, L"Find files", L"String to find inside files & archives (optional):", txt);
    SearchOptions so;
    so.root_path = g_app.in_archive ? g_app.current_archive : g_app.current_dir;
    so.filename_pattern = wide_to_utf8(pat);
    so.text_to_find = wide_to_utf8(txt);
    so.password = g_app.default_password;
    auto matches = ArchiveManager::find_files(so);
    std::ostringstream oss;
    oss << "Found " << matches.size() << " match(es):\n\n";
    for (size_t i = 0; i < std::min<size_t>(matches.size(), 25); ++i) {
        const auto& m = matches[i];
        if (m.is_in_archive()) oss << "[ARCHIVE] " << path_to_utf8(m.archive_path.filename()) << " -> " << m.entry_path << "\n";
        else oss << path_to_utf8(m.file_path) << "\n";
    }
    MessageBoxW(g_app.hwnd, utf8_to_wide(oss.str()).c_str(), L"Search results - OpenRAR", MB_ICONINFORMATION);
}

void cmd_wizard() {
    int choice = show_wizard_dialog(g_app.hwnd, g_app.font);
    if (choice == 1) cmd_extract();
    else if (choice == 2) {
        fs::path fp;
        if (open_file_dialog(g_app.hwnd, L"All Files\0*.*\0", fp)) cmd_add({fp});
    } else if (choice == 3) {
        fs::path arc, fp;
        if (open_file_dialog(g_app.hwnd, L"Archives (*.orar;*.zip)\0*.orar;*.zip\0", arc) &&
            open_file_dialog(g_app.hwnd, L"File to add\0*.*\0", fp)) {
            CreateOptions co; co.password = g_app.default_password;
            auto res = ArchiveManager::add_entries(arc, {fp}, co);
            MessageBoxW(g_app.hwnd, utf8_to_wide(res.message).c_str(), L"OpenRAR Wizard", res.success ? MB_ICONINFORMATION : MB_ICONERROR);
        }
    }
}

void cmd_info() {
    fs::path archive = g_app.in_archive ? g_app.current_archive : fs::path{};
    if (archive.empty()) {
        auto sel = get_selected_items();
        if (!sel.empty() && sel[0].is_archive) archive = path_from_utf8(sel[0].full_path);
        else { MessageBoxW(g_app.hwnd, L"Select or open an archive to view its info.", L"OpenRAR", MB_ICONINFORMATION); return; }
    }
    std::vector<EntryInfo> entries; ArchiveInfo info; std::string err;
    if (!ArchiveManager::list(archive, entries, g_app.default_password, &info, &err)) {
        MessageBoxW(g_app.hwnd, utf8_to_wide(err).c_str(), L"OpenRAR Info", MB_ICONERROR); return;
    }
    std::ostringstream oss;
    oss << "Archive: " << path_to_utf8(info.file_path) << "\n"
        << "Format: " << format_name(info.format) << " (" << info.compression_method << ")\n"
        << "Files / Folders: " << info.file_count << " / " << info.folder_count << "\n"
        << "Total size: " << format_bytes(info.total_uncompressed_size) << "\n"
        << "Packed size: " << format_bytes(info.archive_size) << " (" << std::fixed << std::setprecision(1) << info.overall_ratio_percent() << "%)\n"
        << "Solid: " << (info.is_solid ? "Yes" : "No") << "\n"
        << "Encrypted: " << (info.is_encrypted ? "Yes" : "No") << (info.has_encrypted_headers ? " (Headers encrypted)" : "") << "\n"
        << "Recovery record: " << (info.has_recovery_record ? ("Yes (" + std::to_string(info.recovery_percent) + "%)") : "No") << "\n";
    if (!info.comment.empty()) oss << "\nComment:\n" << info.comment;
    MessageBoxW(g_app.hwnd, utf8_to_wide(oss.str()).c_str(), L"Archive information", MB_ICONINFORMATION);
}

void cmd_repair() {
    fs::path src = g_app.in_archive ? g_app.current_archive : fs::path{};
    if (src.empty()) {
        auto sel = get_selected_items();
        if (!sel.empty() && sel[0].is_archive) src = path_from_utf8(sel[0].full_path);
        else if (!open_file_dialog(g_app.hwnd, L"Archives (*.orar;*.zip)\0*.orar;*.zip\0", src)) return;
    }
    fs::path dst = src.parent_path() / path_from_utf8("rebuilt." + path_to_utf8(src.filename()));
    auto res = ArchiveManager::repair(src, dst);
    std::string msg = res.message + (res.success ? ("\nSaved to: " + path_to_utf8(dst)) : "");
    MessageBoxW(g_app.hwnd, utf8_to_wide(msg).c_str(), L"Repairing archive", res.success ? MB_ICONINFORMATION : MB_ICONERROR);
}

void cmd_comment() {
    if (!g_app.in_archive) { MessageBoxW(g_app.hwnd, L"Open a .orar or .zip archive to edit its comment.", L"OpenRAR", MB_ICONINFORMATION); return; }
    std::wstring cmt = utf8_to_wide(g_app.current_archive_info.comment);
    if (show_input_dialog(g_app.hwnd, g_app.font, L"Archive comment", L"Enter a comment:", cmt, false, true)) {
        auto res = ArchiveManager::set_comment(g_app.current_archive, wide_to_utf8(cmt), g_app.default_password);
        MessageBoxW(g_app.hwnd, utf8_to_wide(res.message).c_str(), L"OpenRAR", res.success ? MB_ICONINFORMATION : MB_ICONERROR);
        populate_listview_archive(g_app.current_archive);
    }
}

void cmd_sfx() {
    fs::path arc = g_app.in_archive ? g_app.current_archive : fs::path{};
    if (arc.empty()) {
        auto sel = get_selected_items();
        if (!sel.empty() && sel[0].is_archive) arc = path_from_utf8(sel[0].full_path);
        else if (!open_file_dialog(g_app.hwnd, L"Archives (*.orar;*.zip)\0*.orar;*.zip\0", arc)) return;
    }
    fs::path stub = get_exe_path().parent_path() / "openrar_sfx_stub.exe";
    fs::path out = arc.parent_path() / path_from_utf8(path_to_utf8(arc.stem()) + ".exe");
    auto res = ArchiveManager::create_sfx(arc, stub, out);
    MessageBoxW(g_app.hwnd, utf8_to_wide(res.message).c_str(), L"Create SFX archive", res.success ? MB_ICONINFORMATION : MB_ICONERROR);
}

void cmd_benchmark() {
    auto bm = ArchiveManager::run_benchmark(0, 2.0);
    std::ostringstream oss;
    oss << "Benchmark and hardware test\n\n"
        << "Multithreading: " << bm.threads << " threads\n"
        << "Elapsed: " << std::fixed << std::setprecision(2) << bm.duration_seconds << " s\n"
        << "Processed: " << format_bytes(bm.bytes_processed) << "\n"
        << "Resulting speed: " << std::setprecision(1) << bm.speed_kbps << " KB/s (" << std::setprecision(2) << bm.speed_mbps << " MB/s)\n"
        << "Compression ratio: " << std::setprecision(1) << bm.compression_ratio << "%\n";
    MessageBoxW(g_app.hwnd, utf8_to_wide(oss.str()).c_str(), L"Benchmark", MB_ICONINFORMATION);
}

void persist_current_config() {
    RECT wrc{};
    int ww = 860, wh = 540;
    if (g_app.hwnd && GetWindowRect(g_app.hwnd, &wrc)) {
        ww = std::clamp<int>(wrc.right - wrc.left, 640, 3840);
        wh = std::clamp<int>(wrc.bottom - wrc.top, 400, 2160);
    }
    save_user_config({g_app.current_dir, g_app.favorites, ww, wh, i18n::language_code(i18n::current_language())});
}

void add_favorite() {
    fs::path p = g_app.in_archive ? g_app.current_archive : g_app.current_dir;
    if (g_app.favorites.size() >= 20) return;
    g_app.favorites.push_back(p);
    AppendMenuW(g_app.fav_menu, MF_STRING, IDM_FAV_BASE + static_cast<UINT_PTR>(g_app.favorites.size() - 1), p.wstring().c_str());
    persist_current_config();
    MessageBoxW(g_app.hwnd, (tr_w_plain(i18n::StrKey::FavAdd) + L":\n" + p.wstring()).c_str(), L"OpenRAR", MB_ICONINFORMATION);
}

void open_selected_item() {
    int idx = ListView_GetNextItem(g_app.listview, -1, LVNI_FOCUSED);
    if (idx < 0) idx = ListView_GetNextItem(g_app.listview, -1, LVNI_SELECTED);
    if (idx < 0 || static_cast<size_t>(idx) >= g_app.items.size()) return;
    const auto& it = g_app.items[idx];
    if (it.is_parent) {
        go_up_one_level();
    } else if (!g_app.in_archive) {
        fs::path target = path_from_utf8(it.full_path);
        if (it.is_dir) populate_listview_filesystem(target);
        else if (it.is_archive) populate_listview_archive(target);
        else cmd_view();
    } else {
        if (it.is_dir) render_archive_subdir(it.full_path);
        else cmd_view();
    }
}

void show_listview_context_menu(POINT pt) {
    HMENU menu = CreatePopupMenu();
    auto sel = get_selected_items();
    if (g_app.in_archive) {
        AppendMenuW(menu, MF_STRING, IDM_CMD_VIEW, (tr_w(i18n::StrKey::CmdView) + L"\tAlt+V").c_str());
        AppendMenuW(menu, MF_STRING, IDM_CMD_EXTRACT, (tr_w(i18n::StrKey::CmdExtract) + L"\tAlt+E").c_str());
        AppendMenuW(menu, MF_STRING, IDM_CMD_EXTRACT_HERE, tr_w(i18n::StrKey::CtxExtractHere).c_str());
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, IDM_CMD_ADD, (tr_w(i18n::StrKey::CmdAdd) + L"\tAlt+A").c_str());
        AppendMenuW(menu, MF_STRING, IDM_CMD_DELETE, (tr_w(i18n::StrKey::CmdDelete) + L"\tDel").c_str());
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, IDM_CMD_TEST, (tr_w(i18n::StrKey::CmdTest) + L"\tAlt+T").c_str());
        AppendMenuW(menu, MF_STRING, IDM_CMD_INFO, (tr_w(i18n::StrKey::CmdInfo) + L"\tAlt+I").c_str());
        AppendMenuW(menu, MF_STRING, IDM_CMD_COMMENT, (tr_w(i18n::StrKey::CmdComment) + L"\tAlt+M").c_str());
    } else {
        if (!sel.empty() && sel[0].is_archive) {
            std::wstring stem = path_from_utf8(sel[0].full_path).stem().wstring();
            AppendMenuW(menu, MF_STRING, IDM_CMD_EXTRACT, (tr_w(i18n::StrKey::CtxExtractFiles) + L"\tAlt+E").c_str());
            AppendMenuW(menu, MF_STRING, IDM_CMD_EXTRACT_HERE, tr_w(i18n::StrKey::CtxExtractHere).c_str());
            AppendMenuW(menu, MF_STRING, IDM_CMD_EXTRACT_SUBFOLDER, (tr_w(i18n::StrKey::TbExtractTo) + L" \"" + stem + L"\\\"").c_str());
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, IDM_CMD_TEST, (tr_w(i18n::StrKey::CmdTest) + L"\tAlt+T").c_str());
            AppendMenuW(menu, MF_STRING, IDM_TOOLS_REPAIR, (tr_w(i18n::StrKey::ToolsRepair) + L"\tAlt+R").c_str());
            AppendMenuW(menu, MF_STRING, IDM_CMD_INFO, (tr_w(i18n::StrKey::CmdInfo) + L"\tAlt+I").c_str());
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        }
        if (!sel.empty()) {
            std::wstring stem = path_from_utf8(sel[0].full_path).stem().wstring();
            AppendMenuW(menu, MF_STRING, IDM_CMD_ADD, (tr_w(i18n::StrKey::CmdAdd) + L"\tAlt+A").c_str());
            AppendMenuW(menu, MF_STRING, IDM_CMD_QUICK_ORAR, (tr_w(i18n::StrKey::TbAdd) + L" -> \"" + stem + L".orar\"").c_str());
            AppendMenuW(menu, MF_STRING, IDM_CMD_QUICK_ZIP,  (tr_w(i18n::StrKey::TbAdd) + L" -> \"" + stem + L".zip\"").c_str());
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, IDM_CMD_VIEW, tr_w(i18n::StrKey::CmdView).c_str());
            AppendMenuW(menu, MF_STRING, IDM_CMD_DELETE, (tr_w(i18n::StrKey::CmdDelete) + L"\tDel").c_str());
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        }
        AppendMenuW(menu, MF_STRING, IDM_CMD_REFRESH, (tr_w(i18n::StrKey::CtxRefresh) + L"\tF5").c_str());
    }
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_app.hwnd, nullptr);
    DestroyMenu(menu);
}

// ============================================================================
// Main Window Plumbing
// ============================================================================
LRESULT CALLBACK ComboEditSubProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_KEYDOWN && wp == VK_RETURN) {
        fs::path p = get_window_text(g_app.combo_path);
        std::error_code ec;
        if (fs::is_directory(p, ec)) populate_listview_filesystem(p);
        else if (fs::is_regular_file(p, ec) && is_archive_path(p)) populate_listview_archive(p);
        SetFocus(g_app.listview);
        return 0;
    }
    return CallWindowProcW(g_orig_combo_edit_proc, hwnd, msg, wp, lp);
}

void layout_controls(int w, int h) {
    constexpr int kTbH = 66, kNavH = 28;
    MoveWindow(g_app.toolbar, 0, 0, w, kTbH, TRUE);
    MoveWindow(g_app.btn_up, 4, kTbH + 3, 30, 22, TRUE);
    MoveWindow(g_app.combo_path, 40, kTbH + 2, std::max(100, w - 46), 240, TRUE);

    SendMessageW(g_app.statusbar, WM_SIZE, 0, 0);
    RECT sb_rc{}; GetWindowRect(g_app.statusbar, &sb_rc);
    int sb_h = sb_rc.bottom - sb_rc.top;
    int parts[2] = {std::max(140, w / 2), -1};
    SendMessageW(g_app.statusbar, SB_SETPARTS, 2, reinterpret_cast<LPARAM>(parts));
    MoveWindow(g_app.listview, 0, kTbH + kNavH, w, std::max(100, h - kTbH - kNavH - sb_h), TRUE);
}

HMENU build_main_menu() {
    HMENU mbar = CreateMenu();
    HMENU m_file = CreatePopupMenu();
    AppendMenuW(m_file, MF_STRING, IDM_FILE_OPEN, (tr_w(i18n::StrKey::FileOpen) + L"\tCtrl+O").c_str());
    AppendMenuW(m_file, MF_STRING, IDM_FILE_FOLDER, tr_w(i18n::StrKey::FileBrowseFolder).c_str());
    AppendMenuW(m_file, MF_STRING, IDM_FILE_PASSWORD, (tr_w(i18n::StrKey::FilePassword) + L"\tCtrl+P").c_str());
    AppendMenuW(m_file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m_file, MF_STRING, IDM_FILE_EXIT, tr_w(i18n::StrKey::FileExit).c_str());

    HMENU m_cmd = CreatePopupMenu();
    AppendMenuW(m_cmd, MF_STRING, IDM_CMD_ADD, (tr_w(i18n::StrKey::CmdAdd) + L"\tAlt+A").c_str());
    AppendMenuW(m_cmd, MF_STRING, IDM_CMD_EXTRACT, (tr_w(i18n::StrKey::CmdExtract) + L"\tAlt+E").c_str());
    AppendMenuW(m_cmd, MF_STRING, IDM_CMD_TEST, (tr_w(i18n::StrKey::CmdTest) + L"\tAlt+T").c_str());
    AppendMenuW(m_cmd, MF_STRING, IDM_CMD_VIEW, (tr_w(i18n::StrKey::CmdView) + L"\tAlt+V").c_str());
    AppendMenuW(m_cmd, MF_STRING, IDM_CMD_DELETE, (tr_w(i18n::StrKey::CmdDelete) + L"\tDel").c_str());
    AppendMenuW(m_cmd, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m_cmd, MF_STRING, IDM_CMD_INFO, (tr_w(i18n::StrKey::CmdInfo) + L"\tAlt+I").c_str());
    AppendMenuW(m_cmd, MF_STRING, IDM_CMD_COMMENT, (tr_w(i18n::StrKey::CmdComment) + L"\tAlt+M").c_str());

    HMENU m_tools = CreatePopupMenu();
    AppendMenuW(m_tools, MF_STRING, IDM_TOOLS_WIZARD, (tr_w(i18n::StrKey::ToolsWizard) + L"\tAlt+W").c_str());
    AppendMenuW(m_tools, MF_STRING, IDM_TOOLS_FIND, (tr_w(i18n::StrKey::ToolsFind) + L"\tF3").c_str());
    AppendMenuW(m_tools, MF_STRING, IDM_TOOLS_REPAIR, (tr_w(i18n::StrKey::ToolsRepair) + L"\tAlt+R").c_str());
    AppendMenuW(m_tools, MF_STRING, IDM_TOOLS_SFX, tr_w(i18n::StrKey::ToolsSfx).c_str());
    AppendMenuW(m_tools, MF_STRING, IDM_TOOLS_BENCHMARK, (tr_w(i18n::StrKey::ToolsBenchmark) + L"\tAlt+B").c_str());

    g_app.fav_menu = CreatePopupMenu();
    AppendMenuW(g_app.fav_menu, MF_STRING, IDM_FAV_ADD, (tr_w(i18n::StrKey::FavAdd) + L"\tCtrl+F").c_str());
    AppendMenuW(g_app.fav_menu, MF_SEPARATOR, 0, nullptr);
    for (size_t i = 0; i < g_app.favorites.size(); ++i) {
        AppendMenuW(g_app.fav_menu, MF_STRING, IDM_FAV_BASE + static_cast<UINT_PTR>(i), g_app.favorites[i].wstring().c_str());
    }

    HMENU m_lang = CreatePopupMenu();
    const auto& langs = i18n::available_languages();
    i18n::Language cur_lang = i18n::current_language();
    for (size_t i = 0; i < langs.size(); ++i) {
        UINT flags = MF_STRING | (langs[i].id == cur_lang ? MF_CHECKED : MF_UNCHECKED);
        std::wstring label = utf8_to_wide(std::string(langs[i].native_name) + " (" + langs[i].code + ")");
        AppendMenuW(m_lang, flags, IDM_LANG_BASE + static_cast<UINT_PTR>(i), label.c_str());
    }

    HMENU m_opt = CreatePopupMenu();
    AppendMenuW(m_opt, MF_POPUP, reinterpret_cast<UINT_PTR>(m_lang), tr_w(i18n::StrKey::MenuLanguage).c_str());
    AppendMenuW(m_opt, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m_opt, MF_STRING, IDM_OPT_SHELL_REG, tr_w(i18n::StrKey::OptShellReg).c_str());
    AppendMenuW(m_opt, MF_STRING, IDM_OPT_SHELL_UNREG, tr_w(i18n::StrKey::OptShellUnreg).c_str());

    HMENU m_help = CreatePopupMenu();
    AppendMenuW(m_help, MF_STRING, IDM_HELP_ABOUT, tr_w(i18n::StrKey::HelpAbout).c_str());

    AppendMenuW(mbar, MF_POPUP, reinterpret_cast<UINT_PTR>(m_file), tr_w(i18n::StrKey::MenuFile).c_str());
    AppendMenuW(mbar, MF_POPUP, reinterpret_cast<UINT_PTR>(m_cmd), tr_w(i18n::StrKey::MenuCommands).c_str());
    AppendMenuW(mbar, MF_POPUP, reinterpret_cast<UINT_PTR>(m_tools), tr_w(i18n::StrKey::MenuTools).c_str());
    AppendMenuW(mbar, MF_POPUP, reinterpret_cast<UINT_PTR>(g_app.fav_menu), tr_w(i18n::StrKey::MenuFavorites).c_str());
    AppendMenuW(mbar, MF_POPUP, reinterpret_cast<UINT_PTR>(m_opt), tr_w(i18n::StrKey::MenuOptions).c_str());
    AppendMenuW(mbar, MF_POPUP, reinterpret_cast<UINT_PTR>(m_help), tr_w(i18n::StrKey::MenuHelp).c_str());
    return mbar;
}

void update_listview_column_headers() {
    const i18n::StrKey col_keys[6] = {
        i18n::StrKey::ColName, i18n::StrKey::ColSize, i18n::StrKey::ColType,
        i18n::StrKey::ColModified, i18n::StrKey::ColPacked, i18n::StrKey::ColCrc32
    };
    for (int i = 0; i < 6; ++i) {
        std::wstring title = tr_w(col_keys[i]);
        LVCOLUMNW col{};
        col.mask = LVCF_TEXT;
        col.pszText = title.data();
        ListView_SetColumn(g_app.listview, i, &col);
    }
}

void apply_language(i18n::Language lang) {
    i18n::set_language(lang);
    HMENU old_menu = GetMenu(g_app.hwnd);
    SetMenu(g_app.hwnd, build_main_menu());
    if (old_menu) DestroyMenu(old_menu);
    DrawMenuBar(g_app.hwnd);
    update_listview_column_headers();
    if (g_app.toolbar) InvalidateRect(g_app.toolbar, nullptr, TRUE);
    update_status_bar();
    persist_current_config();
}

LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            g_app.hwnd = hwnd;
            HINSTANCE hi = GetModuleHandleW(nullptr);
            g_app.font = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                     CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

            g_app.app_icon_big = static_cast<HICON>(LoadImageW(hi, MAKEINTRESOURCEW(101), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR));
            g_app.app_icon_small = static_cast<HICON>(LoadImageW(hi, MAKEINTRESOURCEW(101), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR));
            if (!g_app.app_icon_big) g_app.app_icon_big = create_app_icon(32);
            if (!g_app.app_icon_small) g_app.app_icon_small = create_app_icon(16);
            SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(g_app.app_icon_big));
            SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(g_app.app_icon_small));

            g_app.img_list = ImageList_Create(16, 16, ILC_COLOR32 | ILC_MASK, 32, 32);
            SHFILEINFOW sfi{};
            SHGetFileInfoW(L"folder", FILE_ATTRIBUTE_DIRECTORY, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES);
            g_app.icon_folder = sfi.hIcon ? ImageList_AddIcon(g_app.img_list, sfi.hIcon) : 0;
            if (sfi.hIcon) DestroyIcon(sfi.hIcon);
            g_app.icon_archive = ImageList_AddIcon(g_app.img_list, g_app.app_icon_small);
            SHGetFileInfoW(L"file.txt", FILE_ATTRIBUTE_NORMAL, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES);
            g_app.icon_file = sfi.hIcon ? ImageList_AddIcon(g_app.img_list, sfi.hIcon) : 0;
            if (sfi.hIcon) DestroyIcon(sfi.hIcon);

            SetMenu(hwnd, build_main_menu());
            g_app.toolbar = create_app_toolbar(hwnd, hi, IDC_TOOLBAR);

            g_app.btn_up = CreateWindowW(L"BUTTON", L"\x2191", WS_CHILD | WS_VISIBLE | BS_FLAT, 0, 0, 30, 22, hwnd, reinterpret_cast<HMENU>(IDC_BTN_UP), hi, nullptr);
            g_app.combo_path = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWN | CBS_AUTOHSCROLL | WS_VSCROLL, 0, 0, 500, 240, hwnd, reinterpret_cast<HMENU>(IDC_COMBO_PATH), hi, nullptr);
            SendMessageW(g_app.btn_up, WM_SETFONT, reinterpret_cast<WPARAM>(g_app.font), TRUE);
            SendMessageW(g_app.combo_path, WM_SETFONT, reinterpret_cast<WPARAM>(g_app.font), TRUE);
            COMBOBOXINFO cbi{sizeof(cbi)};
            if (GetComboBoxInfo(g_app.combo_path, &cbi) && cbi.hwndItem) {
                g_orig_combo_edit_proc = reinterpret_cast<WNDPROC>(
                    SetWindowLongPtrW(cbi.hwndItem, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(ComboEditSubProc)));
            }

            g_app.listview = CreateWindowExW(0, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS,
                                             0, 0, 800, 400, hwnd, reinterpret_cast<HMENU>(IDC_LISTVIEW), hi, nullptr);
            ListView_SetExtendedListViewStyle(g_app.listview, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
            ListView_SetImageList(g_app.listview, g_app.img_list, LVSIL_SMALL);
            SetWindowTheme(g_app.listview, L"Explorer", nullptr);
            SendMessageW(g_app.listview, WM_SETFONT, reinterpret_cast<WPARAM>(g_app.font), TRUE);

            struct ColDef { i18n::StrKey key; int width; int fmt; };
            ColDef cols[] = {
                {i18n::StrKey::ColName, 260, LVCFMT_LEFT}, {i18n::StrKey::ColSize, 90, LVCFMT_RIGHT}, {i18n::StrKey::ColType, 160, LVCFMT_LEFT},
                {i18n::StrKey::ColModified, 145, LVCFMT_LEFT}, {i18n::StrKey::ColPacked, 85, LVCFMT_RIGHT}, {i18n::StrKey::ColCrc32, 80, LVCFMT_LEFT}
            };
            for (int i = 0; i < 6; ++i) {
                std::wstring title = tr_w(cols[i].key);
                LVCOLUMNW col{}; col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
                col.fmt = cols[i].fmt; col.cx = cols[i].width; col.pszText = title.data();
                ListView_InsertColumn(g_app.listview, i, &col);
            }

            g_app.statusbar = CreateWindowExW(0, STATUSCLASSNAMEW, nullptr, WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
                                              0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_STATUSBAR), hi, nullptr);
            DragAcceptFiles(hwnd, TRUE);
            return 0;
        }
        case WM_ERASEBKGND: {
            RECT rc{}; GetClientRect(hwnd, &rc);
            FillRect(reinterpret_cast<HDC>(wp), &rc, reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
            return 1;
        }
        case WM_SIZE: layout_controls(LOWORD(lp), HIWORD(lp)); return 0;
        case WM_DROPFILES: {
            HDROP hdrop = reinterpret_cast<HDROP>(wp);
            UINT cnt = DragQueryFileW(hdrop, 0xFFFFFFFF, nullptr, 0);
            std::vector<fs::path> dropped;
            for (UINT i = 0; i < cnt; ++i) {
                wchar_t buf[MAX_PATH * 2];
                if (DragQueryFileW(hdrop, i, buf, static_cast<UINT>(std::size(buf)))) dropped.emplace_back(buf);
            }
            DragFinish(hdrop);
            if (dropped.size() == 1 && is_archive_path(dropped[0])) populate_listview_archive(dropped[0]);
            else if (!dropped.empty()) cmd_add(dropped);
            return 0;
        }
        case WM_NOTIFY: {
            auto* nm = reinterpret_cast<NMHDR*>(lp);
            if (nm->idFrom == IDC_LISTVIEW) {
                if (nm->code == NM_DBLCLK) open_selected_item();
                else if (nm->code == NM_RCLICK) { POINT pt{}; GetCursorPos(&pt); show_listview_context_menu(pt); }
                else if (nm->code == LVN_KEYDOWN) {
                    auto* kd = reinterpret_cast<NMLVKEYDOWN*>(lp);
                    if (kd->wVKey == VK_RETURN) open_selected_item();
                    else if (kd->wVKey == VK_BACK) go_up_one_level();
                    else if (kd->wVKey == VK_DELETE) cmd_delete();
                    else if (kd->wVKey == VK_F5) SendMessageW(hwnd, WM_COMMAND, IDM_CMD_REFRESH, 0);
                } else if (nm->code == LVN_ITEMCHANGED) update_status_bar();
                else if (nm->code == LVN_COLUMNCLICK) {
                    auto* lv = reinterpret_cast<NMLISTVIEW*>(lp);
                    if (g_app.sort_col == lv->iSubItem) g_app.sort_asc = !g_app.sort_asc;
                    else { g_app.sort_col = lv->iSubItem; g_app.sort_asc = true; }
                    sort_current_items();
                    render_items_to_listview();
                }
            } else if (nm->idFrom == IDC_STATUSBAR && nm->code == NM_CLICK) {
                if (reinterpret_cast<NMMOUSE*>(lp)->dwItemSpec == 0) SendMessageW(hwnd, WM_COMMAND, IDM_FILE_PASSWORD, 0);
            }
            return 0;
        }
        case WM_COMMAND: {
            int id = LOWORD(wp);
            if (id == IDC_COMBO_PATH && HIWORD(wp) == CBN_SELCHANGE) {
                int sel = static_cast<int>(SendMessageW(g_app.combo_path, CB_GETCURSEL, 0, 0));
                if (sel >= 0) {
                    wchar_t buf[MAX_PATH * 2] = {};
                    SendMessageW(g_app.combo_path, CB_GETLBTEXT, sel, reinterpret_cast<LPARAM>(buf));
                    std::error_code ec;
                    if (fs::is_directory(buf, ec)) populate_listview_filesystem(buf);
                }
                return 0;
            }
            if (id >= IDM_FAV_BASE && id < IDM_FAV_BASE + 20) {
                size_t fidx = static_cast<size_t>(id - IDM_FAV_BASE);
                if (fidx < g_app.favorites.size()) {
                    const auto& fp = g_app.favorites[fidx];
                    if (fs::is_directory(fp)) populate_listview_filesystem(fp);
                    else if (is_archive_path(fp)) populate_listview_archive(fp);
                }
                return 0;
            }
            if (id >= IDM_LANG_BASE && id < IDM_LANG_BASE + static_cast<int>(i18n::Language::Count)) {
                size_t lidx = static_cast<size_t>(id - IDM_LANG_BASE);
                const auto& langs = i18n::available_languages();
                if (lidx < langs.size()) apply_language(langs[lidx].id);
                return 0;
            }
            switch (id) {
                case IDM_FILE_OPEN: { fs::path fp; if (open_file_dialog(hwnd, kAllArchivesFilter, fp)) populate_listview_archive(fp); break; }
                case IDM_FILE_FOLDER: { fs::path dir; if (browse_folder(hwnd, tr_w_plain(i18n::StrKey::FileBrowseFolder).c_str(), dir)) populate_listview_filesystem(dir); break; }
                case IDM_FILE_PASSWORD: {
                    std::wstring pw = utf8_to_wide(g_app.default_password);
                    if (show_input_dialog(hwnd, g_app.font, tr_w_plain(i18n::StrKey::FilePassword), tr_w(i18n::StrKey::DlgEnterPassword), pw, true)) {
                        g_app.default_password = wide_to_utf8(pw);
                        update_status_bar();
                    }
                    break;
                }
                case IDM_FILE_EXIT: PostQuitMessage(0); break;
                case IDM_CMD_ADD: cmd_add(); break;
                case IDM_CMD_EXTRACT: cmd_extract(); break;
                case IDM_CMD_EXTRACT_HERE: cmd_extract_quick(false); break;
                case IDM_CMD_EXTRACT_SUBFOLDER: cmd_extract_quick(true); break;
                case IDM_CMD_QUICK_ORAR: cmd_quick_archive(ArchiveFormat::Orar); break;
                case IDM_CMD_QUICK_ZIP: cmd_quick_archive(ArchiveFormat::Zip); break;
                case IDM_CMD_TEST: cmd_test(); break;
                case IDM_CMD_VIEW: cmd_view(); break;
                case IDM_CMD_DELETE: cmd_delete(); break;
                case IDM_CMD_INFO: cmd_info(); break;
                case IDM_CMD_COMMENT: cmd_comment(); break;
                case IDM_CMD_REFRESH:
                    if (g_app.in_archive) {
                        std::string keep_sub = g_app.archive_subdir;
                        populate_listview_archive(g_app.current_archive);
                        if (!keep_sub.empty()) render_archive_subdir(keep_sub);
                    } else {
                        populate_listview_filesystem(g_app.current_dir);
                    }
                    break;
                case IDM_CMD_SELECT_ALL:
                    for (size_t i = 0; i < g_app.items.size(); ++i)
                        if (!g_app.items[i].is_parent) ListView_SetItemState(g_app.listview, static_cast<int>(i), LVIS_SELECTED, LVIS_SELECTED);
                    break;
                case IDM_TOOLS_WIZARD: cmd_wizard(); break;
                case IDM_TOOLS_FIND: cmd_find(); break;
                case IDM_TOOLS_REPAIR: cmd_repair(); break;
                case IDM_TOOLS_SFX: cmd_sfx(); break;
                case IDM_TOOLS_BENCHMARK: cmd_benchmark(); break;
                case IDM_FAV_ADD: add_favorite(); break;
                case IDM_OPT_SHELL_REG: {
                    fs::path gui = get_exe_path();
                    std::string err;
                    bool ok = ArchiveManager::register_shell_context_menu(gui, gui.parent_path() / "openrar.exe", &err);
                    MessageBoxW(hwnd, ok ? tr_w(i18n::StrKey::MsgShellRegOk).c_str() : utf8_to_wide(err).c_str(),
                                L"OpenRAR", ok ? MB_ICONINFORMATION : MB_ICONERROR);
                    break;
                }
                case IDM_OPT_SHELL_UNREG:
                    ArchiveManager::unregister_shell_context_menu();
                    MessageBoxW(hwnd, tr_w(i18n::StrKey::MsgShellUnregOk).c_str(), L"OpenRAR", MB_ICONINFORMATION);
                    break;
                case IDM_HELP_ABOUT:
                    MessageBoxW(hwnd,
                                L"OpenRAR 1.0 (Open-Source C++20 Archiver)\n\n"
                                L"Cross-platform Windows, Linux & macOS engine:\n"
                                L"- OpenRAR (.orar): Solid, AES-256-GCM, Encrypted Headers, Recovery Records, Split Volumes\n"
                                L"- ZIP / ZIP64 (.zip): Dynamic Huffman Deflate, AES-256 & ZipCrypto, Comments, Repair\n"
                                L"- RAR4/RAR5, 7z, TAR, TAR.GZ, TAR.XZ, TAR.BZ2, CAB, ISO\n"
                                L"- 13 Languages (en, zh, es, hi, ar, pt, id, fr, ja, ru, sr, hr, bs)\n"
                                L"- Self-Extracting Archives (SFX), Wizard, Find in Archives, Multi-Threaded Benchmark",
                                tr_w_plain(i18n::StrKey::HelpAbout).c_str(), MB_ICONINFORMATION);
                    break;
                case IDC_BTN_UP: go_up_one_level(); break;
            }
            return 0;
        }
        case WM_DESTROY: {
            persist_current_config();
            if (g_app.font) DeleteObject(g_app.font);
            if (g_app.app_icon_big) DestroyIcon(g_app.app_icon_big);
            if (g_app.app_icon_small) DestroyIcon(g_app.app_icon_small);
            if (g_app.img_list) ImageList_Destroy(g_app.img_list);
            PostQuitMessage(0);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int nCmdShow) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    Gdiplus::GdiplusStartupInput gdi_in;
    ULONG_PTR gdi_token = 0;
    Gdiplus::GdiplusStartup(&gdi_token, &gdi_in, nullptr);

    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES | ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    WNDCLASSW wc{};
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"OpenRARMainWindow";
    wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&wc);

    auto cfg = load_user_config();
    if (cfg.language_code.empty() || !i18n::set_language_code(cfg.language_code)) {
        i18n::set_language(i18n::detect_system_language());
    }
    g_app.favorites = std::move(cfg.favorites);

    HWND hwnd = CreateWindowExW(0, L"OpenRARMainWindow", L"OpenRAR",
                                WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, cfg.win_width, cfg.win_height,
                                nullptr, nullptr, hInst, nullptr);

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);

    // Handle direct Windows Explorer right-click menu actions without showing the main window
    if (argc >= 3) {
        std::wstring flag = argv[1];
        fs::path target = argv[2];
        bool handled_direct = false;

        if (flag == L"--add") {
            cmd_add({target});
            handled_direct = true;
        } else if (flag == L"--quick-orar" || flag == L"--quick-zip" || flag == L"--quick-7z") {
            ArchiveFormat fmt = (flag == L"--quick-zip") ? ArchiveFormat::Zip
                              : (flag == L"--quick-7z")  ? ArchiveFormat::SevenZip
                                                         : ArchiveFormat::Orar;
            fs::path out = target.parent_path() / path_from_utf8(path_to_utf8(target.stem()) + default_extension(fmt));
            CreateOptions co;
            co.format = fmt;
            co.level = CompressionLevel::Normal;
            if (fmt == ArchiveFormat::Orar) { co.add_recovery_record = true; co.recovery_percent = 5; }
            auto res = run_with_progress(hwnd, g_app.font, L"Creating " + out.filename().wstring(),
                [&](ProgressCallback cb, std::atomic<bool>* cancel) {
                    co.progress = std::move(cb); co.cancel_flag = cancel;
                    return ArchiveManager::create(out, {target}, co);
                });
            if (!res.success) MessageBoxW(hwnd, utf8_to_wide(res.message).c_str(), L"OpenRAR", MB_ICONERROR);
            handled_direct = true;
        } else if (flag == L"--extract") {
            g_app.in_archive = true;
            g_app.current_archive = target;
            cmd_extract();
            handled_direct = true;
        } else if (flag == L"--extract-here" || flag == L"--extract-folder") {
            g_app.in_archive = true;
            g_app.current_archive = target;
            cmd_extract_quick(flag == L"--extract-folder");
            handled_direct = true;
        } else if (flag == L"--test") {
            g_app.in_archive = true;
            g_app.current_archive = target;
            cmd_test();
            handled_direct = true;
        }

        if (handled_direct) {
            if (argv) LocalFree(argv);
            DestroyWindow(hwnd);
            Gdiplus::GdiplusShutdown(gdi_token);
            CoUninitialize();
            return 0;
        }
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    if (argc >= 2 && fs::exists(argv[1])) {
        fs::path p = argv[1];
        if (fs::is_directory(p)) populate_listview_filesystem(p);
        else if (is_archive_path(p)) populate_listview_archive(p);
        else populate_listview_filesystem(p.parent_path());
    } else if (!cfg.last_dir.empty()) {
        populate_listview_filesystem(cfg.last_dir);
    } else {
        PWSTR dl_path = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, 0, nullptr, &dl_path)) && dl_path) {
            populate_listview_filesystem(fs::path(dl_path));
            CoTaskMemFree(dl_path);
        } else {
            populate_listview_filesystem(fs::current_path());
        }
    }
    if (argv) LocalFree(argv);

    ACCEL accels[] = {
        {FVIRTKEY | FCONTROL, 'O', IDM_FILE_OPEN},
        {FVIRTKEY | FCONTROL, 'P', IDM_FILE_PASSWORD},
        {FVIRTKEY | FCONTROL, 'F', IDM_FAV_ADD},
        {FVIRTKEY | FCONTROL, 'A', IDM_CMD_SELECT_ALL},
        {FVIRTKEY | FALT,     'A', IDM_CMD_ADD},
        {FVIRTKEY | FALT,     'E', IDM_CMD_EXTRACT},
        {FVIRTKEY | FALT,     'T', IDM_CMD_TEST},
        {FVIRTKEY | FALT,     'V', IDM_CMD_VIEW},
        {FVIRTKEY | FALT,     'I', IDM_CMD_INFO},
        {FVIRTKEY | FALT,     'M', IDM_CMD_COMMENT},
        {FVIRTKEY | FALT,     'W', IDM_TOOLS_WIZARD},
        {FVIRTKEY | FALT,     'R', IDM_TOOLS_REPAIR},
        {FVIRTKEY | FALT,     'B', IDM_TOOLS_BENCHMARK},
        {FVIRTKEY,            VK_F3, IDM_TOOLS_FIND},
        {FVIRTKEY,            VK_F5, IDM_CMD_REFRESH}
    };
    HACCEL haccel = CreateAcceleratorTableW(accels, static_cast<int>(std::size(accels)));

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (!TranslateAcceleratorW(hwnd, haccel, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    if (haccel) DestroyAcceleratorTable(haccel);

    Gdiplus::GdiplusShutdown(gdi_token);
    CoUninitialize();
    return 0;
}
