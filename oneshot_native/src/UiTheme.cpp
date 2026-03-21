#include "oneshot_native/UiTheme.h"

#include <dwmapi.h>

namespace
{
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif

    constexpr oneshot::ui::Palette kPalette{
        RGB(18, 24, 33),
        RGB(31, 39, 52),
        RGB(86, 103, 128),
        RGB(82, 146, 230),
        RGB(103, 165, 247),
        RGB(60, 120, 200),
        RGB(56, 68, 88),
        RGB(67, 80, 101),
        RGB(75, 90, 114),
        RGB(85, 53, 62),
        RGB(101, 63, 74),
        RGB(119, 74, 86),
        RGB(230, 236, 244),
        RGB(154, 168, 188),
        RGB(12, 18, 26)
    };
}

namespace oneshot::ui
{
    const Palette& GetPalette() noexcept
    {
        return kPalette;
    }

    int ScaleForDpi(int value, UINT dpi) noexcept
    {
        return MulDiv(value, static_cast<int>(dpi == 0 ? 96 : dpi), 96);
    }

    COLORREF BlendColor(COLORREF base, COLORREF overlay, BYTE alpha) noexcept
    {
        const BYTE inverseAlpha = static_cast<BYTE>(255 - alpha);
        return RGB(
            (GetRValue(base) * inverseAlpha + GetRValue(overlay) * alpha) / 255,
            (GetGValue(base) * inverseAlpha + GetGValue(overlay) * alpha) / 255,
            (GetBValue(base) * inverseAlpha + GetBValue(overlay) * alpha) / 255);
    }

    HFONT CreateUiFont(HWND hwnd, int pointSize, int weight)
    {
        const UINT dpi = hwnd ? GetDpiForWindow(hwnd) : 96;
        const int height = -MulDiv(pointSize, static_cast<int>(dpi == 0 ? 96 : dpi), 72);
        return CreateFontW(
            height,
            0,
            0,
            0,
            weight,
            FALSE,
            FALSE,
            FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE,
            L"Segoe UI");
    }

    void FillRoundedRect(HDC dc, const RECT& rect, COLORREF color, int radius)
    {
        HBRUSH brush = CreateSolidBrush(color);
        HRGN region = CreateRoundRectRgn(rect.left, rect.top, rect.right + 1, rect.bottom + 1, radius, radius);
        FillRgn(dc, region, brush);
        DeleteObject(region);
        DeleteObject(brush);
    }

    void FrameRoundedRect(HDC dc, const RECT& rect, COLORREF color, int radius, int thickness)
    {
        HPEN pen = CreatePen(PS_SOLID, thickness, color);
        HGDIOBJ oldPen = SelectObject(dc, pen);
        HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
        RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        DeleteObject(pen);
    }

    void ApplyModernFrame(HWND hwnd, bool darkCaption)
    {
        if (!hwnd)
        {
            return;
        }

        const BOOL enabled = darkCaption ? TRUE : FALSE;
        const DWM_WINDOW_CORNER_PREFERENCE preference = DWMWCP_ROUND;
        (void)DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &enabled, sizeof(enabled));
        (void)DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &preference, sizeof(preference));
    }
}
