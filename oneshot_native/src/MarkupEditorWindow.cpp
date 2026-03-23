#include "oneshot_native/MarkupEditorWindow.h"
#include "oneshot_native/UiTheme.h"

#include <algorithm>
#include <commctrl.h>
#include <windowsx.h>
#include <cmath>
#include <commdlg.h>
#include <dwmapi.h>
#include <uxtheme.h>

namespace
{
    using StrokeToolSettings = oneshot::MarkupStrokePreferences;
    using ShapeToolSettings = oneshot::MarkupShapePreferences;
    using TextToolSettings = oneshot::MarkupTextPreferences;

    constexpr int kToolbarGroupHeight = 64;
    constexpr int kMargin = 16;
    constexpr double kMaxZoom = 8.0;
    constexpr double kFitPaddingFactor = 1.0;
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
    constexpr int kThicknessPreviewId = 2019;
    constexpr int kThicknessSliderId = 2020;
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
    constexpr int kToolbarHeaderInsetY = 8;
    constexpr int kToolbarButtonInsetY = 26;
    constexpr int kToolbarGroupGap = 12;
    constexpr int kToolbarGroupInnerPaddingX = 12;
    constexpr int kToolbarGroupBottomPadding = 10;
    constexpr int kToolbarMetaGap = 10;
    constexpr int kToolbarMetaHeight = 18;
    constexpr int kActionButtonSize = 30;
    constexpr int kActionButtonRadius = 14;
    constexpr int kActionButtonFontPointSize = 13;
    constexpr int kStrokeThicknessMin = 1;
    constexpr int kStrokeThicknessMax = 16;
    constexpr UINT kThicknessSliderSyncMessage = WM_APP + 1;

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
    constexpr wchar_t kActionGlyphFit[] = L"\uE9A6";
    constexpr wchar_t kActionGlyphUndo[] = L"\uE7A7";
    constexpr wchar_t kActionGlyphRedo[] = L"\uE7A6";
    constexpr wchar_t kActionGlyphCopy[] = L"\uE8C8";
    constexpr wchar_t kActionGlyphDone[] = L"\uE8FB";
    constexpr wchar_t kActionGlyphCancel[] = L"\uE711";

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

    struct ToolbarControlSpec
    {
        int id{0};
        int width{0};
        bool isCombo{false};
        bool isTrackbar{false};
    };

    struct ToolbarGroupLayout
    {
        RECT panelRect{};
        RECT contentRect{};
    };

    struct ToolbarLayout
    {
        ToolbarGroupLayout tools{};
        ToolbarGroupLayout options{};
        ToolbarGroupLayout actions{};
        RECT metaRect{};
        RECT toolbarRect{};
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

    HFONT CreateSymbolFont(HWND hwnd, int pointSize)
    {
        const UINT dpi = hwnd ? GetDpiForWindow(hwnd) : 96;
        const int height = -MulDiv(pointSize, static_cast<int>(dpi == 0 ? 96 : dpi), 72);
        return CreateFontW(
            height,
            0,
            0,
            0,
            FW_NORMAL,
            FALSE,
            FALSE,
            FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE,
            L"Segoe MDL2 Assets");
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
        if (thickness <= 0)
        {
            return;
        }

        HRGN outer = CreateRoundRectRgn(rect.left, rect.top, rect.right + 1, rect.bottom + 1, radius, radius);
        if (!outer)
        {
            return;
        }

        RECT inner = rect;
        InflateRect(&inner, -thickness, -thickness);
        if (inner.right > inner.left && inner.bottom > inner.top)
        {
            const int innerRadius = std::max(0, radius - (thickness * 2));
            HRGN innerRegion = CreateRoundRectRgn(inner.left, inner.top, inner.right + 1, inner.bottom + 1, innerRadius, innerRadius);
            if (innerRegion)
            {
                CombineRgn(outer, outer, innerRegion, RGN_DIFF);
                DeleteObject(innerRegion);
            }
        }

        HBRUSH brush = CreateSolidBrush(color);
        FillRgn(dc, outer, brush);
        DeleteObject(brush);
        DeleteObject(outer);
    }

    void DrawToolbarPanel(HDC dc, const RECT& rect, const wchar_t* label)
    {
        FillRoundedRect(dc, rect, kToolbarPanel, 18);
        FrameRoundedRect(dc, rect, kControlBorder, 18);

        RECT labelRect = rect;
        labelRect.left += 14;
        labelRect.top += kToolbarHeaderInsetY;
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, kMutedTextColor);
        DrawTextW(dc, label, -1, &labelRect, DT_LEFT | DT_TOP | DT_SINGLELINE);
    }

    std::vector<ToolbarControlSpec> GetToolControlSpecs()
    {
        return {
            { kToolPenId, 54 },
            { kToolLineId, 54 },
            { kToolArrowId, 60 },
            { kToolRectId, 54 },
            { kToolEllipseId, 68 },
            { kToolPolygonId, 56 },
            { kToolTextId, 54 }
        };
    }

    bool ToolUsesStrokeToolbarControls(oneshot::MarkupEditorWindow::Tool tool)
    {
        return tool != oneshot::MarkupEditorWindow::Tool::Text;
    }

    bool ToolUsesFillToolbarControls(oneshot::MarkupEditorWindow::Tool tool)
    {
        return tool == oneshot::MarkupEditorWindow::Tool::Rectangle
            || tool == oneshot::MarkupEditorWindow::Tool::Ellipse
            || tool == oneshot::MarkupEditorWindow::Tool::Polygon;
    }

    bool ToolUsesTextToolbarControls(oneshot::MarkupEditorWindow::Tool tool)
    {
        return tool == oneshot::MarkupEditorWindow::Tool::Text;
    }

    bool ShouldShowToolbarControl(oneshot::MarkupEditorWindow::Tool tool, int id)
    {
        switch (id)
        {
        case kStrokeColorId:
        case kThicknessPreviewId:
        case kThicknessSliderId:
            return ToolUsesStrokeToolbarControls(tool);
        case kFillColorId:
        case kFillToggleId:
            return ToolUsesFillToolbarControls(tool);
        case kTextColorId:
        case kFontDownId:
        case kFontUpId:
        case kFontComboId:
            return ToolUsesTextToolbarControls(tool);
        default:
            return true;
        }
    }

    std::vector<ToolbarControlSpec> GetOptionControlSpecs(oneshot::MarkupEditorWindow::Tool tool)
    {
        std::vector<ToolbarControlSpec> specs;
        if (ShouldShowToolbarControl(tool, kStrokeColorId))
        {
            specs.push_back({ kStrokeColorId, 92 });
        }
        if (ShouldShowToolbarControl(tool, kFillColorId))
        {
            specs.push_back({ kFillColorId, 74 });
        }
        if (ShouldShowToolbarControl(tool, kTextColorId))
        {
            specs.push_back({ kTextColorId, 76 });
        }
        if (ShouldShowToolbarControl(tool, kFillToggleId))
        {
            specs.push_back({ kFillToggleId, 64 });
        }
        if (ShouldShowToolbarControl(tool, kThicknessPreviewId))
        {
            specs.push_back({ kThicknessPreviewId, 34 });
        }
        if (ShouldShowToolbarControl(tool, kThicknessSliderId))
        {
            specs.push_back({ kThicknessSliderId, 132, false, true });
        }
        if (ShouldShowToolbarControl(tool, kFontDownId))
        {
            specs.push_back({ kFontDownId, 44 });
        }
        if (ShouldShowToolbarControl(tool, kFontUpId))
        {
            specs.push_back({ kFontUpId, 44 });
        }
        if (ShouldShowToolbarControl(tool, kFontComboId))
        {
            specs.push_back({ kFontComboId, 168, true });
        }

        return specs;
    }

    std::vector<ToolbarControlSpec> GetActionControlSpecs()
    {
        return {
            { kFitId, kActionButtonSize },
            { kUndoId, kActionButtonSize },
            { kRedoId, kActionButtonSize },
            { kCopyId, kActionButtonSize },
            { kDoneId, kActionButtonSize },
            { kCancelId, kActionButtonSize }
        };
    }

    int MeasureSingleRowWidth(const std::vector<ToolbarControlSpec>& specs, UINT dpi, int gap)
    {
        int width = 0;
        for (size_t i = 0; i < specs.size(); ++i)
        {
            const int itemWidth = ScaleForDpi(specs[i].width, dpi);
            width += itemWidth;
            if ((i + 1) < specs.size())
            {
                width += gap;
            }
        }

        return width;
    }

    int GetToolbarGroupHeight(UINT dpi)
    {
        const int contentTopOffset = ScaleForDpi(kToolbarButtonInsetY, dpi);
        const int controlHeight = ScaleForDpi(28, dpi);
        const int bottomPadding = ScaleForDpi(kToolbarGroupBottomPadding, dpi);
        return std::max(ScaleForDpi(kToolbarGroupHeight, dpi), contentTopOffset + controlHeight + bottomPadding);
    }

    int MeasureToolbarGroupWidth(const std::vector<ToolbarControlSpec>& specs, UINT dpi)
    {
        const int innerPaddingX = ScaleForDpi(kToolbarGroupInnerPaddingX, dpi);
        const int gap = ScaleForDpi(6, dpi);
        return (innerPaddingX * 2) + MeasureSingleRowWidth(specs, dpi, gap);
    }

    std::vector<oneshot::MarkupEditorWindow::Tool> GetAllMarkupTools()
    {
        return {
            oneshot::MarkupEditorWindow::Tool::Pen,
            oneshot::MarkupEditorWindow::Tool::Line,
            oneshot::MarkupEditorWindow::Tool::Arrow,
            oneshot::MarkupEditorWindow::Tool::Rectangle,
            oneshot::MarkupEditorWindow::Tool::Ellipse,
            oneshot::MarkupEditorWindow::Tool::Polygon,
            oneshot::MarkupEditorWindow::Tool::Text
        };
    }

    int GetWidestOptionsGroupWidth(UINT dpi)
    {
        int widestWidth = 0;
        for (const auto tool : GetAllMarkupTools())
        {
            widestWidth = std::max(widestWidth, MeasureToolbarGroupWidth(GetOptionControlSpecs(tool), dpi));
        }

        return widestWidth;
    }

    ToolbarGroupLayout BuildToolbarGroupLayout(int left, int top, int width, UINT dpi)
    {
        const int innerPaddingX = ScaleForDpi(kToolbarGroupInnerPaddingX, dpi);
        const int contentTopOffset = ScaleForDpi(kToolbarButtonInsetY, dpi);
        const int controlHeight = ScaleForDpi(28, dpi);
        const int groupHeight = GetToolbarGroupHeight(dpi);
        const int contentLeft = left + innerPaddingX;
        const int contentRight = std::max(contentLeft, left + width - innerPaddingX);

        ToolbarGroupLayout group{};
        group.panelRect = MakeRect(left, top, left + width, top + groupHeight);
        group.contentRect = MakeRect(
            contentLeft,
            top + contentTopOffset,
            contentRight,
            top + contentTopOffset + controlHeight);
        return group;
    }

    ToolbarLayout BuildToolbarLayout(HWND hwnd, oneshot::MarkupEditorWindow::Tool tool)
    {
        RECT client{};
        GetClientRect(hwnd, &client);
        const UINT dpi = GetDpiForWindow(hwnd);
        const int margin = ScaleForDpi(12, dpi);
        const int top = ScaleForDpi(16, dpi);
        const int groupGap = ScaleForDpi(kToolbarGroupGap, dpi);
        const int metaGap = ScaleForDpi(kToolbarMetaGap, dpi);
        const int metaHeight = ScaleForDpi(kToolbarMetaHeight, dpi);
        const int toolbarBottomInset = ScaleForDpi(10, dpi);
        const int availableWidth = std::max(1, static_cast<int>(client.right) - (margin * 2));

        const auto toolSpecs = GetToolControlSpecs();
        const auto optionSpecs = GetOptionControlSpecs(tool);
        const auto actionSpecs = GetActionControlSpecs();
        const int toolsWidth = MeasureToolbarGroupWidth(toolSpecs, dpi);
        const int actionWidth = MeasureToolbarGroupWidth(actionSpecs, dpi);
        const int optionBaseWidth = MeasureToolbarGroupWidth(optionSpecs, dpi);
        const int optionExtraWidth = std::max(0, availableWidth - (toolsWidth + optionBaseWidth + actionWidth + (groupGap * 2)));
        const int optionsWidth = optionBaseWidth + optionExtraWidth;
        const int groupHeight = GetToolbarGroupHeight(dpi);

        ToolbarLayout layout{};
        int left = margin;
        layout.tools = BuildToolbarGroupLayout(left, top, toolsWidth, dpi);
        left = layout.tools.panelRect.right + groupGap;
        layout.options = BuildToolbarGroupLayout(left, top, optionsWidth, dpi);
        left = layout.options.panelRect.right + groupGap;
        layout.actions = BuildToolbarGroupLayout(left, top, actionWidth, dpi);

        const int groupsBottom = top + groupHeight;
        layout.metaRect = MakeRect(margin, groupsBottom + metaGap, std::max(margin, static_cast<int>(client.right) - margin), groupsBottom + metaGap + metaHeight);
        layout.toolbarRect = MakeRect(0, 0, client.right, layout.metaRect.bottom + toolbarBottomInset);
        return layout;
    }

    bool IsActionButton(int id)
    {
        switch (id)
        {
        case kFitId:
        case kUndoId:
        case kRedoId:
        case kCopyId:
        case kDoneId:
        case kCancelId:
            return true;
        default:
            return false;
        }
    }

    const wchar_t* GetActionButtonGlyph(int id)
    {
        switch (id)
        {
        case kFitId:
            return kActionGlyphFit;
        case kUndoId:
            return kActionGlyphUndo;
        case kRedoId:
            return kActionGlyphRedo;
        case kCopyId:
            return kActionGlyphCopy;
        case kDoneId:
            return kActionGlyphDone;
        case kCancelId:
            return kActionGlyphCancel;
        default:
            return nullptr;
        }
    }

    const wchar_t* GetActionButtonLabel(int id)
    {
        switch (id)
        {
        case kFitId:
            return L"Fit";
        case kUndoId:
            return L"Undo";
        case kRedoId:
            return L"Redo";
        case kCopyId:
            return L"Copy";
        case kDoneId:
            return L"Done";
        case kCancelId:
            return L"Cancel";
        default:
            return L"";
        }
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
        StrokeToolSettings penSettings{};
        StrokeToolSettings lineSettings{};
        StrokeToolSettings arrowSettings{};
        ShapeToolSettings rectangleSettings{};
        ShapeToolSettings ellipseSettings{};
        ShapeToolSettings polygonSettings{};
        TextToolSettings textSettings{};
        bool finished{false};
        OutputService* outputService{nullptr};
        MarkupEditorSettingsStore* settingsStore{nullptr};
        COLORREF customColors[16]{};
        HWND fontCombo{nullptr};
        HWND thicknessPreview{nullptr};
        HWND thicknessSlider{nullptr};
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
        HWND tooltip{nullptr};
        HFONT uiFont{nullptr};
        HFONT uiBoldFont{nullptr};
        HFONT actionSymbolFont{nullptr};
        HDC canvasBackBufferDc{nullptr};
        HBITMAP canvasBackBufferBitmap{nullptr};
        HGDIOBJ canvasBackBufferOldBitmap{nullptr};
        int canvasBackBufferWidth{0};
        int canvasBackBufferHeight{0};
    };

    MarkupEditorWindow::MarkupEditorWindow(OutputService& outputService, MarkupEditorSettingsStore& settingsStore)
        : _outputService(outputService)
        , _settingsStore(settingsStore)
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

    static RECT NormalizeShapeRect(POINT start, POINT end, bool keepAspectRatio);

    struct FinalizedDragGeometry
    {
        POINT start{};
        POINT end{};
    };

    static FinalizedDragGeometry FinalizeDragGeometry(MarkupEditorWindow::Tool tool, POINT start, POINT end, bool shiftHeld)
    {
        switch (tool)
        {
        case MarkupEditorWindow::Tool::Line:
        case MarkupEditorWindow::Tool::Arrow:
            if (shiftHeld)
            {
                end = SnapLineEndpoint(start, end);
            }
            break;
        case MarkupEditorWindow::Tool::Rectangle:
        case MarkupEditorWindow::Tool::Ellipse:
        {
            const RECT rect = NormalizeShapeRect(start, end, shiftHeld);
            start = { rect.left, rect.top };
            end = { rect.right, rect.bottom };
            break;
        }
        default:
            break;
        }

        return { start, end };
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

    static void DrawPolygonPreview(HDC hdc, const RECT& canvasRect, double zoom, const std::vector<POINT>& polygonPoints, std::optional<POINT> currentPoint, COLORREF color, int thickness)
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
            HPEN pen = CreatePen(PS_SOLID, std::max(1, thickness), color);
            HGDIOBJ oldPen = SelectObject(hdc, pen);
            HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
            Polyline(hdc, preview.data(), static_cast<int>(preview.size()));
            SelectObject(hdc, oldBrush);
            SelectObject(hdc, oldPen);
            DeleteObject(pen);
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

    static std::vector<int> GetMarkupButtonIds();

    static StrokeToolSettings& GetStrokeSettings(MarkupEditorWindow::State& state, MarkupEditorWindow::Tool tool)
    {
        switch (tool)
        {
        case MarkupEditorWindow::Tool::Pen:
            return state.penSettings;
        case MarkupEditorWindow::Tool::Line:
            return state.lineSettings;
        case MarkupEditorWindow::Tool::Arrow:
            return state.arrowSettings;
        case MarkupEditorWindow::Tool::Rectangle:
            return state.rectangleSettings.stroke;
        case MarkupEditorWindow::Tool::Ellipse:
            return state.ellipseSettings.stroke;
        case MarkupEditorWindow::Tool::Polygon:
            return state.polygonSettings.stroke;
        case MarkupEditorWindow::Tool::Text:
        default:
            return state.penSettings;
        }
    }

    static const StrokeToolSettings& GetStrokeSettings(const MarkupEditorWindow::State& state, MarkupEditorWindow::Tool tool)
    {
        switch (tool)
        {
        case MarkupEditorWindow::Tool::Pen:
            return state.penSettings;
        case MarkupEditorWindow::Tool::Line:
            return state.lineSettings;
        case MarkupEditorWindow::Tool::Arrow:
            return state.arrowSettings;
        case MarkupEditorWindow::Tool::Rectangle:
            return state.rectangleSettings.stroke;
        case MarkupEditorWindow::Tool::Ellipse:
            return state.ellipseSettings.stroke;
        case MarkupEditorWindow::Tool::Polygon:
            return state.polygonSettings.stroke;
        case MarkupEditorWindow::Tool::Text:
        default:
            return state.penSettings;
        }
    }

    static ShapeToolSettings* GetShapeSettings(MarkupEditorWindow::State& state, MarkupEditorWindow::Tool tool)
    {
        switch (tool)
        {
        case MarkupEditorWindow::Tool::Rectangle:
            return &state.rectangleSettings;
        case MarkupEditorWindow::Tool::Ellipse:
            return &state.ellipseSettings;
        case MarkupEditorWindow::Tool::Polygon:
            return &state.polygonSettings;
        default:
            return nullptr;
        }
    }

    static const ShapeToolSettings* GetShapeSettings(const MarkupEditorWindow::State& state, MarkupEditorWindow::Tool tool)
    {
        switch (tool)
        {
        case MarkupEditorWindow::Tool::Rectangle:
            return &state.rectangleSettings;
        case MarkupEditorWindow::Tool::Ellipse:
            return &state.ellipseSettings;
        case MarkupEditorWindow::Tool::Polygon:
            return &state.polygonSettings;
        default:
            return nullptr;
        }
    }

    static TextToolSettings& GetTextSettings(MarkupEditorWindow::State& state)
    {
        return state.textSettings;
    }

    static const TextToolSettings& GetTextSettings(const MarkupEditorWindow::State& state)
    {
        return state.textSettings;
    }

    static bool ToolUsesStrokeOptions(MarkupEditorWindow::Tool tool)
    {
        return tool != MarkupEditorWindow::Tool::Text;
    }

    static bool ToolUsesFillOptions(MarkupEditorWindow::Tool tool)
    {
        return tool == MarkupEditorWindow::Tool::Rectangle
            || tool == MarkupEditorWindow::Tool::Ellipse
            || tool == MarkupEditorWindow::Tool::Polygon;
    }

    static bool ToolUsesTextOptions(MarkupEditorWindow::Tool tool)
    {
        return tool == MarkupEditorWindow::Tool::Text;
    }

    static std::wstring FormatHexColor(COLORREF color)
    {
        wchar_t buffer[16]{};
        swprintf_s(buffer, L"#%02X%02X%02X", GetRValue(color), GetGValue(color), GetBValue(color));
        return buffer;
    }

    static const wchar_t* GetToolOptionsLabel(MarkupEditorWindow::Tool tool)
    {
        switch (tool)
        {
        case MarkupEditorWindow::Tool::Pen:
            return L"PEN OPTIONS";
        case MarkupEditorWindow::Tool::Line:
            return L"LINE OPTIONS";
        case MarkupEditorWindow::Tool::Arrow:
            return L"ARROW OPTIONS";
        case MarkupEditorWindow::Tool::Rectangle:
            return L"RECT OPTIONS";
        case MarkupEditorWindow::Tool::Ellipse:
            return L"ELLIPSE OPTIONS";
        case MarkupEditorWindow::Tool::Polygon:
            return L"POLY OPTIONS";
        case MarkupEditorWindow::Tool::Text:
            return L"TEXT OPTIONS";
        default:
            return L"OPTIONS";
        }
    }

    static std::wstring GetToolMetaText(const MarkupEditorWindow::State& state)
    {
        std::wstring meta;
        switch (state.tool)
        {
        case MarkupEditorWindow::Tool::Pen:
        case MarkupEditorWindow::Tool::Line:
        case MarkupEditorWindow::Tool::Arrow:
        {
            const auto& stroke = GetStrokeSettings(state, state.tool);
            meta = std::wstring(GetToolOptionsLabel(state.tool));
            meta += L"  ";
            meta += FormatHexColor(stroke.color);
            meta += L"  ";
            meta += std::to_wstring(stroke.thickness);
            meta += L"px";
            break;
        }
        case MarkupEditorWindow::Tool::Rectangle:
        case MarkupEditorWindow::Tool::Ellipse:
        case MarkupEditorWindow::Tool::Polygon:
        {
            const auto& shape = *GetShapeSettings(state, state.tool);
            meta = std::wstring(GetToolOptionsLabel(state.tool));
            meta += L"  Stroke ";
            meta += FormatHexColor(shape.stroke.color);
            meta += L" ";
            meta += std::to_wstring(shape.stroke.thickness);
            meta += L"px  Fill ";
            meta += shape.fillEnabled ? L"On " : L"Off ";
            meta += FormatHexColor(shape.fillColor);
            break;
        }
        case MarkupEditorWindow::Tool::Text:
        {
            const auto& text = GetTextSettings(state);
            meta = L"TEXT OPTIONS  ";
            meta += FormatHexColor(text.color);
            meta += L"  ";
            meta += std::to_wstring(text.fontSize);
            meta += L"pt  ";
            meta += text.fontFace;
            break;
        }
        default:
            break;
        }

        meta += L"   Ctrl+Wheel zoom   Middle mouse pan";
        return meta;
    }

    static MarkupEditorPreferences BuildPreferencesSnapshot(const MarkupEditorWindow::State& state)
    {
        MarkupEditorPreferences preferences{};
        preferences.activeTool = state.tool;
        preferences.pen = state.penSettings;
        preferences.line = state.lineSettings;
        preferences.arrow = state.arrowSettings;
        preferences.rectangle = state.rectangleSettings;
        preferences.ellipse = state.ellipseSettings;
        preferences.polygon = state.polygonSettings;
        preferences.text = state.textSettings;
        return preferences;
    }

    static void PersistEditorPreferences(MarkupEditorWindow::State& state)
    {
        if (!state.settingsStore)
        {
            return;
        }

        state.settingsStore->Save(BuildPreferencesSnapshot(state));
    }

    static void SetControlVisible(HWND control, bool visible);

    static int GetCurrentStrokeThickness(const MarkupEditorWindow::State& state)
    {
        if (!ToolUsesStrokeOptions(state.tool))
        {
            return kStrokeThicknessMin;
        }

        return std::clamp(GetStrokeSettings(state, state.tool).thickness, kStrokeThicknessMin, kStrokeThicknessMax);
    }

    static int GetCanvasPreviewStrokeThickness(const MarkupEditorWindow::State& state, int thickness)
    {
        const int boundedThickness = std::max(kStrokeThicknessMin, thickness);
        return std::max(1, static_cast<int>(std::lround(static_cast<double>(boundedThickness) * state.zoom)));
    }

    static void InvalidateStrokeThicknessControls(const MarkupEditorWindow::State& state)
    {
        if (state.thicknessPreview)
        {
            InvalidateRect(state.thicknessPreview, nullptr, TRUE);
        }

        if (state.thicknessSlider)
        {
            InvalidateRect(state.thicknessSlider, nullptr, TRUE);
        }
    }

    static void RedrawStrokeThicknessControls(const MarkupEditorWindow::State& state)
    {
        InvalidateStrokeThicknessControls(state);

        if (state.thicknessPreview)
        {
            RedrawWindow(state.thicknessPreview, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE);
        }

        if (state.thicknessSlider)
        {
            RedrawWindow(state.thicknessSlider, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE);
        }
    }

    static void ApplyStrokeThicknessChange(MarkupEditorWindow::State& state)
    {
        if (!state.thicknessSlider || !ToolUsesStrokeOptions(state.tool))
        {
            return;
        }

        auto& stroke = GetStrokeSettings(state, state.tool);
        const int nextThickness = std::clamp(
            static_cast<int>(SendMessageW(state.thicknessSlider, TBM_GETPOS, 0, 0)),
            kStrokeThicknessMin,
            kStrokeThicknessMax);
        if (stroke.thickness != nextThickness)
        {
            stroke.thickness = nextThickness;
            PersistEditorPreferences(state);
        }

        RedrawStrokeThicknessControls(state);

        if (state.hwnd)
        {
            RedrawWindow(state.hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
        }
        if (state.canvas)
        {
            RedrawWindow(state.canvas, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
        }
    }

    static void SyncStrokeThicknessControls(MarkupEditorWindow::State& state)
    {
        const bool visible = ToolUsesStrokeOptions(state.tool);
        SetControlVisible(state.thicknessPreview, visible);
        SetControlVisible(state.thicknessSlider, visible);

        if (!visible || !state.thicknessSlider)
        {
            return;
        }

        SendMessageW(state.thicknessSlider, TBM_SETPOS, TRUE, GetCurrentStrokeThickness(state));
        RedrawStrokeThicknessControls(state);
    }

    static void DrawStrokePreviewDot(HDC dc, const RECT& rect, COLORREF color, int thickness, UINT dpi);

    static void PaintThicknessPreviewControl(const MarkupEditorWindow::State& state, HWND hwnd, HDC dc)
    {
        RECT rect{};
        GetClientRect(hwnd, &rect);

        HBRUSH surfaceBrush = CreateSolidBrush(kToolbarPanel);
        FillRect(dc, &rect, surfaceBrush);
        DeleteObject(surfaceBrush);

        RECT paintRect = rect;
        InflateRect(&paintRect, -1, -1);

        COLORREF background = kControlSurface;
        COLORREF border = kControlBorder;
        if (!IsWindowEnabled(hwnd))
        {
            background = RGB(46, 54, 67);
            border = RGB(122, 132, 147);
        }

        FillRoundedRect(dc, paintRect, background, 12);
        FrameRoundedRect(dc, paintRect, border, 12);

        if (ToolUsesStrokeOptions(state.tool))
        {
            const auto& stroke = GetStrokeSettings(state, state.tool);
            DrawStrokePreviewDot(dc, paintRect, stroke.color, stroke.thickness, GetDpiForWindow(state.hwnd));
        }
    }

    static LRESULT CALLBACK ThicknessSliderProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR refData)
    {
        auto* state = reinterpret_cast<MarkupEditorWindow::State*>(refData);
        if (!state)
        {
            return DefSubclassProc(hwnd, message, wParam, lParam);
        }

        const auto syncAfterDefault = [&](bool shouldSync) -> LRESULT
        {
            const LRESULT before = SendMessageW(hwnd, TBM_GETPOS, 0, 0);
            const LRESULT result = DefSubclassProc(hwnd, message, wParam, lParam);
            if (shouldSync && before != SendMessageW(hwnd, TBM_GETPOS, 0, 0))
            {
                SendMessageW(GetParent(hwnd), kThicknessSliderSyncMessage, 0, 0);
            }
            return result;
        };

        switch (message)
        {
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_MOUSEMOVE:
        case WM_KEYDOWN:
            return syncAfterDefault(message != WM_MOUSEMOVE || (wParam & MK_LBUTTON) != 0);
        case WM_MOUSEWHEEL:
        {
            const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            const int stepCount = delta / WHEEL_DELTA;
            if (stepCount == 0)
            {
                return 0;
            }

            const int current = std::clamp(
                static_cast<int>(SendMessageW(hwnd, TBM_GETPOS, 0, 0)),
                kStrokeThicknessMin,
                kStrokeThicknessMax);
            const int next = std::clamp(current + stepCount, kStrokeThicknessMin, kStrokeThicknessMax);
            if (next != current)
            {
                SendMessageW(hwnd, TBM_SETPOS, TRUE, next);
                SendMessageW(GetParent(hwnd), kThicknessSliderSyncMessage, 0, 0);
            }
            return 0;
        }
        case WM_NCDESTROY:
            RemoveWindowSubclass(hwnd, ThicknessSliderProc, 0);
            break;
        default:
            break;
        }

        return DefSubclassProc(hwnd, message, wParam, lParam);
    }

    static LRESULT CALLBACK ThicknessPreviewProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR refData)
    {
        auto* state = reinterpret_cast<MarkupEditorWindow::State*>(refData);
        switch (message)
        {
        case WM_ERASEBKGND:
            return TRUE;
        case WM_PAINT:
        {
            if (!state)
            {
                break;
            }

            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(hwnd, &paint);
            PaintThicknessPreviewControl(*state, hwnd, dc);
            EndPaint(hwnd, &paint);
            return 0;
        }
        case WM_NCDESTROY:
            RemoveWindowSubclass(hwnd, ThicknessPreviewProc, 0);
            break;
        default:
            break;
        }

        return DefSubclassProc(hwnd, message, wParam, lParam);
    }

    static bool ShouldShowToolControl(MarkupEditorWindow::Tool tool, int id)
    {
        switch (id)
        {
        case kStrokeColorId:
        case kThicknessPreviewId:
        case kThicknessSliderId:
            return ToolUsesStrokeOptions(tool);
        case kFillColorId:
        case kFillToggleId:
            return ToolUsesFillOptions(tool);
        case kTextColorId:
        case kFontDownId:
        case kFontUpId:
        case kFontComboId:
            return ToolUsesTextOptions(tool);
        default:
            return true;
        }
    }

    static void SetControlVisible(HWND control, bool visible)
    {
        if (!control)
        {
            return;
        }

        ShowWindow(control, visible ? SW_SHOWNA : SW_HIDE);
        EnableWindow(control, visible);
    }

    static void SyncToolOptionControls(MarkupEditorWindow::State& state)
    {
        if (!state.hwnd)
        {
            return;
        }

        for (const int id : { kStrokeColorId, kFillColorId, kTextColorId, kFillToggleId, kThicknessPreviewId, kThicknessSliderId, kFontDownId, kFontUpId })
        {
            SetControlVisible(GetDlgItem(state.hwnd, id), ShouldShowToolControl(state.tool, id));
        }

        SetControlVisible(state.fontCombo, ShouldShowToolControl(state.tool, kFontComboId));
        SyncStrokeThicknessControls(state);

        if (const ShapeToolSettings* shape = GetShapeSettings(state, state.tool))
        {
            CheckDlgButton(state.hwnd, kFillToggleId, shape->fillEnabled ? BST_CHECKED : BST_UNCHECKED);
        }
        else
        {
            CheckDlgButton(state.hwnd, kFillToggleId, BST_UNCHECKED);
        }

        if (state.fontCombo)
        {
            const auto& text = GetTextSettings(state);
            const LRESULT selection = SendMessageW(state.fontCombo, CB_SELECTSTRING, static_cast<WPARAM>(-1), reinterpret_cast<LPARAM>(text.fontFace.c_str()));
            if (selection == CB_ERR)
            {
                const LRESULT fallbackSelection = SendMessageW(state.fontCombo, CB_SETCURSEL, 0, 0);
                if (fallbackSelection != CB_ERR)
                {
                    wchar_t fontName[LF_FACESIZE]{};
                    SendMessageW(state.fontCombo, CB_GETLBTEXT, fallbackSelection, reinterpret_cast<LPARAM>(fontName));
                    state.textSettings.fontFace = fontName;
                }
            }
        }
    }

    static void ApplyToolbarButtonRegions(const MarkupEditorWindow::State& state, int buttonRadius)
    {
        if (!state.hwnd)
        {
            return;
        }

        for (const int id : GetMarkupButtonIds())
        {
            HWND control = GetDlgItem(state.hwnd, id);
            if (control)
            {
                oneshot::ui::ApplyRoundedWindowRegion(control, buttonRadius);
            }
        }
    }

    static void ConfigureActionTooltips(MarkupEditorWindow::State& state)
    {
        if (!state.tooltip)
        {
            return;
        }

        const auto addTool = [&state](int id)
        {
            HWND control = GetDlgItem(state.hwnd, id);
            if (!control)
            {
                return;
            }

            TOOLINFOW tool{};
            tool.cbSize = sizeof(tool);
            tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
            tool.hwnd = state.hwnd;
            tool.uId = reinterpret_cast<UINT_PTR>(control);
            tool.lpszText = const_cast<LPWSTR>(GetActionButtonLabel(id));
            SendMessageW(state.tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
        };

        addTool(kFitId);
        addTool(kUndoId);
        addTool(kRedoId);
        addTool(kCopyId);
        addTool(kDoneId);
        addTool(kCancelId);
    }

    static void PositionToolbarGroupControls(HWND parent, const std::vector<ToolbarControlSpec>& specs, const RECT& contentRect, UINT dpi, bool alignRight)
    {
        const int gap = ScaleForDpi(6, dpi);
        const int controlHeight = ScaleForDpi(28, dpi);
        const int rowWidth = MeasureSingleRowWidth(specs, dpi, gap);
        int left = alignRight ? std::max(contentRect.left, contentRect.right - rowWidth) : contentRect.left;

        for (const auto& spec : specs)
        {
            HWND control = GetDlgItem(parent, spec.id);
            if (!control)
            {
                continue;
            }

            const int controlWidth = ScaleForDpi(spec.width, dpi);
            const int controlWindowHeight = spec.isCombo ? ScaleForDpi(280, dpi) : (spec.isTrackbar ? ScaleForDpi(32, dpi) : controlHeight);
            MoveWindow(control, left, contentRect.top, controlWidth, controlWindowHeight, TRUE);
            left += controlWidth + gap;
        }
    }

    static SIZE GetMinimumEditorWindowSize(HWND hwnd)
    {
        const UINT dpi = hwnd ? GetDpiForWindow(hwnd) : 96;
        const int margin = ScaleForDpi(12, dpi);
        const int top = ScaleForDpi(16, dpi);
        const int groupGap = ScaleForDpi(kToolbarGroupGap, dpi);
        const int metaGap = ScaleForDpi(kToolbarMetaGap, dpi);
        const int metaHeight = ScaleForDpi(kToolbarMetaHeight, dpi);
        const int toolbarBottomInset = ScaleForDpi(10, dpi);
        const int minimumCanvasHeight = ScaleForDpi(140, dpi);
        const int minimumClientWidth =
            (margin * 2)
            + (groupGap * 2)
            + MeasureToolbarGroupWidth(GetToolControlSpecs(), dpi)
            + GetWidestOptionsGroupWidth(dpi)
            + MeasureToolbarGroupWidth(GetActionControlSpecs(), dpi);
        const int minimumToolbarHeight = top + GetToolbarGroupHeight(dpi) + metaGap + metaHeight + toolbarBottomInset;
        const int minimumClientHeight = std::max(ScaleForDpi(300, dpi), minimumToolbarHeight + minimumCanvasHeight);

        RECT rect = MakeRect(0, 0, minimumClientWidth, minimumClientHeight);
        const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE));
        const DWORD exStyle = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_EXSTYLE));
        AdjustWindowRectExForDpi(&rect, style, FALSE, exStyle, dpi);

        SIZE size{};
        size.cx = rect.right - rect.left;
        size.cy = rect.bottom - rect.top;
        return size;
    }

    static std::vector<int> GetMarkupButtonIds()
    {
        return {
            kToolPenId, kToolLineId, kToolArrowId, kToolRectId, kToolEllipseId, kToolPolygonId, kToolTextId,
            kStrokeColorId, kFillColorId, kTextColorId, kFillToggleId, kThicknessPreviewId,
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
        if (state.actionSymbolFont)
        {
            DeleteObject(state.actionSymbolFont);
        }

        state.uiFont = CreateUiFont(state.hwnd, 10);
        state.uiBoldFont = CreateUiFont(state.hwnd, 10, FW_SEMIBOLD);
        state.actionSymbolFont = CreateSymbolFont(state.hwnd, kActionButtonFontPointSize);

        for (const int id : GetMarkupButtonIds())
        {
            HWND control = GetDlgItem(state.hwnd, id);
            if (!control)
            {
                continue;
            }

            if (id == kThicknessPreviewId)
            {
                continue;
            }

            const HFONT font = IsActionButton(id) ? state.actionSymbolFont : state.uiFont;
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
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
        const ToolbarLayout toolbar = BuildToolbarLayout(state.hwnd, state.tool);
        const int buttonRadius = ScaleForDpi(kActionButtonRadius, dpi);

        PositionToolbarGroupControls(state.hwnd, GetToolControlSpecs(), toolbar.tools.contentRect, dpi, false);
        PositionToolbarGroupControls(state.hwnd, GetOptionControlSpecs(state.tool), toolbar.options.contentRect, dpi, false);
        PositionToolbarGroupControls(
            state.hwnd,
            GetActionControlSpecs(),
            toolbar.actions.contentRect,
            dpi,
            true);

        ApplyToolbarButtonRegions(state, buttonRadius);

        if (state.canvas)
        {
            const int canvasTop = toolbar.toolbarRect.bottom;
            RECT client{};
            GetClientRect(state.hwnd, &client);
            const int canvasWidth = std::max(1, static_cast<int>(client.right) - ScaleForDpi(24, dpi));
            const int canvasHeight = std::max(1, static_cast<int>(client.bottom) - canvasTop - ScaleForDpi(12, dpi));
            MoveWindow(
                state.canvas,
                ScaleForDpi(12, dpi),
                canvasTop,
                canvasWidth,
                canvasHeight,
                TRUE);
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

    static SIZE MeasureRenderedStrokeFootprint(const RECT& rect, int thickness, COLORREF color, UINT dpi)
    {
        SIZE measured{};
        const int width = std::max(1, static_cast<int>(rect.right - rect.left));
        const int height = std::max(1, static_cast<int>(rect.bottom - rect.top));
        const int fallbackDiameter = std::max(1, ScaleForDpi(std::max(1, thickness), dpi));
        constexpr DWORD kBackgroundPixel = 0x00D34721;

        HDC screenDc = GetDC(nullptr);
        if (!screenDc)
        {
            measured.cx = fallbackDiameter;
            measured.cy = fallbackDiameter;
            return measured;
        }

        HDC memoryDc = CreateCompatibleDC(screenDc);
        void* pixelBits = nullptr;
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        HBITMAP bitmap = CreateDIBSection(screenDc, &info, DIB_RGB_COLORS, &pixelBits, nullptr, 0);
        if (!memoryDc || !bitmap)
        {
            if (bitmap)
            {
                DeleteObject(bitmap);
            }
            if (memoryDc)
            {
                DeleteDC(memoryDc);
            }
            ReleaseDC(nullptr, screenDc);
            measured.cx = fallbackDiameter;
            measured.cy = fallbackDiameter;
            return measured;
        }

        HGDIOBJ previousBitmap = SelectObject(memoryDc, bitmap);
        auto* pixels = static_cast<DWORD*>(pixelBits);
        if (pixels)
        {
            std::fill_n(pixels, static_cast<size_t>(width) * static_cast<size_t>(height), kBackgroundPixel);
        }

        const int centerY = height / 2;
        const int inset = std::min(width / 3, std::max(2, ScaleForDpi(4, dpi)));
        const int startX = inset;
        const int endX = std::max(startX + 1, (width - 1) - inset);
        LOGBRUSH brush{};
        brush.lbStyle = BS_SOLID;
        brush.lbColor = color;
        HPEN pen = ExtCreatePen(
            PS_GEOMETRIC | PS_SOLID | PS_ENDCAP_FLAT | PS_JOIN_MITER,
            std::max(1, thickness),
            &brush,
            0,
            nullptr);
        if (!pen)
        {
            pen = CreatePen(PS_SOLID, std::max(1, thickness), color);
        }
        HGDIOBJ oldPen = SelectObject(memoryDc, pen);
        MoveToEx(memoryDc, startX, centerY, nullptr);
        LineTo(memoryDc, endX, centerY);
        SelectObject(memoryDc, oldPen);
        DeleteObject(pen);

        bool found = false;
        int minX = width;
        int minY = height;
        int maxX = -1;
        int maxY = -1;
        if (pixels)
        {
            for (int y = 0; y < height; ++y)
            {
                for (int x = 0; x < width; ++x)
                {
                    const DWORD pixel = pixels[(static_cast<size_t>(y) * static_cast<size_t>(width)) + static_cast<size_t>(x)];
                    if (pixel == kBackgroundPixel)
                    {
                        continue;
                    }

                    found = true;
                    minX = std::min(minX, x);
                    minY = std::min(minY, y);
                    maxX = std::max(maxX, x);
                    maxY = std::max(maxY, y);
                }
            }
        }

        if (found)
        {
            measured.cx = (maxX - minX) + 1;
            measured.cy = (maxY - minY) + 1;
        }
        else
        {
            measured.cx = fallbackDiameter;
            measured.cy = fallbackDiameter;
        }

        SelectObject(memoryDc, previousBitmap);
        DeleteObject(bitmap);
        DeleteDC(memoryDc);
        ReleaseDC(nullptr, screenDc);
        return measured;
    }

    static void DrawStrokePreviewDot(HDC dc, const RECT& rect, COLORREF color, int thickness, UINT dpi)
    {
        const int boundedThickness = std::clamp(thickness, kStrokeThicknessMin, kStrokeThicknessMax);
        const int inset = ScaleForDpi(6, dpi);
        const int innerMinimumDiameter = ScaleForDpi(2, dpi);
        const int ringPadding = std::max(1, ScaleForDpi(2, dpi));
        const int ringThickness = std::max(1, ScaleForDpi(1, dpi));
        const int maximumDiameter = std::max(innerMinimumDiameter, static_cast<int>(rect.bottom - rect.top) - (inset * 2));
        const SIZE measuredStroke = MeasureRenderedStrokeFootprint(rect, boundedThickness, color, dpi);
        const int innerDiameter = std::clamp(static_cast<int>(measuredStroke.cy), innerMinimumDiameter, maximumDiameter);
        const int ringDiameter = std::min(maximumDiameter, innerDiameter + (ringPadding * 2));
        const int centerX = (rect.left + rect.right) / 2;
        const int centerY = (rect.top + rect.bottom) / 2;

        RECT ringRect{};
        ringRect.left = centerX - (ringDiameter / 2);
        ringRect.top = centerY - (ringDiameter / 2);
        ringRect.right = ringRect.left + ringDiameter;
        ringRect.bottom = ringRect.top + ringDiameter;

        RECT innerRect{};
        innerRect.left = centerX - (innerDiameter / 2);
        innerRect.top = centerY - (innerDiameter / 2);
        innerRect.right = innerRect.left + innerDiameter;
        innerRect.bottom = innerRect.top + innerDiameter;

        HBRUSH hollowBrush = static_cast<HBRUSH>(GetStockObject(HOLLOW_BRUSH));
        HPEN ringPen = CreatePen(PS_SOLID, ringThickness, RGB(230, 236, 244));
        HGDIOBJ previousBrush = SelectObject(dc, hollowBrush);
        HGDIOBJ previousPen = SelectObject(dc, ringPen);
        Ellipse(dc, ringRect.left, ringRect.top, ringRect.right, ringRect.bottom);
        SelectObject(dc, previousPen);
        DeleteObject(ringPen);

        HBRUSH fillBrush = CreateSolidBrush(color);
        HPEN fillPen = CreatePen(PS_SOLID, 1, color);
        previousBrush = SelectObject(dc, fillBrush);
        previousPen = SelectObject(dc, fillPen);
        Ellipse(dc, innerRect.left, innerRect.top, innerRect.right, innerRect.bottom);
        SelectObject(dc, previousPen);
        SelectObject(dc, previousBrush);
        DeleteObject(fillPen);
        DeleteObject(fillBrush);
    }

    static LRESULT DrawThemedThicknessSlider(const MarkupEditorWindow::State& state, NMCUSTOMDRAW& customDraw)
    {
        if (customDraw.dwDrawStage != CDDS_PREPAINT)
        {
            return CDRF_DODEFAULT;
        }

        RECT client = customDraw.rc;
        HBRUSH backgroundBrush = CreateSolidBrush(kToolbarPanel);
        FillRect(customDraw.hdc, &client, backgroundBrush);
        DeleteObject(backgroundBrush);

        RECT channel{};
        RECT thumb{};
        SendMessageW(customDraw.hdr.hwndFrom, TBM_GETCHANNELRECT, 0, reinterpret_cast<LPARAM>(&channel));
        SendMessageW(customDraw.hdr.hwndFrom, TBM_GETTHUMBRECT, 0, reinterpret_cast<LPARAM>(&thumb));

        RECT track = channel;
        const int trackHeight = std::max(ScaleForDpi(4, GetDpiForWindow(state.hwnd)), 4);
        const int trackCenterY = (channel.top + channel.bottom) / 2;
        track.top = trackCenterY - (trackHeight / 2);
        track.bottom = track.top + trackHeight;

        const int thumbCenterX = (thumb.left + thumb.right) / 2;
        RECT activeTrack = track;
        activeTrack.right = static_cast<LONG>(std::clamp(thumbCenterX, static_cast<int>(track.left), static_cast<int>(track.right)));

        FillRoundedRect(customDraw.hdc, track, kControlBorder, trackHeight);
        if (activeTrack.right > activeTrack.left)
        {
            FillRoundedRect(customDraw.hdc, activeTrack, kAccentColor, trackHeight);
        }

        RECT thumbRect = thumb;
        InflateRect(&thumbRect, -1, -1);
        const bool focused = GetFocus() == customDraw.hdr.hwndFrom;
        const COLORREF thumbColor = focused ? kAccentHotColor : kControlSurfaceHot;
        FillRoundedRect(customDraw.hdc, thumbRect, thumbColor, std::max(ScaleForDpi(8, GetDpiForWindow(state.hwnd)), 8));
        FrameRoundedRect(customDraw.hdc, thumbRect, kAccentHotColor, std::max(ScaleForDpi(8, GetDpiForWindow(state.hwnd)), 8));

        return CDRF_SKIPDEFAULT;
    }

    static void DrawThemedButton(const MarkupEditorWindow::State& state, const DRAWITEMSTRUCT& draw)
    {
        RECT rect = draw.rcItem;
        RECT paintRect = rect;
        InflateRect(&paintRect, -1, -1);
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

        HBRUSH surfaceBrush = CreateSolidBrush(kToolbarPanel);
        FillRect(draw.hDC, &rect, surfaceBrush);
        DeleteObject(surfaceBrush);

        FillRoundedRect(draw.hDC, paintRect, background, 12);
        const bool drawBorder = !IsActionButton(draw.CtlID);
        if (drawBorder)
        {
            FrameRoundedRect(draw.hDC, paintRect, border, 12);
        }

        std::wstring label;
        if (draw.CtlID == kStrokeColorId)
        {
            const auto& stroke = GetStrokeSettings(state, state.tool);
            DrawColorSwatch(draw.hDC, paintRect, stroke.color);
            label = FormatColorLabel(L"Stroke", stroke.color);
        }
        else if (draw.CtlID == kFillColorId)
        {
            const ShapeToolSettings* shape = GetShapeSettings(state, state.tool);
            const COLORREF color = shape ? shape->fillColor : RGB(255, 224, 224);
            DrawColorSwatch(draw.hDC, paintRect, color);
            label = FormatColorLabel(L"Fill", color);
        }
        else if (draw.CtlID == kTextColorId)
        {
            const auto& textSettings = GetTextSettings(state);
            DrawColorSwatch(draw.hDC, paintRect, textSettings.color);
            label = FormatColorLabel(L"Text", textSettings.color);
        }
        else if (draw.CtlID == kFillToggleId)
        {
            const ShapeToolSettings* shape = GetShapeSettings(state, state.tool);
            label = (shape && shape->fillEnabled) ? L"Fill On" : L"Fill Off";
        }
        else if (draw.CtlID == kThicknessPreviewId)
        {
            const auto& stroke = GetStrokeSettings(state, state.tool);
            DrawStrokePreviewDot(draw.hDC, paintRect, stroke.color, stroke.thickness, GetDpiForWindow(state.hwnd));
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

        if (IsActionButton(draw.CtlID))
        {
            if (const wchar_t* glyph = GetActionButtonGlyph(draw.CtlID))
            {
                HFONT previousFont = static_cast<HFONT>(SelectObject(draw.hDC, state.actionSymbolFont ? state.actionSymbolFont : GetStockObject(DEFAULT_GUI_FONT)));
                RECT glyphRect = paintRect;
                glyphRect.top -= 1;
                SetBkMode(draw.hDC, TRANSPARENT);
                SetTextColor(draw.hDC, text);
                DrawTextW(draw.hDC, glyph, -1, &glyphRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                SelectObject(draw.hDC, previousFont);
            }
        }
        else if (draw.CtlID == kStrokeColorId || draw.CtlID == kFillColorId || draw.CtlID == kTextColorId)
        {
            RECT textRect = paintRect;
            textRect.left += 30;
            SetBkMode(draw.hDC, TRANSPARENT);
            SetTextColor(draw.hDC, text);
            DrawTextW(draw.hDC, label.c_str(), static_cast<int>(label.size()), &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
        else if (draw.CtlID == kThicknessPreviewId)
        {
        }
        else
        {
            DrawButtonText(draw.hDC, paintRect, label, text);
        }

        if (focused)
        {
            RECT focusRect = paintRect;
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
            fakeState.penSettings.color = kAccentColor;
            fakeState.rectangleSettings.fillColor = RGB(120, 156, 204);
            fakeState.textSettings.color = kTextColor;
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

        const auto& stroke = GetStrokeSettings(state, state.tool);
        HPEN pen = CreatePen(PS_SOLID, stroke.thickness, stroke.color);
        HGDIOBJ oldPen = SelectObject(memoryDc, pen);
        HBRUSH fillBrush = nullptr;
        HGDIOBJ oldBrush = SelectObject(memoryDc, GetStockObject(HOLLOW_BRUSH));
        if (const ShapeToolSettings* shape = GetShapeSettings(state, state.tool))
        {
            fillBrush = CreateSolidBrush(shape->fillColor);
            SelectObject(memoryDc, oldBrush);
            oldBrush = SelectObject(memoryDc, shape->fillEnabled ? fillBrush : GetStockObject(HOLLOW_BRUSH));
        }

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
            if (fillBrush)
            {
                DeleteObject(fillBrush);
            }
            MarkupEditorWindow::DrawArrow(memoryDc, startPoint, endPoint, stroke.color, stroke.thickness);
            pen = nullptr;
            fillBrush = nullptr;
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
        if (fillBrush)
        {
            DeleteObject(fillBrush);
        }
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

    static void RefreshToolSelectionUi(MarkupEditorWindow::State& state)
    {
        UpdateToolButtons(state);
        SyncToolOptionControls(state);
        LayoutToolbarControls(state);
        InvalidateRect(state.hwnd, nullptr, FALSE);
        InvalidateCanvas(state);
    }

    static void SetActiveTool(MarkupEditorWindow::State& state, MarkupEditorWindow::Tool tool)
    {
        state.tool = tool;
        PersistEditorPreferences(state);
        RefreshToolSelectionUi(state);
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

        if (state.tool == MarkupEditorWindow::Tool::Polygon && state.polygonInProgress)
        {
            const POINT current = ClientToImagePoint(state.imageRect, state.currentImage, state.zoom, state.dragCurrent);
            const auto& polygon = state.polygonSettings;
            const int previewThickness = GetCanvasPreviewStrokeThickness(state, polygon.stroke.thickness);
            DrawPolygonPreview(targetDc, canvasRect, state.zoom, state.polygonPoints, current, polygon.stroke.color, previewThickness);
        }

        if (state.previewActive && state.tool != MarkupEditorWindow::Tool::Pen && state.tool != MarkupEditorWindow::Tool::Polygon)
        {
            const POINT start = ClientToImagePoint(state.imageRect, state.currentImage, state.zoom, state.dragStart);
            const POINT end = ClientToImagePoint(state.imageRect, state.currentImage, state.zoom, state.dragCurrent);
            const bool shiftHeld = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            const FinalizedDragGeometry geometry = FinalizeDragGeometry(state.tool, start, end, shiftHeld);
            const POINT previewStart = ImagePointToClientPoint(canvasRect, state.zoom, geometry.start);
            const POINT previewEnd = ImagePointToClientPoint(canvasRect, state.zoom, geometry.end);

            const auto& stroke = GetStrokeSettings(state, state.tool);
            const int previewThickness = GetCanvasPreviewStrokeThickness(state, stroke.thickness);
            HPEN pen = CreatePen(PS_SOLID, previewThickness, stroke.color);
            HGDIOBJ oldPen = SelectObject(targetDc, pen);
            HBRUSH fillBrush = nullptr;
            HGDIOBJ oldBrush = SelectObject(targetDc, GetStockObject(HOLLOW_BRUSH));
            if (const ShapeToolSettings* shape = GetShapeSettings(state, state.tool))
            {
                fillBrush = CreateSolidBrush(shape->fillColor);
                SelectObject(targetDc, oldBrush);
                oldBrush = SelectObject(targetDc, shape->fillEnabled ? fillBrush : GetStockObject(HOLLOW_BRUSH));
            }
            switch (state.tool)
            {
            case MarkupEditorWindow::Tool::Line:
            {
                MoveToEx(targetDc, previewStart.x, previewStart.y, nullptr);
                LineTo(targetDc, previewEnd.x, previewEnd.y);
                break;
            }
            case MarkupEditorWindow::Tool::Arrow:
                MarkupEditorWindow::DrawArrow(targetDc, previewStart, previewEnd, stroke.color, previewThickness);
                break;
            case MarkupEditorWindow::Tool::Rectangle:
            {
                RECT rect = NormalizeShapeRect(previewStart, previewEnd, false);
                Rectangle(targetDc, rect.left, rect.top, rect.right, rect.bottom);
                break;
            }
            case MarkupEditorWindow::Tool::Ellipse:
            {
                RECT rect = NormalizeShapeRect(previewStart, previewEnd, false);
                Ellipse(targetDc, rect.left, rect.top, rect.right, rect.bottom);
                break;
            }
            default:
                break;
            }
            SelectObject(targetDc, oldBrush);
            SelectObject(targetDc, oldPen);
            if (fillBrush)
            {
                DeleteObject(fillBrush);
            }
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
        state.settingsStore = &_settingsStore;
        state.currentImage.bitmap = CloneBitmap(source.bitmap);
        state.currentImage.x = source.x;
        state.currentImage.y = source.y;
        state.currentImage.width = source.width;
        state.currentImage.height = source.height;
        state.currentImage.capturedAtUtc = source.capturedAtUtc;

        const auto& preferences = _settingsStore.GetPreferences();
        state.tool = preferences.activeTool;
        state.penSettings = preferences.pen;
        state.lineSettings = preferences.line;
        state.arrowSettings = preferences.arrow;
        state.rectangleSettings = preferences.rectangle;
        state.ellipseSettings = preferences.ellipse;
        state.polygonSettings = preferences.polygon;
        state.textSettings = preferences.text;

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
        case WM_GETMINMAXINFO:
        {
            auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
            if (info)
            {
                const SIZE minimumSize = GetMinimumEditorWindowSize(hwnd);
                info->ptMinTrackSize.x = minimumSize.cx;
                info->ptMinTrackSize.y = minimumSize.cy;
                return 0;
            }
            break;
        }
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
            state->thicknessPreview = CreateWindowExW(0, L"Static", L"", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kThicknessPreviewId)), GetModuleHandleW(nullptr), nullptr);
            state->thicknessSlider = CreateWindowExW(0, TRACKBAR_CLASSW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_HORZ | TBS_NOTICKS, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kThicknessSliderId)), GetModuleHandleW(nullptr), nullptr);
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
            state->tooltip = CreateWindowExW(
                WS_EX_TOPMOST,
                TOOLTIPS_CLASSW,
                nullptr,
                WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
                CW_USEDEFAULT,
                CW_USEDEFAULT,
                CW_USEDEFAULT,
                CW_USEDEFAULT,
                hwnd,
                nullptr,
                GetModuleHandleW(nullptr),
                nullptr);
            if (state->thicknessSlider)
            {
                SendMessageW(state->thicknessSlider, TBM_SETRANGE, TRUE, MAKELPARAM(kStrokeThicknessMin, kStrokeThicknessMax));
                SendMessageW(state->thicknessSlider, TBM_SETLINESIZE, 0, 1);
                SendMessageW(state->thicknessSlider, TBM_SETPAGESIZE, 0, 1);
                SendMessageW(state->thicknessSlider, TBM_SETPOS, TRUE, GetCurrentStrokeThickness(*state));
                SetWindowTheme(state->thicknessSlider, L"", L"");
                SetWindowSubclass(state->thicknessSlider, ThicknessSliderProc, 0, reinterpret_cast<DWORD_PTR>(state));
            }
            if (state->thicknessPreview)
            {
                SetWindowSubclass(state->thicknessPreview, ThicknessPreviewProc, 0, reinterpret_cast<DWORD_PTR>(state));
            }
            PopulateFontCombo(state->fontCombo, state->textSettings.fontFace);
            ApplyToolbarFonts(*state);
            ConfigureActionTooltips(*state);
            SyncToolOptionControls(*state);
            LayoutToolbarControls(*state);
            FitImageToViewport(*state);
            UpdateToolButtons(*state);
            return 0;
        case WM_SIZE:
            if (state->canvas)
            {
                LayoutToolbarControls(*state);
                UpdateZoomBounds(*state, true);
                RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
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
        case WM_NOTIFY:
        {
            auto* header = reinterpret_cast<NMHDR*>(lParam);
            if (header && header->idFrom == kThicknessSliderId && header->code == NM_CUSTOMDRAW)
            {
                auto* customDraw = reinterpret_cast<NMCUSTOMDRAW*>(lParam);
                return DrawThemedThicknessSlider(*state, *customDraw);
            }
            break;
        }
        case WM_COMMAND:
            switch (LOWORD(wParam))
            {
            case kToolPenId:
                SetActiveTool(*state, Tool::Pen);
                return 0;
            case kToolLineId:
                SetActiveTool(*state, Tool::Line);
                return 0;
            case kToolArrowId:
                SetActiveTool(*state, Tool::Arrow);
                return 0;
            case kToolRectId:
                SetActiveTool(*state, Tool::Rectangle);
                return 0;
            case kToolEllipseId:
                SetActiveTool(*state, Tool::Ellipse);
                return 0;
            case kToolPolygonId:
                SetActiveTool(*state, Tool::Polygon);
                return 0;
            case kToolTextId:
                SetActiveTool(*state, Tool::Text);
                return 0;
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
                if (ToolUsesStrokeOptions(state->tool))
                {
                    auto& stroke = GetStrokeSettings(*state, state->tool);
                    if (ChooseEditorColor(hwnd, stroke.color, state->customColors))
                    {
                        PersistEditorPreferences(*state);
                        RedrawStrokeThicknessControls(*state);
                        InvalidateRect(hwnd, nullptr, FALSE);
                        InvalidateCanvas(*state);
                    }
                }
                return 0;
            case kFillColorId:
                if (ShapeToolSettings* shape = GetShapeSettings(*state, state->tool))
                {
                    if (ChooseEditorColor(hwnd, shape->fillColor, state->customColors))
                    {
                        PersistEditorPreferences(*state);
                        InvalidateRect(hwnd, nullptr, FALSE);
                        InvalidateCanvas(*state);
                    }
                }
                return 0;
            case kTextColorId:
                if (ToolUsesTextOptions(state->tool))
                {
                    auto& text = GetTextSettings(*state);
                    if (ChooseEditorColor(hwnd, text.color, state->customColors))
                    {
                        PersistEditorPreferences(*state);
                        InvalidateRect(hwnd, nullptr, FALSE);
                        InvalidateCanvas(*state);
                    }
                }
                return 0;
            case kFillToggleId:
                if (ShapeToolSettings* shape = GetShapeSettings(*state, state->tool))
                {
                    shape->fillEnabled = IsDlgButtonChecked(hwnd, kFillToggleId) == BST_CHECKED;
                    PersistEditorPreferences(*state);
                    InvalidateRect(hwnd, nullptr, FALSE);
                    InvalidateCanvas(*state);
                }
                return 0;
            case kFontDownId:
                if (ToolUsesTextOptions(state->tool))
                {
                    auto& text = GetTextSettings(*state);
                    text.fontSize = std::max(8, text.fontSize - 2);
                    PersistEditorPreferences(*state);
                    InvalidateRect(hwnd, nullptr, FALSE);
                    InvalidateCanvas(*state);
                }
                return 0;
            case kFontUpId:
                if (ToolUsesTextOptions(state->tool))
                {
                    auto& text = GetTextSettings(*state);
                    text.fontSize = std::min(96, text.fontSize + 2);
                    PersistEditorPreferences(*state);
                    InvalidateRect(hwnd, nullptr, FALSE);
                    InvalidateCanvas(*state);
                }
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
                    state->textSettings.fontFace = fontName;
                    PersistEditorPreferences(*state);
                }
                InvalidateRect(hwnd, nullptr, FALSE);
                InvalidateCanvas(*state);
                return 0;
            }
            break;
        case WM_HSCROLL:
            if (reinterpret_cast<HWND>(lParam) == state->thicknessSlider && ToolUsesStrokeOptions(state->tool))
            {
                ApplyStrokeThicknessChange(*state);
                return 0;
            }
            break;
        case kThicknessSliderSyncMessage:
            ApplyStrokeThicknessChange(*state);
            return 0;
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

            const ToolbarLayout toolbar = BuildToolbarLayout(hwnd, state->tool);
            RECT toolbarRect = toolbar.toolbarRect;
            HBRUSH toolbarBrush = CreateSolidBrush(kToolbarSurface);
            FillRect(dc, &toolbarRect, toolbarBrush);
            DeleteObject(toolbarBrush);

            HFONT previousFont = static_cast<HFONT>(SelectObject(dc, state->uiBoldFont ? state->uiBoldFont : GetStockObject(DEFAULT_GUI_FONT)));
            DrawToolbarPanel(dc, toolbar.tools.panelRect, L"TOOLS");
            DrawToolbarPanel(dc, toolbar.options.panelRect, GetToolOptionsLabel(state->tool));
            DrawToolbarPanel(dc, toolbar.actions.panelRect, L"ACTIONS");

            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, kMutedTextColor);
            std::wstring meta = GetToolMetaText(*state);
            RECT metaRect = toolbar.metaRect;
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
            if (state->actionSymbolFont)
            {
                DeleteObject(state->actionSymbolFont);
                state->actionSymbolFont = nullptr;
            }
            if (state->tooltip)
            {
                DestroyWindow(state->tooltip);
                state->tooltip = nullptr;
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
                    const auto& textSettings = GetTextSettings(*state);
                    SetTextColor(memoryDc, textSettings.color);
                    HFONT font = CreateFontW(textSettings.fontSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, textSettings.fontFace.c_str());
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
                    const auto& stroke = GetStrokeSettings(*state, state->tool);
                    HPEN pen = CreatePen(PS_SOLID, stroke.thickness, stroke.color);
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
                    const bool shiftHeld = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                    const FinalizedDragGeometry geometry = FinalizeDragGeometry(state->tool, start, end, shiftHeld);
                    CommitPreview(*state, geometry.start, geometry.end);
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
