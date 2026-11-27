#include "gui_toolbar.hpp"
#include "toolbar_icons.hpp"

#include <vector>

namespace openrar::gui {

namespace {

struct ToolbarButtonDef {
    ToolbarIcon icon;
    i18n::StrKey label_key;
    int cmd_id;
    bool sep_before;
    RECT rect;
};

struct ToolbarState {
    HWND parent = nullptr;
    int hover = -1;
    int pressed = -1;
    bool tracking_mouse = false;
    std::vector<ToolbarButtonDef> buttons;
};

LRESULT CALLBACK ToolbarWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = reinterpret_cast<ToolbarState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_CREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            st = new ToolbarState();
            st->parent = cs->hwndParent;
            st->buttons = {
                {ToolbarIcon::Add,       i18n::StrKey::TbAdd,       IDM_CMD_ADD,      false, {}},
                {ToolbarIcon::ExtractTo, i18n::StrKey::TbExtractTo, IDM_CMD_EXTRACT,  false, {}},
                {ToolbarIcon::Test,      i18n::StrKey::TbTest,      IDM_CMD_TEST,     false, {}},
                {ToolbarIcon::View,      i18n::StrKey::TbView,      IDM_CMD_VIEW,     false, {}},
                {ToolbarIcon::Delete,    i18n::StrKey::TbDelete,    IDM_CMD_DELETE,   false, {}},
                {ToolbarIcon::Find,      i18n::StrKey::TbFind,      IDM_TOOLS_FIND,   false, {}},
                {ToolbarIcon::Wizard,    i18n::StrKey::TbWizard,    IDM_TOOLS_WIZARD, false, {}},
                {ToolbarIcon::Info,      i18n::StrKey::TbInfo,      IDM_CMD_INFO,     false, {}},
                {ToolbarIcon::Repair,    i18n::StrKey::TbRepair,    IDM_TOOLS_REPAIR, true,  {}}
            };
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc{};
            GetClientRect(hwnd, &rc);

            HDC mem = CreateCompatibleDC(hdc);
            HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
            HGDIOBJ old = SelectObject(mem, bmp);

            Gdiplus::Graphics g(mem);
            g.Clear(Gdiplus::Color(255, 255, 255, 255));
            Gdiplus::Pen border(Gdiplus::Color(255, 224, 224, 224), 1.0f);
            g.DrawLine(&border, 0.0f, static_cast<float>(rc.bottom - 1),
                       static_cast<float>(rc.right), static_cast<float>(rc.bottom - 1));

            Gdiplus::FontFamily ff(L"Segoe UI");
            Gdiplus::Font label_font(&ff, 9.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
            Gdiplus::SolidBrush txt_br(Gdiplus::Color(255, 24, 24, 24));
            Gdiplus::StringFormat sf;
            sf.SetAlignment(Gdiplus::StringAlignmentCenter);

            int x = 8;
            for (size_t i = 0; st && i < st->buttons.size(); ++i) {
                auto& b = st->buttons[i];
                if (b.sep_before) {
                    Gdiplus::Pen sep(Gdiplus::Color(255, 210, 210, 210), 1.0f);
                    g.DrawLine(&sep, static_cast<float>(x + 4), 8.0f,
                               static_cast<float>(x + 4), static_cast<float>(rc.bottom - 10));
                    x += 10;
                }
                std::wstring lbl = tr_w(b.label_key);
                int bw = (lbl.size() > 7) ? 76 : 64;
                b.rect = {x, 3, x + bw, rc.bottom - 4};

                if (static_cast<int>(i) == st->pressed) {
                    Gdiplus::SolidBrush fill(Gdiplus::Color(255, 204, 228, 247));
                    Gdiplus::Pen pen(Gdiplus::Color(255, 0, 120, 215), 1.0f);
                    draw_rounded_rect(g, Gdiplus::RectF(static_cast<float>(b.rect.left), static_cast<float>(b.rect.top),
                                                        static_cast<float>(bw), static_cast<float>(b.rect.bottom - b.rect.top)),
                                      2.5f, &fill, &pen);
                } else if (static_cast<int>(i) == st->hover) {
                    Gdiplus::SolidBrush fill(Gdiplus::Color(255, 229, 243, 255));
                    Gdiplus::Pen pen(Gdiplus::Color(255, 153, 209, 255), 1.0f);
                    draw_rounded_rect(g, Gdiplus::RectF(static_cast<float>(b.rect.left), static_cast<float>(b.rect.top),
                                                        static_cast<float>(bw), static_cast<float>(b.rect.bottom - b.rect.top)),
                                      2.5f, &fill, &pen);
                }

                float icon_x = static_cast<float>(b.rect.left) + (bw - 38.0f) / 2.0f;
                paint_toolbar_icon(g, b.icon, icon_x, 6.0f, 38.0f);
                Gdiplus::RectF txt_rc(static_cast<float>(b.rect.left), 45.0f, static_cast<float>(bw), 18.0f);
                g.DrawString(lbl.c_str(), -1, &label_font, txt_rc, &sf, &txt_br);
                x += bw + 2;
            }

            BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
            SelectObject(mem, old);
            DeleteObject(bmp);
            DeleteDC(mem);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_MOUSEMOVE:
            if (st) {
                if (!st->tracking_mouse) {
                    TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
                    TrackMouseEvent(&tme);
                    st->tracking_mouse = true;
                }
                POINT pt{static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp))};
                int hit = -1;
                for (size_t i = 0; i < st->buttons.size(); ++i) {
                    if (PtInRect(&st->buttons[i].rect, pt)) { hit = static_cast<int>(i); break; }
                }
                if (hit != st->hover) { st->hover = hit; InvalidateRect(hwnd, nullptr, FALSE); }
            }
            return 0;
        case WM_MOUSELEAVE:
            if (st) {
                st->tracking_mouse = false;
                st->hover = -1;
                st->pressed = -1;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_LBUTTONDOWN:
            if (st) { st->pressed = st->hover; InvalidateRect(hwnd, nullptr, FALSE); }
            return 0;
        case WM_LBUTTONUP:
            if (st) {
                int clicked = st->pressed;
                st->pressed = -1;
                InvalidateRect(hwnd, nullptr, FALSE);
                if (clicked >= 0 && clicked == st->hover) {
                    SendMessageW(st->parent, WM_COMMAND, st->buttons[clicked].cmd_id, 0);
                }
            }
            return 0;
        case WM_NCDESTROY:
            delete st;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

HWND create_app_toolbar(HWND parent, HINSTANCE hinst, int control_id) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = ToolbarWndProc;
        wc.hInstance = hinst;
        wc.lpszClassName = L"OpenRaRToolbar";
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        RegisterClassW(&wc);
        registered = true;
    }
    return CreateWindowW(L"OpenRaRToolbar", L"", WS_CHILD | WS_VISIBLE,
                         0, 0, 800, 66, parent,
                         reinterpret_cast<HMENU>(static_cast<INT_PTR>(control_id)), hinst, nullptr);
}

} // namespace openrar::gui
