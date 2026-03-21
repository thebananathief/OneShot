#pragma once

#include "oneshot_native/Common.h"

namespace oneshot::ui
{
    struct Palette
    {
        COLORREF windowBackground{};
        COLORREF panelBackground{};
        COLORREF panelBorder{};
        COLORREF accent{};
        COLORREF accentHot{};
        COLORREF accentPressed{};
        COLORREF controlBackground{};
        COLORREF controlBackgroundHot{};
        COLORREF controlBackgroundPressed{};
        COLORREF dangerBackground{};
        COLORREF dangerBackgroundHot{};
        COLORREF dangerBackgroundPressed{};
        COLORREF text{};
        COLORREF mutedText{};
        COLORREF canvasBackground{};
    };

    [[nodiscard]] const Palette& GetPalette() noexcept;
    [[nodiscard]] int ScaleForDpi(int value, UINT dpi) noexcept;
    [[nodiscard]] COLORREF BlendColor(COLORREF base, COLORREF overlay, BYTE alpha) noexcept;
    [[nodiscard]] HFONT CreateUiFont(HWND hwnd, int pointSize, int weight = FW_NORMAL);

    void FillRoundedRect(HDC dc, const RECT& rect, COLORREF color, int radius);
    void FrameRoundedRect(HDC dc, const RECT& rect, COLORREF color, int radius, int thickness = 1);
    void ApplyModernFrame(HWND hwnd, bool darkCaption = true);
}
