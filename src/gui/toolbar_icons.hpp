#pragma once

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

#include <cmath>
#include <memory>

namespace openrar::gui {

enum class ToolbarIcon {
    Add = 0,
    ExtractTo,
    Test,
    View,
    Delete,
    Find,
    Wizard,
    Info,
    Repair
};

inline void draw_rounded_rect(Gdiplus::Graphics& g, Gdiplus::RectF r, float radius,
                              const Gdiplus::Brush* fill, const Gdiplus::Pen* stroke) {
    Gdiplus::GraphicsPath path;
    float d = radius * 2.0f;
    path.AddArc(r.X, r.Y, d, d, 180, 90);
    path.AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
    path.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
    path.AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
    path.CloseFigure();
    if (fill) g.FillPath(fill, &path);
    if (stroke) g.DrawPath(stroke, &path);
}

inline void draw_star(Gdiplus::Graphics& g, float cx, float cy, float r_outer, float r_inner, Gdiplus::Color col) {
    Gdiplus::PointF pts[10];
    for (int i = 0; i < 10; ++i) {
        float a = static_cast<float>(i) * 3.14159265f / 5.0f - 3.14159265f / 2.0f;
        float r = (i % 2 == 0) ? r_outer : r_inner;
        pts[i] = Gdiplus::PointF(cx + std::cos(a) * r, cy + std::sin(a) * r);
    }
    Gdiplus::SolidBrush br(col);
    g.FillPolygon(&br, pts, 10);
}

// Draws any of the 9 Classic-style glossy icons into a [0..size, 0..size] square
inline void paint_toolbar_icon(Gdiplus::Graphics& g, ToolbarIcon icon, float x, float y, float size) {
    g.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);

    Gdiplus::GraphicsState st = g.Save();
    g.TranslateTransform(x, y);
    float s = size / 40.0f;
    g.ScaleTransform(s, s);

    switch (icon) {
        case ToolbarIcon::Add: {
            // Clean flat geometric archive + zipper icon matching assets/openrar.svg
            Gdiplus::SolidBrush tile(Gdiplus::Color(255, 24, 24, 27));
            draw_rounded_rect(g, Gdiplus::RectF(4.0f, 4.0f, 32.0f, 32.0f), 6.5f, &tile, nullptr);
            Gdiplus::SolidBrush body(Gdiplus::Color(255, 244, 244, 245));
            draw_rounded_rect(g, Gdiplus::RectF(9.5f, 10.0f, 21.0f, 20.5f), 2.5f, &body, nullptr);
            Gdiplus::SolidBrush lid(Gdiplus::Color(255, 37, 99, 235));
            draw_rounded_rect(g, Gdiplus::RectF(9.5f, 10.0f, 21.0f, 5.5f), 2.5f, &lid, nullptr);
            g.FillRectangle(&lid, 9.5f, 12.5f, 21.0f, 3.0f);
            // Center zipper track + amber pull
            g.FillRectangle(&tile, 18.2f, 10.0f, 3.6f, 20.5f);
            Gdiplus::SolidBrush amber(Gdiplus::Color(255, 245, 158, 11));
            g.FillRectangle(&amber, 18.2f, 11.2f, 1.8f, 1.8f);
            g.FillRectangle(&amber, 20.0f, 14.0f, 1.8f, 1.8f);
            g.FillRectangle(&amber, 18.2f, 16.8f, 1.8f, 1.8f);
            g.FillRectangle(&amber, 20.0f, 19.6f, 1.8f, 1.8f);
            draw_rounded_rect(g, Gdiplus::RectF(17.0f, 23.2f, 6.0f, 5.2f), 1.4f, &amber, nullptr);
            g.FillRectangle(&tile, 18.8f, 24.8f, 2.4f, 2.0f);
            break;
        }
        case ToolbarIcon::ExtractTo: {
            // Back folder tab
            Gdiplus::RectF back(4.0f, 6.0f, 15.0f, 7.0f);
            Gdiplus::SolidBrush tab_br(Gdiplus::Color(255, 84, 146, 232));
            draw_rounded_rect(g, back, 2.0f, &tab_br, nullptr);
            Gdiplus::RectF body(4.0f, 9.5f, 32.0f, 24.5f);
            Gdiplus::LinearGradientBrush f_br(body, Gdiplus::Color(255, 145, 195, 252),
                                              Gdiplus::Color(255, 68, 132, 224), Gdiplus::LinearGradientModeVertical);
            Gdiplus::Pen f_pen(Gdiplus::Color(255, 42, 92, 178), 1.1f);
            draw_rounded_rect(g, body, 2.5f, &f_br, &f_pen);
            // Inner papers
            Gdiplus::SolidBrush paper(Gdiplus::Color(240, 252, 254, 255));
            g.FillRectangle(&paper, 7.0f, 13.0f, 26.0f, 6.0f);
            // Front folder flap
            Gdiplus::RectF flap(4.0f, 16.0f, 32.0f, 18.0f);
            Gdiplus::LinearGradientBrush fl_br(flap, Gdiplus::Color(255, 194, 224, 255),
                                               Gdiplus::Color(255, 108, 164, 240), Gdiplus::LinearGradientModeVertical);
            draw_rounded_rect(g, flap, 2.2f, &fl_br, &f_pen);
            // Vertical archive bars inside
            Gdiplus::Pen bar_pen(Gdiplus::Color(150, 74, 124, 196), 1.3f);
            for (float bx = 9.0f; bx <= 31.0f; bx += 4.4f) {
                g.DrawLine(&bar_pen, bx, 19.0f, bx, 30.0f);
            }
            // Orange bottom accent bar
            Gdiplus::SolidBrush accent(Gdiplus::Color(255, 240, 104, 34));
            g.FillRectangle(&accent, 23.0f, 29.5f, 10.5f, 3.0f);
            break;
        }
        case ToolbarIcon::Test: {
            // Red clipboard
            Gdiplus::RectF board(7.0f, 6.0f, 26.0f, 30.0f);
            Gdiplus::LinearGradientBrush b_br(board, Gdiplus::Color(255, 224, 72, 62),
                                              Gdiplus::Color(255, 168, 34, 28), Gdiplus::LinearGradientModeVertical);
            Gdiplus::Pen b_pen(Gdiplus::Color(255, 120, 22, 18), 1.1f);
            draw_rounded_rect(g, board, 2.5f, &b_br, &b_pen);
            // White sheet
            Gdiplus::RectF sheet(10.0f, 10.0f, 20.0f, 23.0f);
            Gdiplus::SolidBrush w_br(Gdiplus::Color(255, 250, 250, 252));
            g.FillRectangle(&w_br, sheet);
            // Metallic top clip
            Gdiplus::RectF clip(14.0f, 3.5f, 12.0f, 6.5f);
            Gdiplus::LinearGradientBrush c_br(clip, Gdiplus::Color(255, 230, 234, 240),
                                              Gdiplus::Color(255, 140, 148, 158), Gdiplus::LinearGradientModeVertical);
            Gdiplus::Pen c_pen(Gdiplus::Color(255, 90, 96, 104), 1.0f);
            draw_rounded_rect(g, clip, 1.5f, &c_br, &c_pen);
            // Green checkmark + checkboxes
            Gdiplus::Pen chk(Gdiplus::Color(255, 32, 168, 52), 2.4f);
            chk.SetStartCap(Gdiplus::LineCapRound);
            chk.SetEndCap(Gdiplus::LineCapRound);
            g.DrawLine(&chk, 13.0f, 17.5f, 16.5f, 21.0f);
            g.DrawLine(&chk, 16.5f, 21.0f, 24.5f, 13.0f);
            Gdiplus::Pen ln(Gdiplus::Color(255, 176, 182, 190), 1.4f);
            g.DrawRectangle(&ln, 13.0f, 24.5f, 4.5f, 4.5f);
            g.DrawLine(&ln, 20.0f, 26.8f, 27.0f, 26.8f);
            break;
        }
        case ToolbarIcon::View: {
            // Open book with green cover & red ribbon
            Gdiplus::RectF cover(4.0f, 9.0f, 32.0f, 26.0f);
            Gdiplus::LinearGradientBrush cov_br(cover, Gdiplus::Color(255, 62, 154, 52),
                                                Gdiplus::Color(255, 28, 96, 26), Gdiplus::LinearGradientModeVertical);
            draw_rounded_rect(g, cover, 2.0f, &cov_br, nullptr);
            // Left & right white pages
            Gdiplus::LinearGradientBrush lp(Gdiplus::RectF(6, 7, 14, 25), Gdiplus::Color(255, 255, 255, 255),
                                            Gdiplus::Color(255, 224, 232, 224), Gdiplus::LinearGradientModeHorizontal);
            Gdiplus::LinearGradientBrush rp(Gdiplus::RectF(20, 7, 14, 25), Gdiplus::Color(255, 228, 234, 228),
                                            Gdiplus::Color(255, 255, 255, 255), Gdiplus::LinearGradientModeHorizontal);
            Gdiplus::Pen pp(Gdiplus::Color(255, 130, 150, 130), 0.9f);
            g.FillRectangle(&lp, 6.0f, 7.0f, 14.0f, 25.0f);
            g.DrawRectangle(&pp, 6.0f, 7.0f, 14.0f, 25.0f);
            g.FillRectangle(&rp, 20.0f, 7.0f, 14.0f, 25.0f);
            g.DrawRectangle(&pp, 20.0f, 7.0f, 14.0f, 25.0f);
            // Text lines
            Gdiplus::Pen tl(Gdiplus::Color(255, 175, 185, 175), 1.0f);
            for (float ly = 12.0f; ly <= 27.0f; ly += 3.8f) {
                g.DrawLine(&tl, 8.5f, ly, 17.5f, ly);
                g.DrawLine(&tl, 22.5f, ly, 31.5f, ly);
            }
            // Red bookmark ribbon
            Gdiplus::PointF rib[5] = { {23, 7}, {27.5f, 7}, {27.5f, 19}, {25.25f, 16.5f}, {23, 19} };
            Gdiplus::SolidBrush rbr(Gdiplus::Color(255, 214, 38, 38));
            g.FillPolygon(&rbr, rib, 5);
            break;
        }
        case ToolbarIcon::Delete: {
            // Metallic silver trash can
            Gdiplus::RectF lid(7.0f, 6.5f, 26.0f, 4.5f);
            Gdiplus::RectF handle(15.5f, 3.5f, 9.0f, 3.5f);
            Gdiplus::LinearGradientBrush met(Gdiplus::RectF(8, 10, 24, 26),
                                             Gdiplus::Color(255, 236, 240, 245),
                                             Gdiplus::Color(255, 148, 156, 168),
                                             Gdiplus::LinearGradientModeHorizontal);
            Gdiplus::Pen mp(Gdiplus::Color(255, 96, 104, 114), 1.2f);
            draw_rounded_rect(g, handle, 1.2f, &met, &mp);
            draw_rounded_rect(g, lid, 1.5f, &met, &mp);
            Gdiplus::PointF can[4] = { {9.5f, 11.0f}, {30.5f, 11.0f}, {28.2f, 35.5f}, {11.8f, 35.5f} };
            g.FillPolygon(&met, can, 4);
            g.DrawPolygon(&mp, can, 4);
            // Vertical ribs
            Gdiplus::Pen rib(Gdiplus::Color(255, 116, 124, 136), 1.4f);
            g.DrawLine(&rib, 15.0f, 14.5f, 16.0f, 32.0f);
            g.DrawLine(&rib, 20.0f, 14.5f, 20.0f, 32.0f);
            g.DrawLine(&rib, 25.0f, 14.5f, 24.0f, 32.0f);
            break;
        }
        case ToolbarIcon::Find: {
            // Handle
            Gdiplus::Pen hp(Gdiplus::Color(255, 72, 78, 88), 4.5f);
            hp.SetStartCap(Gdiplus::LineCapRound);
            hp.SetEndCap(Gdiplus::LineCapRound);
            g.DrawLine(&hp, 26.0f, 26.0f, 34.5f, 34.5f);
            // Lens
            Gdiplus::RectF lens(5.5f, 5.5f, 23.0f, 23.0f);
            Gdiplus::LinearGradientBrush lbr(lens, Gdiplus::Color(255, 156, 208, 255),
                                             Gdiplus::Color(255, 38, 96, 206), Gdiplus::LinearGradientModeForwardDiagonal);
            g.FillEllipse(&lbr, lens);
            Gdiplus::Pen rim(Gdiplus::Color(255, 198, 206, 218), 2.6f);
            Gdiplus::Pen rim_out(Gdiplus::Color(255, 86, 98, 118), 1.0f);
            g.DrawEllipse(&rim, lens);
            g.DrawEllipse(&rim_out, lens);
            // Specular highlight
            Gdiplus::SolidBrush spec(Gdiplus::Color(140, 255, 255, 255));
            g.FillEllipse(&spec, 9.5f, 8.5f, 9.0f, 5.5f);
            break;
        }
        case ToolbarIcon::Wizard: {
            // Magic wand (diagonal black stick with white tip)
            Gdiplus::Pen stick(Gdiplus::Color(255, 42, 46, 54), 3.6f);
            stick.SetStartCap(Gdiplus::LineCapRound);
            stick.SetEndCap(Gdiplus::LineCapRound);
            g.DrawLine(&stick, 34.0f, 34.0f, 17.0f, 17.0f);
            Gdiplus::Pen tip(Gdiplus::Color(255, 245, 245, 248), 3.6f);
            tip.SetEndCap(Gdiplus::LineCapRound);
            g.DrawLine(&tip, 19.5f, 19.5f, 15.0f, 15.0f);
            // Colorful stars around wand tip
            draw_star(g, 11.0f, 13.0f, 5.2f, 2.2f, Gdiplus::Color(255, 255, 206, 36));
            draw_star(g, 20.0f, 7.5f,  4.0f, 1.7f, Gdiplus::Color(255, 84, 210, 96));
            draw_star(g, 9.5f,  23.5f, 3.6f, 1.6f, Gdiplus::Color(255, 76, 168, 250));
            draw_star(g, 27.5f, 14.5f, 3.2f, 1.4f, Gdiplus::Color(255, 245, 92, 128));
            break;
        }
        case ToolbarIcon::Info: {
            Gdiplus::RectF c(5.0f, 5.0f, 30.0f, 30.0f);
            Gdiplus::LinearGradientBrush bg(c, Gdiplus::Color(255, 248, 251, 255),
                                            Gdiplus::Color(255, 196, 214, 242), Gdiplus::LinearGradientModeVertical);
            g.FillEllipse(&bg, c);
            Gdiplus::Pen ring(Gdiplus::Color(255, 84, 126, 196), 2.4f);
            g.DrawEllipse(&ring, c);
            // Bold blue 'i'
            Gdiplus::SolidBrush ibr(Gdiplus::Color(255, 32, 82, 188));
            g.FillEllipse(&ibr, 17.6f, 10.5f, 4.8f, 4.8f);
            draw_rounded_rect(g, Gdiplus::RectF(17.8f, 17.5f, 4.4f, 12.0f), 1.4f, &ibr, nullptr);
            break;
        }
        case ToolbarIcon::Repair: {
            // Green archive box with crossed band-aids
            Gdiplus::RectF box(8.0f, 5.0f, 24.0f, 30.0f);
            Gdiplus::LinearGradientBrush gbr(box, Gdiplus::Color(255, 118, 212, 86),
                                             Gdiplus::Color(255, 48, 152, 42), Gdiplus::LinearGradientModeVertical);
            Gdiplus::Pen gpen(Gdiplus::Color(255, 32, 106, 28), 1.1f);
            draw_rounded_rect(g, box, 2.0f, &gbr, &gpen);
            // Yellow top stripe
            Gdiplus::SolidBrush ybr(Gdiplus::Color(255, 246, 214, 72));
            g.FillRectangle(&ybr, 10.5f, 8.0f, 19.0f, 3.0f);
            // Two beige band-aid strips
            auto draw_bandage = [&](float bx, float by, float angle) {
                Gdiplus::GraphicsState bs = g.Save();
                g.TranslateTransform(bx, by);
                g.RotateTransform(angle);
                Gdiplus::RectF br(-8.5f, -3.0f, 17.0f, 6.0f);
                Gdiplus::SolidBrush bfill(Gdiplus::Color(255, 226, 206, 166));
                Gdiplus::Pen bpen(Gdiplus::Color(255, 150, 128, 86), 0.9f);
                draw_rounded_rect(g, br, 2.0f, &bfill, &bpen);
                Gdiplus::SolidBrush pad(Gdiplus::Color(255, 244, 234, 210));
                g.FillRectangle(&pad, -3.5f, -2.0f, 7.0f, 4.0f);
                g.Restore(bs);
            };
            draw_bandage(22.0f, 23.0f, 42.0f);
            draw_bandage(22.0f, 23.0f, -42.0f);
            break;
        }
    }

    g.Restore(st);
}

// Create fallback HICON from the OpenRAR vault icon (primary icon is loaded from assets/openrar.ico via app.rc)
inline HICON create_app_icon(int px = 32) {
    Gdiplus::Bitmap bmp(px, px, PixelFormat32bppARGB);
    Gdiplus::Graphics g(&bmp);
    g.Clear(Gdiplus::Color(0, 0, 0, 0));
    paint_toolbar_icon(g, ToolbarIcon::Add, 0.0f, 0.0f, static_cast<float>(px));
    HICON hicon = nullptr;
    bmp.GetHICON(&hicon);
    return hicon;
}

} // namespace openrar::gui
