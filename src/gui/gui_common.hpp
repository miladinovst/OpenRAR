#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>

#include "openrar/i18n.hpp"
#include "openrar/types.hpp"

#include <algorithm>
#include <ctime>
#include <string>

namespace openrar::gui {

// All Menu & Control IDs in one place
enum ControlId : int {
    IDM_FILE_OPEN = 1001,
    IDM_FILE_FOLDER,
    IDM_FILE_PASSWORD,
    IDM_FILE_EXIT,

    IDM_CMD_ADD = 1101,
    IDM_CMD_EXTRACT,
    IDM_CMD_EXTRACT_HERE,
    IDM_CMD_EXTRACT_SUBFOLDER,
    IDM_CMD_QUICK_ORAR,
    IDM_CMD_QUICK_ZIP,
    IDM_CMD_TEST,
    IDM_CMD_VIEW,
    IDM_CMD_DELETE,
    IDM_CMD_INFO,
    IDM_CMD_COMMENT,
    IDM_CMD_REFRESH,
    IDM_CMD_SELECT_ALL,

    IDM_TOOLS_WIZARD = 1201,
    IDM_TOOLS_FIND,
    IDM_TOOLS_REPAIR,
    IDM_TOOLS_CONVERT,
    IDM_TOOLS_SFX,
    IDM_TOOLS_BENCHMARK,

    IDM_FAV_ADD = 1301,
    IDM_FAV_BASE = 1320, // up to 20 favorites

    IDM_OPT_SHELL_REG = 1401,
    IDM_OPT_SHELL_UNREG,
    IDM_LANG_BASE = 1420, // 13 supported languages (1420..1432)

    IDM_HELP_ABOUT = 1501,

    IDC_TOOLBAR = 2001,
    IDC_BTN_UP,
    IDC_COMBO_PATH,
    IDC_LISTVIEW,
    IDC_STATUSBAR
};

inline constexpr wchar_t kAllArchivesFilter[] =
    L"All Archives (*.rar;*.7z;*.orar;*.zip;*.tar.gz;*.tgz;*.tar.xz;*.tar.bz2;*.tar;*.gz;*.cab;*.iso)\0"
    L"*.rar;*.7z;*.orar;*.zip;*.tar.gz;*.tgz;*.tar.xz;*.tar.bz2;*.tar;*.gz;*.cab;*.iso\0"
    L"All Files (*.*)\0*.*\0";

inline std::wstring utf8_to_wide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

inline std::wstring tr_w(i18n::StrKey key) {
    return utf8_to_wide(i18n::tr(key));
}

/// Returns translated string stripped of Win32 `&` mnemonic accelerators (for dialog titles & labels).
inline std::wstring tr_w_plain(i18n::StrKey key) {
    std::wstring raw = tr_w(key);
    std::wstring out;
    out.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == L'&') continue;
        // Strip East-Asian/Arabic mnemonic suffixes like "(&F)" when used in plain captions
        if (raw[i] == L'(' && i + 3 < raw.size() && raw[i + 1] == L'&' && raw[i + 3] == L')') {
            i += 3;
            continue;
        }
        out.push_back(raw[i]);
    }
    return out;
}

inline std::string wide_to_utf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

inline std::wstring get_window_text(HWND hwnd) {
    int len = GetWindowTextLengthW(hwnd);
    if (len <= 0) return {};
    std::wstring buf(static_cast<size_t>(len + 1), L'\0');
    GetWindowTextW(hwnd, buf.data(), len + 1);
    buf.resize(static_cast<size_t>(len));
    return buf;
}

inline std::wstring format_commas(uint64_t n) {
    std::wstring s = std::to_wstring(n);
    std::wstring out;
    int cnt = 0;
    for (auto it = s.rbegin(); it != s.rend(); ++it) {
        if (cnt > 0 && cnt % 3 == 0) out.push_back(L',');
        out.push_back(*it);
        cnt++;
    }
    std::reverse(out.begin(), out.end());
    return out;
}

inline std::wstring format_short_date(int64_t unix_time) {
    if (unix_time <= 0) return L"";
    std::time_t t = static_cast<std::time_t>(unix_time);
    std::tm tm_val{};
    localtime_s(&tm_val, &t);
    int hr = tm_val.tm_hour;
    const wchar_t* ampm = (hr >= 12) ? L"PM" : L"AM";
    hr = hr % 12;
    if (hr == 0) hr = 12;
    wchar_t buf[64];
    swprintf_s(buf, L"%d/%d/%04d %d:%02d %s",
               tm_val.tm_mon + 1, tm_val.tm_mday, tm_val.tm_year + 1900,
               hr, tm_val.tm_min, ampm);
    return buf;
}

inline fs::path get_exe_path() {
    wchar_t buf[MAX_PATH * 4];
    GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
    return fs::path(buf);
}

inline bool browse_folder(HWND owner, const wchar_t* title, fs::path& out_path) {
    BROWSEINFOW bi{};
    bi.hwndOwner = owner;
    bi.lpszTitle = title;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return false;
    wchar_t path[MAX_PATH * 2];
    bool ok = SHGetPathFromIDListW(pidl, path) != FALSE;
    CoTaskMemFree(pidl);
    if (ok) out_path = path;
    return ok;
}

inline bool open_file_dialog(HWND owner, const wchar_t* filter, fs::path& out_path) {
    wchar_t buf[MAX_PATH * 4] = {};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = static_cast<DWORD>(std::size(buf));
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameW(&ofn)) {
        out_path = buf;
        return true;
    }
    return false;
}

inline bool save_file_dialog(HWND owner, const wchar_t* filter, const wchar_t* default_name, fs::path& out_path) {
    wchar_t buf[MAX_PATH * 4] = {};
    if (default_name) wcsncpy_s(buf, default_name, _TRUNCATE);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = static_cast<DWORD>(std::size(buf));
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (GetSaveFileNameW(&ofn)) {
        out_path = buf;
        return true;
    }
    return false;
}

} // namespace openrar::gui
