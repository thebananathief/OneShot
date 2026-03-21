#include "oneshot_native/MarkupEditorWindow.h"

#include <commctrl.h>
#include <windowsx.h>
#include <cmath>
#include <commdlg.h>
#include <dwmapi.h>

namespace
{
    constexpr int kToolbarHeight = 84;
    constexpr int kToolbarGroupHeight = 52;
    constexpr int kMargin = 16;
    constexpr double kMaxZoom = 8.0;
    constexpr double kFitPaddingFactor = 0.99;
    constexpr double kZoomStep = 1.1;

    constexpr int kToolPenId = 2001;
    constexpr int kToolLineId = 2002;
    constexpr int kToolArrowId = 2003;
    constexpr int kToolRectId = 2004;
    constexpr int kToolEllipseId = 2005;
    constexpr int kToolPolygonId = 2006;
    constexpr int kToolTextId = 2007;
    constexpr int kUndoId = 2010;
    constexpr int kRedoId = 2011;
    constexpr int kCopyId = 2012;
    constexpr int kDoneId = 2013;
    constexpr int kCancelId = 2014;
    constexpr int kTextInputId = 2015;
    constexpr int kStrokeColorId = 2016;
    constexpr int kFillColorId = 2017;
    constexpr int kTextColorId = 2018;
    constexpr int kThicknessDownId = 2019;
    constexpr int kThicknessUpId = 2020;
    constexpr int kFillToggleId = 2021;
    constexpr int kFontDownId = 2022;
    constexpr int kFontUpId = 2023;
    constexpr int kFitId = 2024;
    constexpr int kFontComboId = 2025;
    constexpr int kAngleSnapIncrementDegrees = 15;
    constexpr int kPromptEditId = 3001;
    constexpr int kPromptOkId = 3002;
    constexpr int kPromptCancelId = 3003;
    constexpr int kToolbarLabelId = 3100;

    constexpr COLORREF kWindowBackground = RGB(18, 24, 33);
    constexpr COLORREF kToolbarSurface = RGB(31, 39, 52);
    constexpr COLORREF kToolbarPanel = RGB(42, 53, 69);
    constexpr COLORREF kCanvasBackground = RGB(12, 18, 26);
    constexpr COLORREF kCanvasPanel = RGB(28, 36, 49);
    constexpr COLORREF kCanvasBorder = RGB(74, 90, 112);
    constexpr COLORREF kTextColor = RGB(230, 236, 244);
    constexpr COLORREF kMutedTextColor = RGB(154, 168, 188);
    constexpr COLORREF kAccentColor = RGB(82, 146, 230);
    constexpr COLORREF kAccentHotColor = RGB(103, 165, 247);
    constexpr COLORREF kAccentPressedColor = RGB(60, 120, 200);
    constexpr COLORREF kControlSurface = RGB(56, 68, 88);
    constexpr COLORREF kControlSurfaceHot = RGB(67, 80, 101);
    constexpr COLORREF kControlSurfacePressed = RGB(75, 90, 114);
    constexpr COLORREF kControlBorder = RGB(86, 103, 128);
    constexpr COLORREF kDangerSurface = RGB(85, 53, 62);
    constexpr COLORREF kDangerSurfaceHot = RGB(101, 63, 74);
    constexpr COLORREF kDangerSurfacePressed = RGB(119, 74, 86);
    constexpr wchar_t kUiHoverProp[] = L"OneShot.UiHover";

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif

    enum class PromptAction
    {
        Neutral,
        Accent
    };

    struct ToolbarLayout
    {
        RECT toolsGroup{};
        RECT optionsGroup{};
        RECT actionsGroup{};
    };


    RECT MakeRect(int left, int top, int right, int bottom)
    {
        RECT rect{};
        rect.left = left;
        rect.top = top;
        rect.right = right;
        rect.bottom = bottom;
        return rect;
    }

    RECT MakeSizedRect(int left, int top, int width, int height)
    {
        return MakeRect(left, top, left + width, top + height);
    }

    int ScaleForDpi(int value, UINT dpi)
    {
        return MulDiv(value, static_cast<int>(dpi == 0 ? 96 : dpi), 96);
    }

    bool IsWindowHovering(HWND hwnd)
    {
        return GetPropW(hwnd, kUiHoverProp) != nullptr;
    }

    void SetWindowHover(HWND hwnd, bool hover)
    {
        if (hover)
        {
            SetPropW(hwnd, kUiHoverProp, reinterpret_cast<HANDLE>(1));
        }
        else
        {
            RemovePropW(hwnd, kUiHoverProp);
        }
    }

    void ApplyModernWindowFrame(HWND hwnd)
    {
        if (!hwnd)
        {
            return;
        }

        const BOOL enabled = TRUE;
        const DWM_WINDOW_CORNER_PREFERENCE preference = DWMWCP_ROUND;
        (void)DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &enabled, sizeof(enabled));
        (void)DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &preference, sizeof(preference));
    }

    HFONT CreateUiFont(HWND hwnd, int pointSize, int weight = FW_NORMAL)
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

    void FrameRoundedRect(HDC dc, const RECT& rect, COLORREF color, int radius, int thickness = 1)
    {
        HPEN pen = CreatePen(PS_SOLID, thickness, color);
        HGDIOBJ oldPen = SelectObject(dc, pen);
        HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
        RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        DeleteObject(pen);
    }

    void DrawToolbarPanel(HDC dc, const RECT& rect, const wchar_t* label)
    {
        FillRoundedRect(dc, rect, kToolbarPanel, 18);
        FrameRoundedRect(dc, rect, kControlBorder, 18);

        RECT labelRect = rect;
        labelRect.left += 14;
        labelRect.top += 8;
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, kMutedTextColor);
        DrawTextW(dc, label, -1, &labelRect, DT_LEFT | DT_TOP | DT_SINGLELINE);
    }

    ToolbarLayout BuildToolbarLayout(HWND hwnd)
    {
        RECT client{};
        GetClientRect(hwnd, &client);
        const UINT dpi = GetDpiForWindow(hwnd);
        const int margin = ScaleForDpi(12, dpi);
        const int top = ScaleForDpi(16, dpi);
        const int bottom = top + ScaleForDpi(kToolbarGroupHeight, dpi);
        ToolbarLayout layout{};
        layout.toolsGroup = MakeRect(margin, top, margin + ScaleForDpi(470, dpi), bottom);
        layout.optionsGroup = MakeRect(layout.toolsGroup.right + ScaleForDpi(12, dpi), top, layout.toolsGroup.right + ScaleForDpi(12, dpi) + ScaleForDpi(470, dpi), bottom);
        layout.actionsGroup = MakeRect(client.right - margin - ScaleForDpi(300, dpi), top, client.right - margin, bottom);
        return layout;
    }


    LRESULT CALLBACK ThemedButtonProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR)
    {
        switch (message)
        {
        case WM_MOUSEMOVE:
            if (!IsWindowHovering(hwnd))
            {
                TRACKMOUSEEVENT track{};
                track.cbSize = sizeof(track);
                track.dwFlags = TME_LEAVE;
                track.hwndTrack = hwnd;
                TrackMouseEvent(&track);
                SetWindowHover(hwnd, true);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            break;
        case WM_MOUSELEAVE:
            SetWindowHover(hwnd, false);
            InvalidateRect(hwnd, nullptr, FALSE);
            break;
        case WM_NCDESTROY:
            RemoveWindowSubclass(hwnd, ThemedButtonProc, 0);
            RemovePropW(hwnd, kUiHoverProp);
            break;
        default:
            break;
        }

        return DefSubclassProc(hwnd, message, wParam, lParam);
    }

    struct TextPromptState
    {
        HWND hwnd{nullptr};
        HWND edit{nullptr};
        HFONT uiFont{nullptr};
        bool finished{false};
        bool accepted{false};
        std::wstring value;
    };
}

namespace oneshot
{
    struct MarkupEditorWindow::State
    {
        HWND hwnd{nullptr};
        HWND canvas{nullptr};
        HWND textInput{nullptr};
        Tool tool{Tool::Pen};
        CapturedImage currentImage;
        std::optional<CapturedImage> resultImage;
        std::vector<HBITMAP> undoStack;
        std::vector<HBITMAP> redoStack;
        RECT imageRect{};
        bool isDrawing{false};
        bool previewActive{false};
        POINT dragStart{};
        POINT dragCurrent{};
        std::vector<POINT> polygonPoints;
        bool polygonInProgress{false};
        COLORREF strokeColor{RGB(255, 0, 0)};
        COLORREF fillColor{RGB(255, 224, 224)};
        COLORREF fontColor{RGB(255, 0, 0)};
        int thickness{3};
        int fontSize{22};
        bool fillShapes{true};
        bool finished{false};
        OutputService* outputService{nullptr};
        COLORREF customColors[16]{};
        HWND fontCombo{nullptr};
        double zoom{1.0};
        double minimumZoom{1.0};
        double viewportOffsetX{0.0};
        double viewportOffsetY{0.0};
        bool hasInitialFit{false};
        bool userAdjustedViewport{false};
        bool middlePanning{false};
        POINT middlePanStart{};
        double middlePanOffsetX{0.0};
        double middlePanOffsetY{0.0};
        std::wstring fontFace{L"Segoe UI"};
        HFONT uiFont{nullptr};
        HFONT uiBoldFont{nullptr};
        HDC canvasBackBufferDc{nullptr};
        HBITMAP canvasBackBufferBitmap{nullptr};
        HGDIOBJ canvasBackBufferOldBitmap{nullptr};
        int canvasBackBufferWidth{0};
        int canvasBackBufferHeight{0};
    };

    MarkupEditorWindow::MarkupEditorWindow(OutputService& outputService)
        : _outputService(outputService)
    {
    }

    MarkupEditorWindow::~MarkupEditorWindow() = default;

    static bool ChooseEditorColor(HWND owner, COLORREF& color, COLORREF customColors[16])
    {
        CHOOSECOLORW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = owner;
        dialog.rgbResult = color;
        dialog.lpCustColors = customColors;
        dialog.Flags = CC_FULLOPEN | CC_RGBINIT;
        if (!ChooseColorW(&dialog))
        {
            return false;
        }

        color = dialog.rgbResult;
        return true;
    }

    static POINT SnapLineEndpoint(POINT start, POINT end)
    {
        const double dx = static_cast<double>(end.x - start.x);
        const double dy = static_cast<double>(end.y - start.y);
        const double distance = std::sqrt((dx * dx) + (dy * dy));
        if (distance <= 0.0)
        {
            return end;
        }

        const double angle = std::atan2(dy, dx);
        const double increment = (3.14159265358979323846 * kAngleSnapIncrementDegrees) / 180.0;
        const double snappedAngle = std::round(angle / increment) * increment;
        POINT point{};
        point.x = static_cast<LONG>(std::round(start.x + (distance * std::cos(snappedAngle))));
        point.y = static_cast<LONG>(std::round(start.y + (distance * std::sin(snappedAngle))));
        return point;
    }

    static RECT NormalizeShapeRect(POINT start, POINT end, bool keepAspectRatio)
    {
        RECT rect{};
        if (!keepAspectRatio)
        {
            rect.left = std::min(start.x, end.x);
            rect.top = std::min(start.y, end.y);
            rect.right = std::max(start.x, end.x);
            rect.bottom = std::max(start.y, end.y);
            return rect;
        }

        const LONG side = std::max(std::abs(end.x - start.x), std::abs(end.y - start.y));
        rect.left = end.x < start.x ? start.x - side : start.x;
        rect.top = end.y < start.y ? start.y - side : start.y;
        rect.right = rect.left + side;
        rect.bottom = rect.top + side;
        return rect;
    }

    static RECT GetCanvasClientRect(HWND hwnd)
    {
        RECT client{};
        GetClientRect(hwnd, &client);
        client.left += kMargin;
        client.top += kMargin;
        client.right -= kMargin;
        client.bottom -= kMargin;
        return client;
    }

    static double ComputeFitZoom(HWND canvas, const CapturedImage& image)
    {
        RECT client = GetCanvasClientRect(canvas);
        const int availableWidth = std::max(1, static_cast<int>(client.right - client.left));
        const int availableHeight = std::max(1, static_cast<int>(client.bottom - client.top));
        const double scaleX = static_cast<double>(availableWidth) / static_cast<double>(image.width);
        const double scaleY = static_cast<double>(availableHeight) / static_cast<double>(image.height);
        const double fit = std::min(scaleX, scaleY) * kFitPaddingFactor;
        return std::clamp(fit, 0.05, kMaxZoom);
    }

    static void ClampViewportOffsets(MarkupEditorWindow::State& state)
    {
        if (!state.canvas)
        {
            return;
        }

        RECT client = GetCanvasClientRect(state.canvas);
        const double clientWidth = std::max(1.0, static_cast<double>(client.right - client.left));
        const double clientHeight = std::max(1.0, static_cast<double>(client.bottom - client.top));
        const double contentWidth = state.currentImage.width * state.zoom;
        const double contentHeight = state.currentImage.height * state.zoom;
        const double maxOffsetX = std::max(0.0, contentWidth - clientWidth);
        const double maxOffsetY = std::max(0.0, contentHeight - clientHeight);

        if (contentWidth <= clientWidth)
        {
            state.viewportOffsetX = 0.0;
        }
        else
        {
            state.viewportOffsetX = std::clamp(state.viewportOffsetX, 0.0, maxOffsetX);
        }

        if (contentHeight <= clientHeight)
        {
            state.viewportOffsetY = 0.0;
        }
        else
        {
            state.viewportOffsetY = std::clamp(state.viewportOffsetY, 0.0, maxOffsetY);
        }
    }

    static RECT ComputeImageRect(MarkupEditorWindow::State& state)
    {
        RECT client = GetCanvasClientRect(state.canvas);
        const double clientWidth = std::max(1.0, static_cast<double>(client.right - client.left));
        const double clientHeight = std::max(1.0, static_cast<double>(client.bottom - client.top));
        const double drawWidth = std::max(1.0, state.currentImage.width * state.zoom);
        const double drawHeight = std::max(1.0, state.currentImage.height * state.zoom);
        const double hostOriginX = std::max(0.0, (clientWidth - drawWidth) / 2.0);
        const double hostOriginY = std::max(0.0, (clientHeight - drawHeight) / 2.0);

        RECT rect{};
        rect.left = static_cast<LONG>(std::lround(client.left + hostOriginX - state.viewportOffsetX));
        rect.top = static_cast<LONG>(std::lround(client.top + hostOriginY - state.viewportOffsetY));
        rect.right = static_cast<LONG>(std::lround(rect.left + drawWidth));
        rect.bottom = static_cast<LONG>(std::lround(rect.top + drawHeight));
        return rect;
    }

    static void FitImageToViewport(MarkupEditorWindow::State& state)
    {
        if (!state.canvas)
        {
            return;
        }

        state.minimumZoom = ComputeFitZoom(state.canvas, state.currentImage);
        state.zoom = state.minimumZoom;
        state.viewportOffsetX = 0.0;
        state.viewportOffsetY = 0.0;
        state.hasInitialFit = true;
        state.imageRect = ComputeImageRect(state);
    }

    static void UpdateZoomBounds(MarkupEditorWindow::State& state, bool forceFitIfAtMinimum)
    {
        if (!state.canvas)
        {
            return;
        }

        const bool wasAtMinimum = std::abs(state.zoom - state.minimumZoom) < 0.0001;
        state.minimumZoom = ComputeFitZoom(state.canvas, state.currentImage);
        if (!state.hasInitialFit)
        {
            FitImageToViewport(state);
        }
        else
        {
            state.zoom = std::clamp(state.zoom, state.minimumZoom, kMaxZoom);
            ClampViewportOffsets(state);
            if (forceFitIfAtMinimum && wasAtMinimum)
            {
                FitImageToViewport(state);
            }
            else
            {
                state.imageRect = ComputeImageRect(state);
            }
        }
    }

    static POINT ImagePointToClientPoint(const RECT& imageRect, double zoom, POINT point)
    {
        POINT mapped{};
        mapped.x = static_cast<LONG>(std::lround(imageRect.left + (point.x * zoom)));
        mapped.y = static_cast<LONG>(std::lround(imageRect.top + (point.y * zoom)));
        return mapped;
    }

    static void DrawPolygonPreview(HDC hdc, const RECT& canvasRect, const CapturedImage& image, double zoom, const std::vector<POINT>& polygonPoints, std::optional<POINT> currentPoint)
    {
        if (polygonPoints.empty())
        {
            return;
        }

        std::vector<POINT> preview;
        preview.reserve(polygonPoints.size() + (currentPoint.has_value() ? 1 : 0));
        for (const auto point : polygonPoints)
        {
            preview.push_back(ImagePointToClientPoint(canvasRect, zoom, point));
        }

        if (currentPoint.has_value())
        {
            preview.push_back(ImagePointToClientPoint(canvasRect, zoom, *currentPoint));
        }

        if (preview.size() >= 2)
        {
            Polyline(hdc, preview.data(), static_cast<int>(preview.size()));
        }
    }

    static POINT ClampPointToImage(const RECT& imageRect, POINT point)
    {
        point.x = static_cast<LONG>(std::clamp(static_cast<int>(point.x), static_cast<int>(imageRect.left), static_cast<int>(imageRect.right - 1)));
        point.y = static_cast<LONG>(std::clamp(static_cast<int>(point.y), static_cast<int>(imageRect.top), static_cast<int>(imageRect.bottom - 1)));
        return point;
    }

    static POINT ClientToImagePoint(const RECT& imageRect, const CapturedImage& image, double zoom, POINT point)
    {
        point = ClampPointToImage(imageRect, point);
        POINT mapped{};
        mapped.x = static_cast<int>(std::round((point.x - imageRect.left) / std::max(0.0001, zoom)));
        mapped.y = static_cast<int>(std::round((point.y - imageRect.top) / std::max(0.0001, zoom)));
        mapped.x = static_cast<LONG>(std::clamp(static_cast<int>(mapped.x), 0, image.width - 1));
        mapped.y = static_cast<LONG>(std::clamp(static_cast<int>(mapped.y), 0, image.height - 1));
        return mapped;
    }

    static bool ScreenToCanvasClient(HWND canvas, LPARAM lParam, POINT& point)
    {
        point.x = GET_X_LPARAM(lParam);
        point.y = GET_Y_LPARAM(lParam);
        return ScreenToClient(canvas, &point) != FALSE;
    }

    static int CALLBACK EnumFontFamiliesProc(const LOGFONTW* logFont, const TEXTMETRICW*, DWORD fontType, LPARAM lParam)
    {
        if ((fontType & RASTER_FONTTYPE) != 0)
        {
            return 1;
        }

        auto* fonts = reinterpret_cast<std::vector<std::wstring>*>(lParam);
        const std::wstring face = logFont->lfFaceName;
        if (std::find(fonts->begin(), fonts->end(), face) == fonts->end())
        {
            fonts->push_back(face);
        }

        return 1;
    }

    static void PopulateFontCombo(HWND combo, const std::wstring& selectedFont)
    {
        std::vector<std::wstring> fonts;
        HDC screenDc = GetDC(nullptr);
        LOGFONTW logFont{};
        logFont.lfCharSet = DEFAULT_CHARSET;
        EnumFontFamiliesExW(screenDc, &logFont, reinterpret_cast<FONTENUMPROCW>(EnumFontFamiliesProc), reinterpret_cast<LPARAM>(&fonts), 0);
        ReleaseDC(nullptr, screenDc);

        std::sort(fonts.begin(), fonts.end());
        for (const auto& font : fonts)
        {
            SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(font.c_str()));
        }

        const LRESULT selection = SendMessageW(combo, CB_SELECTSTRING, static_cast<WPARAM>(-1), reinterpret_cast<LPARAM>(selectedFont.c_str()));
        if (selection == CB_ERR && !fonts.empty())
        {
            SendMessageW(combo, CB_SETCURSEL, 0, 0);
        }
    }

    static std::vector<int> GetMarkupButtonIds()
    {
        return {
            kToolPenId, kToolLineId, kToolArrowId, kToolRectId, kToolEllipseId, kToolPolygonId, kToolTextId,
            kStrokeColorId, kFillColorId, kTextColorId, kFillToggleId, kThicknessDownId, kThicknessUpId,
            kFontDownId, kFontUpId, kFitId, kUndoId, kRedoId, kCopyId, kDoneId, kCancelId,
            kPromptOkId, kPromptCancelId
        };
    }

    static void ConfigureThemedButton(HWND button)
    {
        if (!button)
        {
            return;
        }

        LONG_PTR style = GetWindowLongPtrW(button, GWL_STYLE);
        style &= ~(BS_TYPEMASK);
        style |= BS_OWNERDRAW;
        SetWindowLongPtrW(button, GWL_STYLE, style);
        SetWindowSubclass(button, ThemedButtonProc, 0, 0);
        InvalidateRect(button, nullptr, TRUE);
    }

    static void ApplyToolbarFonts(MarkupEditorWindow::State& state)
    {
        if (!state.hwnd)
        {
            return;
        }

        if (state.uiFont)
        {
            DeleteObject(state.uiFont);
        }
        if (state.uiBoldFont)
        {
            DeleteObject(state.uiBoldFont);
        }

        state.uiFont = CreateUiFont(state.hwnd, 10);
        state.uiBoldFont = CreateUiFont(state.hwnd, 10, FW_SEMIBOLD);

        for (const int id : GetMarkupButtonIds())
        {
            HWND control = GetDlgItem(state.hwnd, id);
            if (!control)
            {
                continue;
            }

            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(state.uiFont), TRUE);
            ConfigureThemedButton(control);
        }

        if (state.fontCombo)
        {
            SendMessageW(state.fontCombo, WM_SETFONT, reinterpret_cast<WPARAM>(state.uiFont), TRUE);
        }
    }

    static void LayoutToolbarControls(MarkupEditorWindow::State& state)
    {
        if (!state.hwnd)
        {
            return;
        }

        const UINT dpi = GetDpiForWindow(state.hwnd);
        const ToolbarLayout toolbar = BuildToolbarLayout(state.hwnd);
        const int buttonTop = toolbar.toolsGroup.top + ScaleForDpi(18, dpi);
        const int buttonHeight = ScaleForDpi(24, dpi);
        const int buttonGap = ScaleForDpi(6, dpi);
        int left = toolbar.toolsGroup.left + ScaleForDpi(12, dpi);

        const auto placeButton = [&](int id, int width)
        {
            HWND control = GetDlgItem(state.hwnd, id);
            if (!control)
            {
                return;
            }

            MoveWindow(control, left, buttonTop, ScaleForDpi(width, dpi), buttonHeight, TRUE);
            left += ScaleForDpi(width, dpi) + buttonGap;
        };

        placeButton(kToolPenId, 54);
        placeButton(kToolLineId, 54);
        placeButton(kToolArrowId, 60);
        placeButton(kToolRectId, 54);
        placeButton(kToolEllipseId, 68);
        placeButton(kToolPolygonId, 56);
        placeButton(kToolTextId, 54);

        left = toolbar.optionsGroup.left + ScaleForDpi(12, dpi);
        placeButton(kStrokeColorId, 76);
        placeButton(kFillColorId, 62);
        placeButton(kTextColorId, 66);
        placeButton(kFillToggleId, 52);
        placeButton(kThicknessDownId, 40);
        placeButton(kThicknessUpId, 40);
        placeButton(kFontDownId, 40);
        placeButton(kFontUpId, 40);
        placeButton(kFitId, 46);

        HWND combo = state.fontCombo;
        if (combo)
        {
            MoveWindow(combo, toolbar.actionsGroup.left + ScaleForDpi(12, dpi), buttonTop, ScaleForDpi(148, dpi), ScaleForDpi(280, dpi), TRUE);
        }

        int right = toolbar.actionsGroup.right - ScaleForDpi(12, dpi);
        const auto placeActionButton = [&](int id, int width)
        {
            HWND control = GetDlgItem(state.hwnd, id);
            if (!control)
            {
                return;
            }

            const int scaledWidth = ScaleForDpi(width, dpi);
            right -= scaledWidth;
            MoveWindow(control, right, buttonTop, scaledWidth, buttonHeight, TRUE);
            right -= buttonGap;
        };

        placeActionButton(kCancelId, 66);
        placeActionButton(kDoneId, 58);
        placeActionButton(kCopyId, 58);
        placeActionButton(kRedoId, 58);
        placeActionButton(kUndoId, 58);

        if (state.canvas)
        {
            const int canvasTop = toolbar.toolsGroup.bottom + ScaleForDpi(16, dpi);
            RECT client{};
            GetClientRect(state.hwnd, &client);
            MoveWindow(state.canvas, ScaleForDpi(12, dpi), canvasTop, client.right - ScaleForDpi(24, dpi), client.bottom - canvasTop - ScaleForDpi(12, dpi), TRUE);
        }
    }

    static void DrawButtonText(HDC dc, const RECT& rect, const std::wstring& text, COLORREF color)
    {
        RECT textRect = rect;
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, color);
        DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    static std::wstring FormatColorLabel(const wchar_t* label, COLORREF color)
    {
        wchar_t buffer[64]{};
        swprintf_s(buffer, L"%ls #%02X%02X%02X", label, GetRValue(color), GetGValue(color), GetBValue(color));
        return buffer;
    }

    static void DrawColorSwatch(HDC dc, const RECT& rect, COLORREF color)
    {
        RECT swatch = rect;
        swatch.left += 8;
        swatch.top += 6;
        swatch.bottom -= 6;
        swatch.right = swatch.left + (swatch.bottom - swatch.top);
        FillRoundedRect(dc, swatch, color, 8);
        FrameRoundedRect(dc, swatch, RGB(230, 236, 244), 8);
    }

    static void DrawThemedButton(const MarkupEditorWindow::State& state, const DRAWITEMSTRUCT& draw)
    {
        RECT rect = draw.rcItem;
        const bool hover = IsWindowHovering(draw.hwndItem) || (draw.itemState & ODS_HOTLIGHT) != 0;
        const bool pressed = (draw.itemState & ODS_SELECTED) != 0;
        const bool disabled = (draw.itemState & ODS_DISABLED) != 0;
        const bool focused = (draw.itemState & ODS_FOCUS) != 0;
        const bool checked = SendMessageW(draw.hwndItem, BM_GETCHECK, 0, 0) == BST_CHECKED;

        COLORREF background = kControlSurface;
        COLORREF border = kControlBorder;
        COLORREF text = kTextColor;

        if (draw.CtlID == kDoneId || draw.CtlID == kPromptOkId)
        {
            background = kAccentColor;
            border = kAccentHotColor;
        }
        else if (draw.CtlID == kCancelId)
        {
            background = kDangerSurface;
            border = kDangerSurfaceHot;
        }
        else if (checked)
        {
            background = kAccentColor;
            border = kAccentHotColor;
        }

        if (hover)
        {
            if (draw.CtlID == kCancelId)
            {
                background = kDangerSurfaceHot;
            }
            else if (checked || draw.CtlID == kDoneId || draw.CtlID == kPromptOkId)
            {
                background = kAccentHotColor;
            }
            else
            {
                background = kControlSurfaceHot;
            }
        }

        if (pressed)
        {
            if (draw.CtlID == kCancelId)
            {
                background = kDangerSurfacePressed;
            }
            else if (checked || draw.CtlID == kDoneId || draw.CtlID == kPromptOkId)
            {
                background = kAccentPressedColor;
            }
            else
            {
                background = kControlSurfacePressed;
            }
        }

        if (disabled)
        {
            background = RGB(46, 54, 67);
            text = RGB(122, 132, 147);
        }

        FillRoundedRect(draw.hDC, rect, background, 14);
        FrameRoundedRect(draw.hDC, rect, border, 14);

        std::wstring label;
        if (draw.CtlID == kStrokeColorId)
        {
            DrawColorSwatch(draw.hDC, rect, state.strokeColor);
            label = FormatColorLabel(L"Stroke", state.strokeColor);
        }
        else if (draw.CtlID == kFillColorId)
        {
            DrawColorSwatch(draw.hDC, rect, state.fillColor);
            label = FormatColorLabel(L"Fill", state.fillColor);
        }
        else if (draw.CtlID == kTextColorId)
        {
            DrawColorSwatch(draw.hDC, rect, state.fontColor);
            label = FormatColorLabel(L"Text", state.fontColor);
        }
        else if (draw.CtlID == kFillToggleId)
        {
            label = state.fillShapes ? L"Fill On" : L"Fill Off";
        }
        else if (draw.CtlID == kThicknessDownId)
        {
            label = L"T-";
        }
        else if (draw.CtlID == kThicknessUpId)
        {
            label = L"T+";
        }
        else if (draw.CtlID == kFontDownId)
        {
            label = L"F-";
        }
        else if (draw.CtlID == kFontUpId)
        {
            label = L"F+";
        }
        else
        {
            wchar_t buffer[128]{};
            GetWindowTextW(draw.hwndItem, buffer, static_cast<int>(std::size(buffer)));
            label = buffer;
        }

        if (draw.CtlID == kStrokeColorId || draw.CtlID == kFillColorId || draw.CtlID == kTextColorId)
        {
            RECT textRect = rect;
            textRect.left += 30;
            SetBkMode(draw.hDC, TRANSPARENT);
            SetTextColor(draw.hDC, text);
            DrawTextW(draw.hDC, label.c_str(), static_cast<int>(label.size()), &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
        else
        {
            DrawButtonText(draw.hDC, rect, label, text);
        }

        if (focused)
        {
            RECT focusRect = rect;
            InflateRect(&focusRect, -4, -4);
            DrawFocusRect(draw.hDC, &focusRect);
        }
    }

    static LRESULT CALLBACK TextPromptProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        auto* state = reinterpret_cast<TextPromptState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

        if (message == WM_NCCREATE)
        {
            const auto* createStruct = reinterpret_cast<LPCREATESTRUCTW>(lParam);
            state = static_cast<TextPromptState*>(createStruct->lpCreateParams);
            state->hwnd = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
            return TRUE;
        }

        if (!state)
        {
            return DefWindowProcW(hwnd, message, wParam, lParam);
        }

        switch (message)
        {
        case WM_CREATE:
            ApplyModernWindowFrame(hwnd);
            state->edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"Edit", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP, 16, 20, 316, 28, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPromptEditId)), GetModuleHandleW(nullptr), nullptr);
            {
                HWND ok = CreateWindowExW(0, L"Button", L"Insert", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP, 168, 64, 78, 30, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPromptOkId)), GetModuleHandleW(nullptr), nullptr);
                HWND cancel = CreateWindowExW(0, L"Button", L"Cancel", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, 254, 64, 78, 30, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPromptCancelId)), GetModuleHandleW(nullptr), nullptr);
                state->uiFont = CreateUiFont(hwnd, 10);
                SendMessageW(state->edit, WM_SETFONT, reinterpret_cast<WPARAM>(state->uiFont), TRUE);
                SendMessageW(ok, WM_SETFONT, reinterpret_cast<WPARAM>(state->uiFont), TRUE);
                SendMessageW(cancel, WM_SETFONT, reinterpret_cast<WPARAM>(state->uiFont), TRUE);
                ConfigureThemedButton(ok);
                ConfigureThemedButton(cancel);
            }
            SetFocus(state->edit);
            return 0;
        case WM_ERASEBKGND:
            return TRUE;
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT:
        {
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetBkColor(dc, kToolbarSurface);
            SetTextColor(dc, kTextColor);
            static HBRUSH brush = CreateSolidBrush(kToolbarSurface);
            return reinterpret_cast<INT_PTR>(brush);
        }
        case WM_DRAWITEM:
        {
            auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
            if (!draw)
            {
                break;
            }

            MarkupEditorWindow::State fakeState{};
            fakeState.strokeColor = kAccentColor;
            fakeState.fillColor = RGB(120, 156, 204);
            fakeState.fontColor = kTextColor;
            DrawThemedButton(fakeState, *draw);
            return TRUE;
        }
        case WM_PAINT:
        {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(hwnd, &paint);
            RECT client{};
            GetClientRect(hwnd, &client);
            HBRUSH brush = CreateSolidBrush(kWindowBackground);
            FillRect(dc, &client, brush);
            DeleteObject(brush);
            RECT labelRect = client;
            labelRect.left = 18;
            labelRect.top = 8;
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, kMutedTextColor);
            DrawTextW(dc, L"Add annotation text", -1, &labelRect, DT_LEFT | DT_TOP | DT_SINGLELINE);
            EndPaint(hwnd, &paint);
            return 0;
        }
        case WM_COMMAND:
            switch (LOWORD(wParam))
            {
            case kPromptOkId:
            {
                wchar_t buffer[512]{};
                GetWindowTextW(state->edit, buffer, static_cast<int>(std::size(buffer)));
                state->value = buffer;
                state->accepted = true;
                state->finished = true;
                DestroyWindow(hwnd);
                return 0;
            }
            case kPromptCancelId:
                state->finished = true;
                DestroyWindow(hwnd);
                return 0;
            default:
                break;
            }
            break;
        case WM_CLOSE:
            state->finished = true;
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            if (state->uiFont)
            {
                DeleteObject(state->uiFont);
                state->uiFont = nullptr;
            }
            return 0;
        default:
            break;
        }

        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    static std::optional<std::wstring> PromptForText(HWND owner)
    {
        WNDCLASSW windowClass{};
        windowClass.lpfnWndProc = TextPromptProc;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.lpszClassName = L"OneShotNative.TextPrompt";
        windowClass.hCursor = LoadCursorW(nullptr, IDC_IBEAM);
        RegisterClassW(&windowClass);

        RECT ownerRect{};
        if (!GetWindowRect(owner, &ownerRect))
        {
            ownerRect = MakeRect(CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT + 324, CW_USEDEFAULT + 100);
        }

        TextPromptState state{};
        HWND window = CreateWindowExW(
            WS_EX_DLGMODALFRAME,
            windowClass.lpszClassName,
            L"Add Text",
            WS_CAPTION | WS_POPUPWINDOW | WS_VISIBLE,
            ownerRect.left + std::max(0L, ((ownerRect.right - ownerRect.left) - 356L) / 2L),
            ownerRect.top + std::max(0L, ((ownerRect.bottom - ownerRect.top) - 116L) / 2L),
            356,
            116,
            owner,
            nullptr,
            GetModuleHandleW(nullptr),
            &state);

        if (!window)
        {
            return std::nullopt;
        }

        EnableWindow(owner, FALSE);
        ShowWindow(window, SW_SHOW);
        UpdateWindow(window);

        MSG message{};
        while (!state.finished && GetMessageW(&message, nullptr, 0, 0))
        {
            if (!IsDialogMessageW(window, &message))
            {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }

        EnableWindow(owner, TRUE);
        SetActiveWindow(owner);

        if (!state.accepted || state.value.empty())
        {
            return std::nullopt;
        }

        return state.value;
    }

    void MarkupEditorWindow::ClearBitmapStack(std::vector<HBITMAP>& stack)
    {
        for (auto bitmap : stack)
        {
            if (bitmap)
            {
                DeleteObject(bitmap);
            }
        }

        stack.clear();
    }

    HBITMAP MarkupEditorWindow::CloneBitmap(HBITMAP source)
    {
        return static_cast<HBITMAP>(CopyImage(source, IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION));
    }

    void MarkupEditorWindow::DrawArrow(HDC dc, POINT start, POINT end, COLORREF color, int thickness)
    {
        HPEN pen = CreatePen(PS_SOLID, thickness, color);
        HGDIOBJ oldPen = SelectObject(dc, pen);
        MoveToEx(dc, start.x, start.y, nullptr);
        LineTo(dc, end.x, end.y);

        const double angle = std::atan2(static_cast<double>(end.y - start.y), static_cast<double>(end.x - start.x));
        const double headLength = std::max(10.0, thickness * 4.0);
        const double wingAngle = 3.14159265358979323846 / 7.0;

        POINT head[3]{};
        head[0] = end;
        head[1].x = static_cast<LONG>(std::round(end.x - (headLength * std::cos(angle - wingAngle))));
        head[1].y = static_cast<LONG>(std::round(end.y - (headLength * std::sin(angle - wingAngle))));
        head[2].x = static_cast<LONG>(std::round(end.x - (headLength * std::cos(angle + wingAngle))));
        head[2].y = static_cast<LONG>(std::round(end.y - (headLength * std::sin(angle + wingAngle))));

        HBRUSH brush = CreateSolidBrush(color);
        HGDIOBJ oldBrush = SelectObject(dc, brush);
        Polygon(dc, head, 3);
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        DeleteObject(brush);
        DeleteObject(pen);
    }

    static void CommitPreview(MarkupEditorWindow::State& state, POINT startPoint, POINT endPoint)
    {
        HDC screenDc = GetDC(nullptr);
        HDC memoryDc = CreateCompatibleDC(screenDc);
        HGDIOBJ previous = SelectObject(memoryDc, state.currentImage.bitmap);

        HPEN pen = CreatePen(PS_SOLID, state.thickness, state.strokeColor);
        HGDIOBJ oldPen = SelectObject(memoryDc, pen);
        HBRUSH fillBrush = CreateSolidBrush(state.fillColor);
        HGDIOBJ oldBrush = SelectObject(memoryDc, state.fillShapes ? fillBrush : GetStockObject(HOLLOW_BRUSH));

        switch (state.tool)
        {
        case MarkupEditorWindow::Tool::Line:
            MoveToEx(memoryDc, startPoint.x, startPoint.y, nullptr);
            LineTo(memoryDc, endPoint.x, endPoint.y);
            break;
        case MarkupEditorWindow::Tool::Arrow:
            SelectObject(memoryDc, oldPen);
            DeleteObject(pen);
            SelectObject(memoryDc, oldBrush);
            DeleteObject(fillBrush);
            MarkupEditorWindow::DrawArrow(memoryDc, startPoint, endPoint, state.strokeColor, state.thickness);
            pen = nullptr;
            break;
        case MarkupEditorWindow::Tool::Rectangle:
        {
            RECT rect = NormalizeShapeRect(startPoint, endPoint, false);
            Rectangle(memoryDc, rect.left, rect.top, rect.right, rect.bottom);
            break;
        }
        case MarkupEditorWindow::Tool::Ellipse:
        {
            RECT rect = NormalizeShapeRect(startPoint, endPoint, false);
            Ellipse(memoryDc, rect.left, rect.top, rect.right, rect.bottom);
            break;
        }
        case MarkupEditorWindow::Tool::Polygon:
            if (state.polygonPoints.size() >= 3)
            {
                Polygon(memoryDc, state.polygonPoints.data(), static_cast<int>(state.polygonPoints.size()));
            }
            break;
        default:
            break;
        }

        SelectObject(memoryDc, oldBrush);
        DeleteObject(fillBrush);
        if (pen)
        {
            SelectObject(memoryDc, oldPen);
            DeleteObject(pen);
        }
        SelectObject(memoryDc, previous);
        DeleteDC(memoryDc);
        ReleaseDC(nullptr, screenDc);
    }

    static void PushUndoState(MarkupEditorWindow::State& state)
    {
        if (HBITMAP snapshot = MarkupEditorWindow::CloneBitmap(state.currentImage.bitmap))
        {
            state.undoStack.push_back(snapshot);
            MarkupEditorWindow::ClearBitmapStack(state.redoStack);
        }
    }

    static void RestoreBitmap(MarkupEditorWindow::State& state, HBITMAP snapshot)
    {
        if (!snapshot)
        {
            return;
        }

        state.currentImage.Reset();
        state.currentImage.bitmap = snapshot;
    }

    static void ReleaseCanvasBackBuffer(MarkupEditorWindow::State& state)
    {
        if (state.canvasBackBufferDc)
        {
            if (state.canvasBackBufferOldBitmap)
            {
                SelectObject(state.canvasBackBufferDc, state.canvasBackBufferOldBitmap);
                state.canvasBackBufferOldBitmap = nullptr;
            }
            if (state.canvasBackBufferBitmap)
            {
                DeleteObject(state.canvasBackBufferBitmap);
                state.canvasBackBufferBitmap = nullptr;
            }
            DeleteDC(state.canvasBackBufferDc);
            state.canvasBackBufferDc = nullptr;
        }

        state.canvasBackBufferWidth = 0;
        state.canvasBackBufferHeight = 0;
    }

    static bool EnsureCanvasBackBuffer(MarkupEditorWindow::State& state)
    {
        if (!state.canvas)
        {
            return false;
        }

        RECT client{};
        GetClientRect(state.canvas, &client);
        const int width = std::max(1, static_cast<int>(client.right - client.left));
        const int height = std::max(1, static_cast<int>(client.bottom - client.top));

        if (state.canvasBackBufferDc &&
            state.canvasBackBufferBitmap &&
            state.canvasBackBufferWidth == width &&
            state.canvasBackBufferHeight == height)
        {
            return true;
        }

        ReleaseCanvasBackBuffer(state);

        HDC windowDc = GetDC(state.canvas);
        if (!windowDc)
        {
            return false;
        }

        state.canvasBackBufferDc = CreateCompatibleDC(windowDc);
        if (!state.canvasBackBufferDc)
        {
            ReleaseDC(state.canvas, windowDc);
            return false;
        }

        state.canvasBackBufferBitmap = CreateCompatibleBitmap(windowDc, width, height);
        ReleaseDC(state.canvas, windowDc);
        if (!state.canvasBackBufferBitmap)
        {
            ReleaseCanvasBackBuffer(state);
            return false;
        }

        state.canvasBackBufferOldBitmap = SelectObject(state.canvasBackBufferDc, state.canvasBackBufferBitmap);
        state.canvasBackBufferWidth = width;
        state.canvasBackBufferHeight = height;
        return true;
    }

    static void InvalidateCanvas(const MarkupEditorWindow::State& state)
    {
        if (state.canvas)
        {
            InvalidateRect(state.canvas, nullptr, FALSE);
        }
    }

    static void UpdateToolButtons(MarkupEditorWindow::State& state)
    {
        CheckRadioButton(state.hwnd, kToolPenId, kToolTextId,
            state.tool == MarkupEditorWindow::Tool::Pen ? kToolPenId :
            state.tool == MarkupEditorWindow::Tool::Line ? kToolLineId :
            state.tool == MarkupEditorWindow::Tool::Arrow ? kToolArrowId :
            state.tool == MarkupEditorWindow::Tool::Rectangle ? kToolRectId :
            state.tool == MarkupEditorWindow::Tool::Ellipse ? kToolEllipseId :
            state.tool == MarkupEditorWindow::Tool::Polygon ? kToolPolygonId :
            kToolTextId);
    }

    static void DrawCanvas(MarkupEditorWindow::State& state, HDC hdc)
    {
        RECT client{};
        GetClientRect(state.canvas, &client);

        HDC targetDc = hdc;
        if (EnsureCanvasBackBuffer(state))
        {
            targetDc = state.canvasBackBufferDc;
        }

        HBRUSH backgroundBrush = CreateSolidBrush(kCanvasBackground);
        FillRect(targetDc, &client, backgroundBrush);
        DeleteObject(backgroundBrush);

        for (int y = 24; y < client.bottom; y += 24)
        {
            for (int x = 24; x < client.right; x += 24)
            {
                SetPixel(targetDc, x, y, RGB(26, 34, 47));
            }
        }

        ClampViewportOffsets(state);
        state.imageRect = ComputeImageRect(state);
        RECT canvasRect = state.imageRect;
        RECT panelRect = canvasRect;
        InflateRect(&panelRect, 10, 10);

        RECT shadowRect = panelRect;
        OffsetRect(&shadowRect, 6, 8);
        FillRoundedRect(targetDc, shadowRect, RGB(10, 14, 21), 20);
        FillRoundedRect(targetDc, panelRect, kCanvasPanel, 20);
        FrameRoundedRect(targetDc, panelRect, kCanvasBorder, 20);

        HDC memoryDc = CreateCompatibleDC(targetDc);
        HGDIOBJ previous = SelectObject(memoryDc, state.currentImage.bitmap);
        SetStretchBltMode(targetDc, HALFTONE);
        StretchBlt(
            targetDc,
            canvasRect.left,
            canvasRect.top,
            canvasRect.right - canvasRect.left,
            canvasRect.bottom - canvasRect.top,
            memoryDc,
            0,
            0,
            state.currentImage.width,
            state.currentImage.height,
            SRCCOPY);
        SelectObject(memoryDc, previous);
        DeleteDC(memoryDc);

        FrameRoundedRect(targetDc, canvasRect, RGB(220, 228, 240), 12);

        if (state.tool == MarkupEditorWindow::Tool::Polygon && state.polygonInProgress)
        {
            const POINT current = ClientToImagePoint(state.imageRect, state.currentImage, state.zoom, state.dragCurrent);
            DrawPolygonPreview(targetDc, canvasRect, state.currentImage, state.zoom, state.polygonPoints, current);
        }

        if (state.previewActive && state.tool != MarkupEditorWindow::Tool::Pen && state.tool != MarkupEditorWindow::Tool::Polygon)
        {
            const POINT start = ClientToImagePoint(state.imageRect, state.currentImage, state.zoom, state.dragStart);
            const POINT end = ClientToImagePoint(state.imageRect, state.currentImage, state.zoom, state.dragCurrent);
            POINT previewStart = ImagePointToClientPoint(canvasRect, state.zoom, start);
            POINT previewEnd = ImagePointToClientPoint(canvasRect, state.zoom, end);

            HPEN pen = CreatePen(PS_DOT, 2, state.strokeColor);
            HGDIOBJ oldPen = SelectObject(targetDc, pen);
            HGDIOBJ oldBrush = SelectObject(targetDc, GetStockObject(HOLLOW_BRUSH));
            const bool keepAspectRatio = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            switch (state.tool)
            {
            case MarkupEditorWindow::Tool::Line:
            {
                if (keepAspectRatio)
                {
                    previewEnd = SnapLineEndpoint(previewStart, previewEnd);
                }
                MoveToEx(targetDc, previewStart.x, previewStart.y, nullptr);
                LineTo(targetDc, previewEnd.x, previewEnd.y);
                break;
            }
            case MarkupEditorWindow::Tool::Arrow:
                if (keepAspectRatio)
                {
                    previewEnd = SnapLineEndpoint(previewStart, previewEnd);
                }
                MarkupEditorWindow::DrawArrow(targetDc, previewStart, previewEnd, state.strokeColor, 2);
                break;
            case MarkupEditorWindow::Tool::Rectangle:
            {
                RECT rect = NormalizeShapeRect(previewStart, previewEnd, keepAspectRatio);
                Rectangle(targetDc, rect.left, rect.top, rect.right, rect.bottom);
                break;
            }
            case MarkupEditorWindow::Tool::Ellipse:
            {
                RECT rect = NormalizeShapeRect(previewStart, previewEnd, keepAspectRatio);
                Ellipse(targetDc, rect.left, rect.top, rect.right, rect.bottom);
                break;
            }
            default:
                break;
            }
            SelectObject(targetDc, oldBrush);
            SelectObject(targetDc, oldPen);
            DeleteObject(pen);
        }

        if (targetDc != hdc)
        {
            BitBlt(hdc, 0, 0, state.canvasBackBufferWidth, state.canvasBackBufferHeight, targetDc, 0, 0, SRCCOPY);
        }
    }

    std::optional<CapturedImage> MarkupEditorWindow::Edit(HWND owner, const CapturedImage& source)
    {
        WNDCLASSW windowClass{};
        windowClass.lpfnWndProc = &MarkupEditorWindow::WindowProc;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.lpszClassName = L"OneShotNative.MarkupEditor";
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        RegisterClassW(&windowClass);

        WNDCLASSW canvasClass{};
        canvasClass.lpfnWndProc = &MarkupEditorWindow::CanvasProc;
        canvasClass.hInstance = GetModuleHandleW(nullptr);
        canvasClass.lpszClassName = L"OneShotNative.MarkupCanvas";
        canvasClass.hCursor = LoadCursorW(nullptr, IDC_CROSS);
        RegisterClassW(&canvasClass);

        State state{};
        state.outputService = &_outputService;
        state.currentImage.bitmap = CloneBitmap(source.bitmap);
        state.currentImage.x = source.x;
        state.currentImage.y = source.y;
        state.currentImage.width = source.width;
        state.currentImage.height = source.height;
        state.currentImage.capturedAtUtc = source.capturedAtUtc;

        HWND window = CreateWindowExW(
            WS_EX_APPWINDOW,
            windowClass.lpszClassName,
            L"OneShot Markup",
            WS_OVERLAPPEDWINDOW | WS_VISIBLE,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            1500,
            800,
            owner,
            nullptr,
            GetModuleHandleW(nullptr),
            &state);

        if (!window)
        {
            return std::nullopt;
        }

        ApplyModernWindowFrame(window);
        ShowWindow(window, SW_SHOW);
        UpdateWindow(window);

        MSG message{};
        while (!state.finished && GetMessageW(&message, nullptr, 0, 0))
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        ClearBitmapStack(state.undoStack);
        ClearBitmapStack(state.redoStack);
        return std::move(state.resultImage);
    }

    LRESULT CALLBACK MarkupEditorWindow::WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        auto* state = reinterpret_cast<State*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

        if (message == WM_NCCREATE)
        {
            auto* createStruct = reinterpret_cast<LPCREATESTRUCTW>(lParam);
            state = static_cast<State*>(createStruct->lpCreateParams);
            state->hwnd = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
            return TRUE;
        }

        if (!state)
        {
            return DefWindowProcW(hwnd, message, wParam, lParam);
        }

        switch (message)
        {
        case WM_ERASEBKGND:
            return TRUE;
        case WM_CREATE:
            ApplyModernWindowFrame(hwnd);
            CreateWindowExW(0, L"Button", L"Pen", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kToolPenId)), GetModuleHandleW(nullptr), nullptr);
            CreateWindowExW(0, L"Button", L"Line", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kToolLineId)), GetModuleHandleW(nullptr), nullptr);
            CreateWindowExW(0, L"Button", L"Arrow", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kToolArrowId)), GetModuleHandleW(nullptr), nullptr);
            CreateWindowExW(0, L"Button", L"Rect", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kToolRectId)), GetModuleHandleW(nullptr), nullptr);
            CreateWindowExW(0, L"Button", L"Ellipse", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kToolEllipseId)), GetModuleHandleW(nullptr), nullptr);
            CreateWindowExW(0, L"Button", L"Poly", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kToolPolygonId)), GetModuleHandleW(nullptr), nullptr);
            CreateWindowExW(0, L"Button", L"Text", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kToolTextId)), GetModuleHandleW(nullptr), nullptr);
            CreateWindowExW(0, L"Button", L"Stroke", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kStrokeColorId)), GetModuleHandleW(nullptr), nullptr);
            CreateWindowExW(0, L"Button", L"Fill", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFillColorId)), GetModuleHandleW(nullptr), nullptr);
            CreateWindowExW(0, L"Button", L"Text", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kTextColorId)), GetModuleHandleW(nullptr), nullptr);
            CreateWindowExW(0, L"Button", L"Fill", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFillToggleId)), GetModuleHandleW(nullptr), nullptr);
            CreateWindowExW(0, L"Button", L"T-", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kThicknessDownId)), GetModuleHandleW(nullptr), nullptr);
            CreateWindowExW(0, L"Button", L"T+", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kThicknessUpId)), GetModuleHandleW(nullptr), nullptr);
            CreateWindowExW(0, L"Button", L"F-", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFontDownId)), GetModuleHandleW(nullptr), nullptr);
            CreateWindowExW(0, L"Button", L"F+", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFontUpId)), GetModuleHandleW(nullptr), nullptr);
            CreateWindowExW(0, L"Button", L"Fit", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFitId)), GetModuleHandleW(nullptr), nullptr);
            CreateWindowExW(0, L"Button", L"Undo", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kUndoId)), GetModuleHandleW(nullptr), nullptr);
            CreateWindowExW(0, L"Button", L"Redo", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kRedoId)), GetModuleHandleW(nullptr), nullptr);
            CreateWindowExW(0, L"Button", L"Copy", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCopyId)), GetModuleHandleW(nullptr), nullptr);
            CreateWindowExW(0, L"Button", L"Done", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kDoneId)), GetModuleHandleW(nullptr), nullptr);
            CreateWindowExW(0, L"Button", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCancelId)), GetModuleHandleW(nullptr), nullptr);
            state->fontCombo = CreateWindowExW(0, L"ComboBox", nullptr, WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST | WS_TABSTOP, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFontComboId)), GetModuleHandleW(nullptr), nullptr);
            state->canvas = CreateWindowExW(0, L"OneShotNative.MarkupCanvas", L"", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, nullptr, GetModuleHandleW(nullptr), state);
            PopulateFontCombo(state->fontCombo, state->fontFace);
            ApplyToolbarFonts(*state);
            LayoutToolbarControls(*state);
            FitImageToViewport(*state);
            UpdateToolButtons(*state);
            CheckDlgButton(hwnd, kFillToggleId, BST_CHECKED);
            return 0;
        case WM_SIZE:
            if (state->canvas)
            {
                LayoutToolbarControls(*state);
                UpdateZoomBounds(*state, true);
                InvalidateCanvas(*state);
            }
            return 0;
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT:
        {
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetBkColor(dc, kToolbarPanel);
            SetTextColor(dc, kTextColor);
            static HBRUSH brush = CreateSolidBrush(kToolbarPanel);
            return reinterpret_cast<INT_PTR>(brush);
        }
        case WM_DRAWITEM:
        {
            auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
            if (!draw)
            {
                break;
            }

            DrawThemedButton(*state, *draw);
            return TRUE;
        }
        case WM_COMMAND:
            switch (LOWORD(wParam))
            {
            case kToolPenId: state->tool = Tool::Pen; UpdateToolButtons(*state); InvalidateRect(hwnd, nullptr, FALSE); return 0;
            case kToolLineId: state->tool = Tool::Line; UpdateToolButtons(*state); InvalidateRect(hwnd, nullptr, FALSE); return 0;
            case kToolArrowId: state->tool = Tool::Arrow; UpdateToolButtons(*state); InvalidateRect(hwnd, nullptr, FALSE); return 0;
            case kToolRectId: state->tool = Tool::Rectangle; UpdateToolButtons(*state); InvalidateRect(hwnd, nullptr, FALSE); return 0;
            case kToolEllipseId: state->tool = Tool::Ellipse; UpdateToolButtons(*state); InvalidateRect(hwnd, nullptr, FALSE); return 0;
            case kToolPolygonId: state->tool = Tool::Polygon; UpdateToolButtons(*state); InvalidateRect(hwnd, nullptr, FALSE); return 0;
            case kToolTextId: state->tool = Tool::Text; UpdateToolButtons(*state); InvalidateRect(hwnd, nullptr, FALSE); return 0;
            case kUndoId:
                if (!state->undoStack.empty())
                {
                    if (HBITMAP current = CloneBitmap(state->currentImage.bitmap))
                    {
                        state->redoStack.push_back(current);
                    }
                    HBITMAP snapshot = state->undoStack.back();
                    state->undoStack.pop_back();
                    RestoreBitmap(*state, snapshot);
                    InvalidateRect(hwnd, nullptr, FALSE);
                    InvalidateCanvas(*state);
                }
                return 0;
            case kRedoId:
                if (!state->redoStack.empty())
                {
                    if (HBITMAP current = CloneBitmap(state->currentImage.bitmap))
                    {
                        state->undoStack.push_back(current);
                    }
                    HBITMAP snapshot = state->redoStack.back();
                    state->redoStack.pop_back();
                    RestoreBitmap(*state, snapshot);
                    InvalidateRect(hwnd, nullptr, FALSE);
                    InvalidateCanvas(*state);
                }
                return 0;
            case kCopyId:
            {
                std::wstring error;
                (void)state->outputService->CopyToClipboard(hwnd, state->currentImage, error);
                return 0;
            }
            case kStrokeColorId:
                ChooseEditorColor(hwnd, state->strokeColor, state->customColors);
                InvalidateRect(hwnd, nullptr, FALSE);
                InvalidateCanvas(*state);
                return 0;
            case kFillColorId:
                ChooseEditorColor(hwnd, state->fillColor, state->customColors);
                InvalidateRect(hwnd, nullptr, FALSE);
                InvalidateCanvas(*state);
                return 0;
            case kTextColorId:
                ChooseEditorColor(hwnd, state->fontColor, state->customColors);
                InvalidateRect(hwnd, nullptr, FALSE);
                InvalidateCanvas(*state);
                return 0;
            case kFillToggleId:
                state->fillShapes = IsDlgButtonChecked(hwnd, kFillToggleId) == BST_CHECKED;
                InvalidateRect(hwnd, nullptr, FALSE);
                InvalidateCanvas(*state);
                return 0;
            case kThicknessDownId:
                state->thickness = std::max(1, state->thickness - 1);
                InvalidateRect(hwnd, nullptr, FALSE);
                InvalidateCanvas(*state);
                return 0;
            case kThicknessUpId:
                state->thickness = std::min(16, state->thickness + 1);
                InvalidateRect(hwnd, nullptr, FALSE);
                InvalidateCanvas(*state);
                return 0;
            case kFontDownId:
                state->fontSize = std::max(8, state->fontSize - 2);
                InvalidateRect(hwnd, nullptr, FALSE);
                InvalidateCanvas(*state);
                return 0;
            case kFontUpId:
                state->fontSize = std::min(96, state->fontSize + 2);
                InvalidateRect(hwnd, nullptr, FALSE);
                InvalidateCanvas(*state);
                return 0;
            case kFitId:
                state->userAdjustedViewport = false;
                FitImageToViewport(*state);
                InvalidateRect(hwnd, nullptr, FALSE);
                InvalidateCanvas(*state);
                return 0;
            case kDoneId:
                state->resultImage = std::move(state->currentImage);
                state->finished = true;
                DestroyWindow(hwnd);
                return 0;
            case kCancelId:
                state->finished = true;
                DestroyWindow(hwnd);
                return 0;
            default:
                break;
            }
            if (LOWORD(wParam) == kFontComboId && HIWORD(wParam) == CBN_SELCHANGE)
            {
                const LRESULT selection = SendMessageW(state->fontCombo, CB_GETCURSEL, 0, 0);
                if (selection != CB_ERR)
                {
                    wchar_t fontName[LF_FACESIZE]{};
                    SendMessageW(state->fontCombo, CB_GETLBTEXT, selection, reinterpret_cast<LPARAM>(fontName));
                    state->fontFace = fontName;
                }
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            break;
        case WM_CLOSE:
            state->finished = true;
            DestroyWindow(hwnd);
            return 0;
        case WM_KEYDOWN:
            if ((GetKeyState(VK_CONTROL) & 0x8000) != 0)
            {
                if (wParam == 'Z')
                {
                    SendMessageW(hwnd, WM_COMMAND, kUndoId, 0);
                    return 0;
                }
                if (wParam == 'Y')
                {
                    SendMessageW(hwnd, WM_COMMAND, kRedoId, 0);
                    return 0;
                }
                if (wParam == '0')
                {
                    SendMessageW(hwnd, WM_COMMAND, kFitId, 0);
                    return 0;
                }
            }
            break;
        case WM_PAINT:
        {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(hwnd, &paint);
            RECT client{};
            GetClientRect(hwnd, &client);
            HBRUSH backgroundBrush = CreateSolidBrush(kWindowBackground);
            FillRect(dc, &client, backgroundBrush);
            DeleteObject(backgroundBrush);

            RECT toolbarRect = MakeRect(0, 0, client.right, BuildToolbarLayout(hwnd).toolsGroup.bottom + ScaleForDpi(16, GetDpiForWindow(hwnd)));
            HBRUSH toolbarBrush = CreateSolidBrush(kToolbarSurface);
            FillRect(dc, &toolbarRect, toolbarBrush);
            DeleteObject(toolbarBrush);

            const ToolbarLayout toolbar = BuildToolbarLayout(hwnd);
            HFONT previousFont = static_cast<HFONT>(SelectObject(dc, state->uiBoldFont ? state->uiBoldFont : GetStockObject(DEFAULT_GUI_FONT)));
            DrawToolbarPanel(dc, toolbar.toolsGroup, L"TOOLS");
            DrawToolbarPanel(dc, toolbar.optionsGroup, L"STYLE");
            DrawToolbarPanel(dc, toolbar.actionsGroup, L"ACTIONS");

            RECT metaRect = MakeRect(18, toolbarRect.bottom - ScaleForDpi(22, GetDpiForWindow(hwnd)), client.right - 18, toolbarRect.bottom - ScaleForDpi(6, GetDpiForWindow(hwnd)));
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, kMutedTextColor);
            std::wstring meta = L"Thickness " + std::to_wstring(state->thickness) + L"   Font " + std::to_wstring(state->fontSize) + L"pt   Ctrl+Wheel zoom   Middle mouse pan";
            DrawTextW(dc, meta.c_str(), static_cast<int>(meta.size()), &metaRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            SelectObject(dc, previousFont);
            EndPaint(hwnd, &paint);
            return 0;
        }
        case WM_DESTROY:
            ReleaseCanvasBackBuffer(*state);
            if (state->uiFont)
            {
                DeleteObject(state->uiFont);
                state->uiFont = nullptr;
            }
            if (state->uiBoldFont)
            {
                DeleteObject(state->uiBoldFont);
                state->uiBoldFont = nullptr;
            }
            return 0;
        default:
            break;
        }

        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    LRESULT CALLBACK MarkupEditorWindow::CanvasProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        auto* state = reinterpret_cast<State*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

        if (message == WM_NCCREATE)
        {
            auto* createStruct = reinterpret_cast<LPCREATESTRUCTW>(lParam);
            state = static_cast<State*>(createStruct->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
            return TRUE;
        }

        if (!state)
        {
            return DefWindowProcW(hwnd, message, wParam, lParam);
        }

        switch (message)
        {
        case WM_ERASEBKGND:
            return TRUE;
        case WM_SIZE:
            EnsureCanvasBackBuffer(*state);
            InvalidateCanvas(*state);
            return 0;
        case WM_LBUTTONDOWN:
        {
            SetFocus(hwnd);
            POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            if (!PtInRect(&state->imageRect, point))
            {
                return 0;
            }

            SetCapture(hwnd);
            state->dragStart = point;
            state->dragCurrent = point;
            state->isDrawing = true;

            if (state->tool == Tool::Pen)
            {
                PushUndoState(*state);
            }
            else if (state->tool == Tool::Polygon)
            {
                const POINT mapped = ClientToImagePoint(state->imageRect, state->currentImage, state->zoom, point);
                if (!state->polygonInProgress)
                {
                    PushUndoState(*state);
                    state->polygonPoints.clear();
                    state->polygonInProgress = true;
                }

                state->polygonPoints.push_back(mapped);
                state->previewActive = true;
                state->dragCurrent = point;
                ReleaseCapture();
                state->isDrawing = false;
                InvalidateCanvas(*state);
            }
            else if (state->tool == Tool::Text)
            {
                const auto text = PromptForText(state->hwnd);
                if (text.has_value())
                {
                    PushUndoState(*state);
                    HDC screenDc = GetDC(nullptr);
                    HDC memoryDc = CreateCompatibleDC(screenDc);
                    HGDIOBJ previous = SelectObject(memoryDc, state->currentImage.bitmap);
                    SetBkMode(memoryDc, TRANSPARENT);
                    SetTextColor(memoryDc, state->fontColor);
                    HFONT font = CreateFontW(state->fontSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, state->fontFace.c_str());
                    HGDIOBJ oldFont = SelectObject(memoryDc, font);
                    POINT mapped = ClientToImagePoint(state->imageRect, state->currentImage, state->zoom, point);
                    TextOutW(memoryDc, mapped.x, mapped.y, text->c_str(), static_cast<int>(text->size()));
                    SelectObject(memoryDc, oldFont);
                    DeleteObject(font);
                    SelectObject(memoryDc, previous);
                    DeleteDC(memoryDc);
                    ReleaseDC(nullptr, screenDc);
                    InvalidateCanvas(*state);
                }
                state->isDrawing = false;
                ReleaseCapture();
            }
            else
            {
                state->previewActive = true;
            }
            return 0;
        }
        case WM_MOUSEMOVE:
            if (state->middlePanning)
            {
                if ((wParam & MK_MBUTTON) == 0)
                {
                    state->middlePanning = false;
                    ReleaseCapture();
                    SetCursor(LoadCursorW(nullptr, IDC_CROSS));
                    return 0;
                }

                const POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                state->viewportOffsetX = state->middlePanOffsetX - (point.x - state->middlePanStart.x);
                state->viewportOffsetY = state->middlePanOffsetY - (point.y - state->middlePanStart.y);
                ClampViewportOffsets(*state);
                state->imageRect = ComputeImageRect(*state);
                InvalidateCanvas(*state);
                return 0;
            }

            if (state->tool == Tool::Polygon && state->polygonInProgress)
            {
                POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                state->dragCurrent = ClampPointToImage(state->imageRect, point);
                InvalidateCanvas(*state);
                return 0;
            }

            if (state->isDrawing && (wParam & MK_LBUTTON))
            {
                POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                point = ClampPointToImage(state->imageRect, point);
                if (state->tool == Tool::Pen)
                {
                    HDC screenDc = GetDC(nullptr);
                    HDC memoryDc = CreateCompatibleDC(screenDc);
                    HGDIOBJ previous = SelectObject(memoryDc, state->currentImage.bitmap);
                    HPEN pen = CreatePen(PS_SOLID, state->thickness, state->strokeColor);
                    HGDIOBJ oldPen = SelectObject(memoryDc, pen);
                    POINT start = ClientToImagePoint(state->imageRect, state->currentImage, state->zoom, state->dragCurrent);
                    POINT end = ClientToImagePoint(state->imageRect, state->currentImage, state->zoom, point);
                    MoveToEx(memoryDc, start.x, start.y, nullptr);
                    LineTo(memoryDc, end.x, end.y);
                    SelectObject(memoryDc, oldPen);
                    DeleteObject(pen);
                    SelectObject(memoryDc, previous);
                    DeleteDC(memoryDc);
                    ReleaseDC(nullptr, screenDc);
                    state->dragCurrent = point;
                    InvalidateCanvas(*state);
                }
                else
                {
                    state->dragCurrent = point;
                    InvalidateCanvas(*state);
                }
            }
            return 0;
        case WM_MBUTTONDOWN:
        {
            SetFocus(hwnd);
            RECT client = GetCanvasClientRect(hwnd);
            const double contentWidth = state->currentImage.width * state->zoom;
            const double contentHeight = state->currentImage.height * state->zoom;
            if (contentWidth <= (client.right - client.left) && contentHeight <= (client.bottom - client.top))
            {
                return 0;
            }

            state->middlePanning = true;
            state->userAdjustedViewport = true;
            state->middlePanStart = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            state->middlePanOffsetX = state->viewportOffsetX;
            state->middlePanOffsetY = state->viewportOffsetY;
            SetCapture(hwnd);
            SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
            return 0;
        }
        case WM_MBUTTONUP:
            if (state->middlePanning)
            {
                state->middlePanning = false;
                ReleaseCapture();
                SetCursor(LoadCursorW(nullptr, IDC_CROSS));
            }
            return 0;
        case WM_LBUTTONUP:
            if (state->isDrawing)
            {
                POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                point = ClampPointToImage(state->imageRect, point);
                if (state->tool != Tool::Pen && state->tool != Tool::Text)
                {
                    PushUndoState(*state);
                    POINT start = ClientToImagePoint(state->imageRect, state->currentImage, state->zoom, state->dragStart);
                    POINT end = ClientToImagePoint(state->imageRect, state->currentImage, state->zoom, point);
                    const bool keepAspectRatio = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                    if (state->tool == Tool::Line || state->tool == Tool::Arrow)
                    {
                        end = SnapLineEndpoint(start, end);
                    }
                    else if (state->tool == Tool::Rectangle || state->tool == Tool::Ellipse)
                    {
                        const RECT rect = NormalizeShapeRect(start, end, keepAspectRatio);
                        start = { rect.left, rect.top };
                        end = { rect.right, rect.bottom };
                    }
                    CommitPreview(*state, start, end);
                    state->previewActive = false;
                    InvalidateCanvas(*state);
                }

                state->isDrawing = false;
                ReleaseCapture();
            }
            return 0;
        case WM_MOUSEWHEEL:
        {
            if ((GET_KEYSTATE_WPARAM(wParam) & MK_CONTROL) == 0)
            {
                break;
            }

            POINT point{};
            ScreenToCanvasClient(hwnd, lParam, point);
            state->userAdjustedViewport = true;

            const POINT imagePoint = ClientToImagePoint(state->imageRect, state->currentImage, state->zoom, point);
            const double step = GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? kZoomStep : (1.0 / kZoomStep);
            const double targetZoom = std::clamp(state->zoom * step, state->minimumZoom, kMaxZoom);
            if (std::abs(targetZoom - state->zoom) < 0.0001)
            {
                return 0;
            }

            state->zoom = targetZoom;
            RECT client = GetCanvasClientRect(hwnd);
            const double clientWidth = std::max(1.0, static_cast<double>(client.right - client.left));
            const double clientHeight = std::max(1.0, static_cast<double>(client.bottom - client.top));
            const double contentWidth = state->currentImage.width * state->zoom;
            const double contentHeight = state->currentImage.height * state->zoom;
            const double hostOriginX = std::max(0.0, (clientWidth - contentWidth) / 2.0);
            const double hostOriginY = std::max(0.0, (clientHeight - contentHeight) / 2.0);
            state->viewportOffsetX = (imagePoint.x * state->zoom) + hostOriginX + client.left - point.x;
            state->viewportOffsetY = (imagePoint.y * state->zoom) + hostOriginY + client.top - point.y;
            ClampViewportOffsets(*state);
            state->imageRect = ComputeImageRect(*state);
            InvalidateCanvas(*state);
            return 0;
        }
        case WM_RBUTTONUP:
            if (state->tool == Tool::Polygon && state->polygonInProgress)
            {
                if (state->polygonPoints.size() >= 3)
                {
                    CommitPreview(*state, { 0, 0 }, { 0, 0 });
                }

                state->polygonPoints.clear();
                state->polygonInProgress = false;
                state->previewActive = false;
                InvalidateCanvas(*state);
            }
            return 0;
        case WM_PAINT:
        {
            PAINTSTRUCT paint{};
            HDC hdc = BeginPaint(hwnd, &paint);
            DrawCanvas(*state, hdc);
            EndPaint(hwnd, &paint);
            return 0;
        }
        case WM_DESTROY:
            ReleaseCanvasBackBuffer(*state);
            return 0;
        default:
            break;
        }

        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}
