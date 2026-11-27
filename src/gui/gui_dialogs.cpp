#include "gui_dialogs.hpp"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <thread>

namespace openrar::gui {

namespace {

// Helper to run any Win32 modal popup loop cleanly centered over `owner`
void run_modal_dialog(HWND owner, const wchar_t* class_name, WNDPROC wnd_proc,
                      const wchar_t* title, int width, int height, void* user_param) {
    HINSTANCE hi = GetModuleHandleW(nullptr);
    WNDCLASSW wc{};
    if (!GetClassInfoW(hi, class_name, &wc)) {
        wc.lpfnWndProc = wnd_proc;
        wc.hInstance = hi;
        wc.lpszClassName = class_name;
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        RegisterClassW(&wc);
    }

    int x = 0, y = 0;
    bool owner_visible = owner && IsWindowVisible(owner);
    if (owner_visible) {
        RECT rc{};
        GetWindowRect(owner, &rc);
        x = rc.left + ((rc.right - rc.left) - width) / 2;
        y = rc.top + ((rc.bottom - rc.top) - height) / 2;
    } else {
        x = (GetSystemMetrics(SM_CXSCREEN) - width) / 2;
        y = (GetSystemMetrics(SM_CYSCREEN) - height) / 2;
    }

    if (owner_visible) EnableWindow(owner, FALSE);
    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, class_name, title,
                               WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
                               x, y, width, height, owner_visible ? owner : nullptr, nullptr, hi, user_param);
    MSG msg;
    while (IsWindow(dlg) && GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    if (owner_visible) {
        EnableWindow(owner, TRUE);
        SetForegroundWindow(owner);
    }
}

// ============================================================================
// 1. Input Dialog
// ============================================================================
struct InputDialogState {
    std::wstring label, value;
    bool password = false, multiline = false, accepted = false;
    HWND edit = nullptr;
    HFONT font = nullptr;
};

LRESULT CALLBACK InputDialogProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = reinterpret_cast<InputDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_CREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            st = reinterpret_cast<InputDialogState*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));
            HWND lbl = CreateWindowW(L"STATIC", st->label.c_str(), WS_CHILD | WS_VISIBLE,
                                     14, 12, 390, 20, hwnd, nullptr, cs->hInstance, nullptr);
            DWORD style = WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL;
            if (st->password) style |= ES_PASSWORD;
            int edit_h = 26;
            if (st->multiline) {
                style = WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL;
                edit_h = 110;
            }
            st->edit = CreateWindowW(L"EDIT", st->value.c_str(), style,
                                     14, 36, 392, edit_h, hwnd, reinterpret_cast<HMENU>(100), cs->hInstance, nullptr);
            int btn_y = 36 + edit_h + 14;
            HWND ok = CreateWindowW(L"BUTTON", tr_w(i18n::StrKey::BtnOk).c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                                    236, btn_y, 80, 28, hwnd, reinterpret_cast<HMENU>(IDOK), cs->hInstance, nullptr);
            HWND cn = CreateWindowW(L"BUTTON", tr_w(i18n::StrKey::BtnCancel).c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                    326, btn_y, 80, 28, hwnd, reinterpret_cast<HMENU>(IDCANCEL), cs->hInstance, nullptr);
            for (HWND w : {lbl, st->edit, ok, cn})
                SendMessageW(w, WM_SETFONT, reinterpret_cast<WPARAM>(st->font), TRUE);
            SetFocus(st->edit);
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wp) == IDOK && st) { st->value = get_window_text(st->edit); st->accepted = true; DestroyWindow(hwnd); return 0; }
            if (LOWORD(wp) == IDCANCEL) { DestroyWindow(hwnd); return 0; }
            break;
        case WM_CLOSE: DestroyWindow(hwnd); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ============================================================================
// 2. Wizard Dialog
// ============================================================================
struct WizardState {
    int choice = 0;
    HFONT font = nullptr;
    HWND r1 = nullptr, r2 = nullptr, r3 = nullptr;
};

LRESULT CALLBACK WizardDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = reinterpret_cast<WizardState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_CREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            st = reinterpret_cast<WizardState*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));
            HINSTANCE hi = cs->hInstance;
            auto sf = [&](HWND w) { SendMessageW(w, WM_SETFONT, reinterpret_cast<WPARAM>(st->font), TRUE); };
            sf(CreateWindowW(L"STATIC", tr_w(i18n::StrKey::DlgWizardPrompt).c_str(), WS_CHILD | WS_VISIBLE, 18, 16, 360, 22, hwnd, nullptr, hi, nullptr));
            st->r1 = CreateWindowW(L"BUTTON", tr_w(i18n::StrKey::DlgWizardUnpack).c_str(), WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP, 28, 48, 340, 24, hwnd, nullptr, hi, nullptr);
            st->r2 = CreateWindowW(L"BUTTON", tr_w(i18n::StrKey::DlgWizardCreate).c_str(), WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON, 28, 78, 340, 24, hwnd, nullptr, hi, nullptr);
            st->r3 = CreateWindowW(L"BUTTON", tr_w(i18n::StrKey::DlgWizardAddExist).c_str(), WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON, 28, 108, 340, 24, hwnd, nullptr, hi, nullptr);
            SendMessageW(st->r1, BM_SETCHECK, BST_CHECKED, 0);
            HWND ok = CreateWindowW(L"BUTTON", tr_w(i18n::StrKey::BtnNext).c_str(), WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 210, 152, 82, 28, hwnd, reinterpret_cast<HMENU>(IDOK), hi, nullptr);
            HWND cn = CreateWindowW(L"BUTTON", tr_w(i18n::StrKey::BtnCancel).c_str(), WS_CHILD | WS_VISIBLE, 300, 152, 82, 28, hwnd, reinterpret_cast<HMENU>(IDCANCEL), hi, nullptr);
            for (HWND w : {st->r1, st->r2, st->r3, ok, cn}) sf(w);
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wp) == IDOK && st) {
                if (SendMessageW(st->r1, BM_GETCHECK, 0, 0) == BST_CHECKED) st->choice = 1;
                else if (SendMessageW(st->r2, BM_GETCHECK, 0, 0) == BST_CHECKED) st->choice = 2;
                else st->choice = 3;
                DestroyWindow(hwnd); return 0;
            }
            if (LOWORD(wp) == IDCANCEL) { DestroyWindow(hwnd); return 0; }
            break;
        case WM_CLOSE: DestroyWindow(hwnd); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ============================================================================
// 3. Add to Archive Dialog
// ============================================================================
struct AddDialogState {
    fs::path output_path;
    CreateOptions opts;
    bool accepted = false;
    HFONT font = nullptr;
    HWND ed_path = nullptr, cb_fmt = nullptr, cb_lvl = nullptr, ed_pass = nullptr;
    HWND chk_enc_hdr = nullptr, chk_solid = nullptr, chk_rr = nullptr, ed_vol = nullptr, ed_cmt = nullptr;
};

LRESULT CALLBACK AddDialogProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = reinterpret_cast<AddDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_CREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            st = reinterpret_cast<AddDialogState*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));
            HINSTANCE hi = cs->hInstance;
            auto sf = [&](HWND w) { SendMessageW(w, WM_SETFONT, reinterpret_cast<WPARAM>(st->font), TRUE); };

            int y = 14;
            sf(CreateWindowW(L"STATIC", tr_w(i18n::StrKey::DlgArchiveName).c_str(), WS_CHILD | WS_VISIBLE, 14, y, 220, 20, hwnd, nullptr, hi, nullptr));
            st->ed_path = CreateWindowW(L"EDIT", st->output_path.wstring().c_str(),
                                        WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL,
                                        14, y + 20, 390, 24, hwnd, nullptr, hi, nullptr);
            sf(st->ed_path);
            sf(CreateWindowW(L"BUTTON", tr_w(i18n::StrKey::BtnBrowse).c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                             410, y + 20, 76, 24, hwnd, reinterpret_cast<HMENU>(501), hi, nullptr));

            y += 56;
            sf(CreateWindowW(L"STATIC", tr_w(i18n::StrKey::DlgArchiveFormat).c_str(), WS_CHILD | WS_VISIBLE, 14, y, 220, 20, hwnd, nullptr, hi, nullptr));
            st->cb_fmt = CreateWindowW(L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
                                       14, y + 20, 220, 140, hwnd, reinterpret_cast<HMENU>(502), hi, nullptr);
            sf(st->cb_fmt);
            SendMessageW(st->cb_fmt, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"OpenRaR (.orar) - Solid/AES/Recovery"));
            SendMessageW(st->cb_fmt, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"ZIP (.zip) - Universal"));
            SendMessageW(st->cb_fmt, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"7-Zip (.7z) - High Compression"));
            SendMessageW(st->cb_fmt, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"TAR.GZ (.tar.gz) - Linux/Unix"));
            SendMessageW(st->cb_fmt, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"TAR (.tar) - Uncompressed"));
            SendMessageW(st->cb_fmt, CB_SETCURSEL, 0, 0);

            sf(CreateWindowW(L"STATIC", tr_w(i18n::StrKey::DlgCompMethod).c_str(), WS_CHILD | WS_VISIBLE, 250, y, 236, 20, hwnd, nullptr, hi, nullptr));
            st->cb_lvl = CreateWindowW(L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
                                       250, y + 20, 236, 150, hwnd, nullptr, hi, nullptr);
            sf(st->cb_lvl);
            for (const wchar_t* s : {L"Store", L"Fastest", L"Fast", L"Normal", L"Good", L"Best"})
                SendMessageW(st->cb_lvl, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(s));
            SendMessageW(st->cb_lvl, CB_SETCURSEL, 3, 0);

            y += 56;
            sf(CreateWindowW(L"STATIC", tr_w(i18n::StrKey::DlgSetPassword).c_str(), WS_CHILD | WS_VISIBLE, 14, y, 220, 20, hwnd, nullptr, hi, nullptr));
            st->ed_pass = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_PASSWORD | ES_AUTOHSCROLL,
                                        14, y + 20, 220, 24, hwnd, nullptr, hi, nullptr);
            sf(st->ed_pass);

            sf(CreateWindowW(L"STATIC", tr_w(i18n::StrKey::DlgSplitVolumes).c_str(), WS_CHILD | WS_VISIBLE, 250, y, 236, 20, hwnd, nullptr, hi, nullptr));
            st->ed_vol = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL,
                                       250, y + 20, 236, 24, hwnd, nullptr, hi, nullptr);
            sf(st->ed_vol);

            y += 54;
            st->chk_solid = CreateWindowW(L"BUTTON", tr_w(i18n::StrKey::DlgSolidArchive).c_str(), WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP, 14, y, 220, 22, hwnd, nullptr, hi, nullptr);
            st->chk_rr = CreateWindowW(L"BUTTON", tr_w(i18n::StrKey::DlgRecoveryRecord).c_str(), WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP, 250, y, 236, 22, hwnd, nullptr, hi, nullptr);
            sf(st->chk_solid); sf(st->chk_rr);
            SendMessageW(st->chk_rr, BM_SETCHECK, BST_CHECKED, 0);

            y += 26;
            st->chk_enc_hdr = CreateWindowW(L"BUTTON", tr_w(i18n::StrKey::DlgEncryptNames).c_str(), WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP, 14, y, 280, 22, hwnd, nullptr, hi, nullptr);
            sf(st->chk_enc_hdr);

            y += 32;
            sf(CreateWindowW(L"STATIC", tr_w(i18n::StrKey::DlgArchiveComment).c_str(), WS_CHILD | WS_VISIBLE, 14, y, 220, 20, hwnd, nullptr, hi, nullptr));
            st->ed_cmt = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 14, y + 20, 472, 24, hwnd, nullptr, hi, nullptr);
            sf(st->ed_cmt);

            y += 58;
            sf(CreateWindowW(L"BUTTON", tr_w(i18n::StrKey::BtnOk).c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 304, y, 88, 28, hwnd, reinterpret_cast<HMENU>(IDOK), hi, nullptr));
            sf(CreateWindowW(L"BUTTON", tr_w(i18n::StrKey::BtnCancel).c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP, 400, y, 86, 28, hwnd, reinterpret_cast<HMENU>(IDCANCEL), hi, nullptr));
            return 0;
        }
        case WM_COMMAND:
            if (!st) break;
            if (LOWORD(wp) == 501) {
                fs::path p;
                if (save_file_dialog(hwnd, L"Archives (*.orar;*.zip;*.7z;*.tar.gz;*.tar)\0*.orar;*.zip;*.7z;*.tar.gz;*.tar\0All Files\0*.*\0",
                                     st->output_path.filename().c_str(), p))
                    SetWindowTextW(st->ed_path, p.wstring().c_str());
                return 0;
            }
            if (LOWORD(wp) == 502 && HIWORD(wp) == CBN_SELCHANGE) {
                int sel = static_cast<int>(SendMessageW(st->cb_fmt, CB_GETCURSEL, 0, 0));
                const wchar_t* exts[] = {L".orar", L".zip", L".7z", L".tar.gz", L".tar"};
                fs::path cur = get_window_text(st->ed_path);
                if (!cur.empty() && sel >= 0 && sel < 5) {
                    std::wstring stem = cur.stem().wstring();
                    if (stem.ends_with(L".tar")) stem = stem.substr(0, stem.size() - 4);
                    SetWindowTextW(st->ed_path, (cur.parent_path() / (stem + exts[sel])).wstring().c_str());
                }
                return 0;
            }
            if (LOWORD(wp) == IDOK) {
                st->output_path = get_window_text(st->ed_path);
                int fidx = static_cast<int>(SendMessageW(st->cb_fmt, CB_GETCURSEL, 0, 0));
                ArchiveFormat fmts[] = {ArchiveFormat::Orar, ArchiveFormat::Zip, ArchiveFormat::SevenZip, ArchiveFormat::TarGz, ArchiveFormat::Tar};
                st->opts.format = (fidx >= 0 && fidx < 5) ? fmts[fidx] : ArchiveFormat::Orar;
                st->opts.level = static_cast<CompressionLevel>(std::clamp(static_cast<int>(SendMessageW(st->cb_lvl, CB_GETCURSEL, 0, 0)), 0, 5));
                st->opts.password = wide_to_utf8(get_window_text(st->ed_pass));
                st->opts.encrypt_headers = (SendMessageW(st->chk_enc_hdr, BM_GETCHECK, 0, 0) == BST_CHECKED);
                st->opts.solid = (SendMessageW(st->chk_solid, BM_GETCHECK, 0, 0) == BST_CHECKED);
                st->opts.add_recovery_record = (SendMessageW(st->chk_rr, BM_GETCHECK, 0, 0) == BST_CHECKED);
                st->opts.recovery_percent = 5;
                st->opts.split_volume_bytes = parse_size_string(wide_to_utf8(get_window_text(st->ed_vol)));
                st->opts.comment = wide_to_utf8(get_window_text(st->ed_cmt));
                st->accepted = true;
                DestroyWindow(hwnd);
                return 0;
            }
            if (LOWORD(wp) == IDCANCEL) { DestroyWindow(hwnd); return 0; }
            break;
        case WM_CLOSE: DestroyWindow(hwnd); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ============================================================================
// 4. Extract Dialog
// ============================================================================
struct ExtractDialogState {
    ExtractOptions opts;
    bool open_folder_after = true;
    bool accepted = false;
    HFONT font = nullptr;
    HWND ed_dest = nullptr, ed_pass = nullptr;
    HWND r_ow = nullptr, r_skip = nullptr, r_ren = nullptr;
    HWND chk_paths = nullptr, chk_open = nullptr;
};

LRESULT CALLBACK ExtractDialogProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = reinterpret_cast<ExtractDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_CREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            st = reinterpret_cast<ExtractDialogState*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));
            HINSTANCE hi = cs->hInstance;
            auto sf = [&](HWND w) { SendMessageW(w, WM_SETFONT, reinterpret_cast<WPARAM>(st->font), TRUE); };

            int y = 14;
            sf(CreateWindowW(L"STATIC", tr_w(i18n::StrKey::DlgDestPath).c_str(), WS_CHILD | WS_VISIBLE, 14, y, 240, 20, hwnd, nullptr, hi, nullptr));
            st->ed_dest = CreateWindowW(L"EDIT", st->opts.destination.wstring().c_str(),
                                        WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL,
                                        14, y + 20, 354, 24, hwnd, nullptr, hi, nullptr);
            sf(st->ed_dest);
            sf(CreateWindowW(L"BUTTON", tr_w(i18n::StrKey::BtnBrowse).c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP, 374, y + 20, 78, 24, hwnd, reinterpret_cast<HMENU>(601), hi, nullptr));

            y += 56;
            sf(CreateWindowW(L"STATIC", tr_w(i18n::StrKey::DlgOverwriteMode).c_str(), WS_CHILD | WS_VISIBLE, 14, y, 200, 20, hwnd, nullptr, hi, nullptr));
            st->r_ow   = CreateWindowW(L"BUTTON", tr_w(i18n::StrKey::DlgOverwriteAll).c_str(), WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP, 22, y + 22, 210, 22, hwnd, nullptr, hi, nullptr);
            st->r_skip = CreateWindowW(L"BUTTON", tr_w(i18n::StrKey::DlgSkipExisting).c_str(), WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,            22, y + 46, 210, 22, hwnd, nullptr, hi, nullptr);
            st->r_ren  = CreateWindowW(L"BUTTON", tr_w(i18n::StrKey::DlgRenameAuto).c_str(),   WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,            22, y + 70, 210, 22, hwnd, nullptr, hi, nullptr);
            SendMessageW(st->r_ow, BM_SETCHECK, BST_CHECKED, 0);

            sf(CreateWindowW(L"STATIC", tr_w(i18n::StrKey::DlgOptions).c_str(), WS_CHILD | WS_VISIBLE, 244, y, 200, 20, hwnd, nullptr, hi, nullptr));
            st->chk_paths = CreateWindowW(L"BUTTON", tr_w(i18n::StrKey::DlgPreservePaths).c_str(), WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 248, y + 22, 200, 22, hwnd, nullptr, hi, nullptr);
            st->chk_open  = CreateWindowW(L"BUTTON", tr_w(i18n::StrKey::DlgOpenAfter).c_str(),     WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 248, y + 46, 200, 22, hwnd, nullptr, hi, nullptr);
            SendMessageW(st->chk_paths, BM_SETCHECK, BST_CHECKED, 0);
            SendMessageW(st->chk_open,  BM_SETCHECK, BST_CHECKED, 0);

            y += 104;
            sf(CreateWindowW(L"STATIC", tr_w(i18n::StrKey::DlgPasswordIfEnc).c_str(), WS_CHILD | WS_VISIBLE, 14, y, 240, 20, hwnd, nullptr, hi, nullptr));
            st->ed_pass = CreateWindowW(L"EDIT", utf8_to_wide(st->opts.password).c_str(),
                                        WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_PASSWORD | ES_AUTOHSCROLL,
                                        14, y + 20, 438, 24, hwnd, nullptr, hi, nullptr);
            sf(st->ed_pass);

            y += 58;
            HWND ok = CreateWindowW(L"BUTTON", tr_w(i18n::StrKey::BtnOk).c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 270, y, 88, 28, hwnd, reinterpret_cast<HMENU>(IDOK), hi, nullptr);
            HWND cn = CreateWindowW(L"BUTTON", tr_w(i18n::StrKey::BtnCancel).c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP, 364, y, 88, 28, hwnd, reinterpret_cast<HMENU>(IDCANCEL), hi, nullptr);
            for (HWND w : {st->r_ow, st->r_skip, st->r_ren, st->chk_paths, st->chk_open, ok, cn}) sf(w);
            return 0;
        }
        case WM_COMMAND:
            if (!st) break;
            if (LOWORD(wp) == 601) {
                fs::path p;
                if (browse_folder(hwnd, L"Select Destination Folder", p)) SetWindowTextW(st->ed_dest, p.wstring().c_str());
                return 0;
            }
            if (LOWORD(wp) == IDOK) {
                st->opts.destination = get_window_text(st->ed_dest);
                st->opts.password = wide_to_utf8(get_window_text(st->ed_pass));
                if (SendMessageW(st->r_skip, BM_GETCHECK, 0, 0) == BST_CHECKED) st->opts.overwrite = OverwriteMode::SkipExisting;
                else if (SendMessageW(st->r_ren, BM_GETCHECK, 0, 0) == BST_CHECKED) st->opts.overwrite = OverwriteMode::RenameAuto;
                else st->opts.overwrite = OverwriteMode::OverwriteAll;
                st->opts.preserve_paths = (SendMessageW(st->chk_paths, BM_GETCHECK, 0, 0) == BST_CHECKED);
                st->open_folder_after = (SendMessageW(st->chk_open, BM_GETCHECK, 0, 0) == BST_CHECKED);
                st->accepted = true;
                DestroyWindow(hwnd);
                return 0;
            }
            if (LOWORD(wp) == IDCANCEL) { DestroyWindow(hwnd); return 0; }
            break;
        case WM_CLOSE: DestroyWindow(hwnd); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ============================================================================
// 5. Non-Blocking Live Progress Dialog
// ============================================================================
struct ProgressDlgState {
    std::wstring title;
    HFONT font = nullptr;
    std::atomic<bool> cancel{false};
    std::atomic<bool> finished{false};
    std::mutex mtx;
    ProgressInfo latest;
    OperationResult result;
    HWND lbl_op = nullptr, lbl_file = nullptr, lbl_stats = nullptr, pbar = nullptr;
    std::chrono::steady_clock::time_point t0;
};

LRESULT CALLBACK ProgressDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = reinterpret_cast<ProgressDlgState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_CREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            st = reinterpret_cast<ProgressDlgState*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));
            HINSTANCE hi = cs->hInstance;
            auto sf = [&](HWND w) { SendMessageW(w, WM_SETFONT, reinterpret_cast<WPARAM>(st->font), TRUE); };

            st->lbl_op = CreateWindowW(L"STATIC", st->title.c_str(), WS_CHILD | WS_VISIBLE, 16, 14, 410, 20, hwnd, nullptr, hi, nullptr);
            st->lbl_file = CreateWindowW(L"STATIC", L"Preparing...", WS_CHILD | WS_VISIBLE | SS_PATHELLIPSIS, 16, 36, 410, 20, hwnd, nullptr, hi, nullptr);
            st->pbar = CreateWindowExW(0, PROGRESS_CLASSW, nullptr, WS_CHILD | WS_VISIBLE | PBS_SMOOTH, 16, 62, 410, 22, hwnd, nullptr, hi, nullptr);
            SendMessageW(st->pbar, PBM_SETRANGE, 0, MAKELPARAM(0, 1000));
            st->lbl_stats = CreateWindowW(L"STATIC", L"0%  |  0.0 s", WS_CHILD | WS_VISIBLE, 16, 92, 310, 20, hwnd, nullptr, hi, nullptr);
            HWND btn_cancel = CreateWindowW(L"BUTTON", tr_w(i18n::StrKey::BtnCancel).c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP, 342, 92, 84, 26, hwnd, reinterpret_cast<HMENU>(IDCANCEL), hi, nullptr);
            for (HWND w : {st->lbl_op, st->lbl_file, st->lbl_stats, btn_cancel}) sf(w);
            SetTimer(hwnd, 1, 50, nullptr);
            return 0;
        }
        case WM_TIMER:
            if (st) {
                ProgressInfo pi;
                { std::lock_guard<std::mutex> lk(st->mtx); pi = st->latest; }
                double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - st->t0).count();
                SendMessageW(st->pbar, PBM_SETPOS, std::clamp(static_cast<int>(pi.percent() * 10.0), 0, 1000), 0);
                if (!pi.current_file.empty()) SetWindowTextW(st->lbl_file, utf8_to_wide(pi.current_file).c_str());

                std::ostringstream oss;
                oss << std::fixed << std::setprecision(1) << pi.percent() << "%  |  " << pi.files_processed << " file(s)  |  ";
                if (pi.speed_bps > 1024.0) oss << std::setprecision(1) << (pi.speed_bps / (1024.0 * 1024.0)) << " MB/s  |  ";
                oss << std::setprecision(1) << elapsed << " s";
                SetWindowTextW(st->lbl_stats, utf8_to_wide(oss.str()).c_str());
                if (st->finished.load()) { KillTimer(hwnd, 1); DestroyWindow(hwnd); }
            }
            return 0;
        case WM_COMMAND:
            if (LOWORD(wp) == IDCANCEL && st) { st->cancel.store(true); SetWindowTextW(st->lbl_file, L"Cancelling..."); return 0; }
            break;
        case WM_CLOSE:
            if (st) st->cancel.store(true);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

bool show_input_dialog(HWND owner, HFONT font, const std::wstring& title, const std::wstring& label,
                       std::wstring& value, bool password, bool multiline) {
    InputDialogState st{label, value, password, multiline, false, nullptr, font};
    run_modal_dialog(owner, L"OpenRaRInputDlg", InputDialogProc, title.c_str(), 436, multiline ? 235 : 150, &st);
    if (st.accepted) value = st.value;
    return st.accepted;
}

int show_wizard_dialog(HWND owner, HFONT font) {
    WizardState st{0, font, nullptr, nullptr, nullptr};
    auto title = tr_w(i18n::StrKey::DlgWizardTitle);
    run_modal_dialog(owner, L"OpenRaRWizardDlg", WizardDlgProc, title.c_str(), 410, 230, &st);
    return st.choice;
}

bool show_add_dialog(HWND owner, HFONT font, fs::path& out_path, CreateOptions& opts) {
    AddDialogState st;
    st.output_path = out_path;
    st.opts = opts;
    st.font = font;
    auto title = tr_w(i18n::StrKey::DlgAddTitle);
    run_modal_dialog(owner, L"OpenRaRAddDlg", AddDialogProc, title.c_str(), 516, 395, &st);
    if (st.accepted) { out_path = st.output_path; opts = st.opts; }
    return st.accepted;
}

bool show_extract_dialog(HWND owner, HFONT font, ExtractOptions& opts, bool& open_folder_after) {
    ExtractDialogState st{opts, open_folder_after, false, font};
    auto title = tr_w(i18n::StrKey::DlgExtractTitle);
    run_modal_dialog(owner, L"OpenRaRExtractDlg", ExtractDialogProc, title.c_str(), 482, 315, &st);
    if (st.accepted) { opts = st.opts; open_folder_after = st.open_folder_after; }
    return st.accepted;
}

OperationResult run_with_progress(
    HWND owner, HFONT font, const std::wstring& title,
    const std::function<OperationResult(ProgressCallback, std::atomic<bool>*)>& task) {
    ProgressDlgState st;
    st.title = title;
    st.font = font;
    st.t0 = std::chrono::steady_clock::now();

    auto cb = [&](const ProgressInfo& pi) {
        std::lock_guard<std::mutex> lk(st.mtx);
        st.latest = pi;
    };

    std::thread worker([&] {
        st.result = task(cb, &st.cancel);
        st.finished.store(true);
    });

    run_modal_dialog(owner, L"OpenRaRProgressDlg", ProgressDlgProc, title.c_str(), 458, 168, &st);
    if (worker.joinable()) worker.join();
    return st.result;
}

} // namespace openrar::gui
