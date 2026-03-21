#include "oneshot_native/MarkupEditorWindow.h"

#include <windowsx.h>
#include <cmath>

namespace
{
    constexpr int kToolbarHeight = 42;
    constexpr int kMargin = 8;

    constexpr int kToolPenId = 2001;
    constexpr int kToolLineId = 2002;
    constexpr int kToolArrowId = 2003;
    constexpr int kToolRectId = 2004;
    constexpr int kToolEllipseId = 2005;
    constexpr int kToolTextId = 2006;
    constexpr int kUndoId = 2010;
    constexpr int kRedoId = 2011;
    constexpr int kCopyId = 2012;
    constexpr int kDoneId = 2013;
    constexpr int kCancelId = 2014;
    constexpr int kTextInputId = 2015;

    RECT MakeRect(int left, int top, int right, int bottom)
    {
        RECT rect{};
        rect.left = left;
        rect.top = top;
        rect.right = right;
        rect.bottom = bottom;
        return rect;
    }
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
        COLORREF color{RGB(255, 0, 0)};
        int thickness{3};
        bool finished{false};
        OutputService* outputService{nullptr};
    };

    MarkupEditorWindow::MarkupEditorWindow(OutputService& outputService)
        : _outputService(outputService)
    {
    }

    MarkupEditorWindow::~MarkupEditorWindow() = default;

    static POINT ClampPointToImage(const RECT& imageRect, POINT point)
    {
        point.x = static_cast<LONG>(std::clamp(static_cast<int>(point.x), static_cast<int>(imageRect.left), static_cast<int>(imageRect.right)));
        point.y = static_cast<LONG>(std::clamp(static_cast<int>(point.y), static_cast<int>(imageRect.top), static_cast<int>(imageRect.bottom)));
        return point;
    }

    static RECT ComputeImageRect(HWND hwnd, const CapturedImage& image)
    {
        RECT client{};
        GetClientRect(hwnd, &client);

        const int availableWidth = std::max(1, static_cast<int>((client.right - client.left) - (kMargin * 2)));
        const int availableHeight = std::max(1, static_cast<int>((client.bottom - client.top) - kToolbarHeight - (kMargin * 2)));
        const double scaleX = static_cast<double>(availableWidth) / static_cast<double>(image.width);
        const double scaleY = static_cast<double>(availableHeight) / static_cast<double>(image.height);
        const double scale = std::min(scaleX, scaleY);

        const int drawWidth = std::max(1, static_cast<int>(std::round(image.width * scale)));
        const int drawHeight = std::max(1, static_cast<int>(std::round(image.height * scale)));
        const int left = kMargin + ((availableWidth - drawWidth) / 2);
        const int top = kToolbarHeight + kMargin + ((availableHeight - drawHeight) / 2);
        return MakeRect(left, top, left + drawWidth, top + drawHeight);
    }

    static POINT ClientToImagePoint(const RECT& imageRect, const CapturedImage& image, POINT point)
    {
        point = ClampPointToImage(imageRect, point);
        const double scaleX = static_cast<double>(image.width) / std::max(1, static_cast<int>(imageRect.right - imageRect.left));
        const double scaleY = static_cast<double>(image.height) / std::max(1, static_cast<int>(imageRect.bottom - imageRect.top));
        POINT mapped{};
        mapped.x = static_cast<int>(std::round((point.x - imageRect.left) * scaleX));
        mapped.y = static_cast<int>(std::round((point.y - imageRect.top) * scaleY));
        mapped.x = static_cast<LONG>(std::clamp(static_cast<int>(mapped.x), 0, image.width - 1));
        mapped.y = static_cast<LONG>(std::clamp(static_cast<int>(mapped.y), 0, image.height - 1));
        return mapped;
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

        HPEN pen = CreatePen(PS_SOLID, state.thickness, state.color);
        HGDIOBJ oldPen = SelectObject(memoryDc, pen);
        HGDIOBJ oldBrush = SelectObject(memoryDc, GetStockObject(HOLLOW_BRUSH));

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
            MarkupEditorWindow::DrawArrow(memoryDc, startPoint, endPoint, state.color, state.thickness);
            break;
        case MarkupEditorWindow::Tool::Rectangle:
            Rectangle(memoryDc, startPoint.x, startPoint.y, endPoint.x, endPoint.y);
            SelectObject(memoryDc, oldBrush);
            DeleteObject(pen);
            break;
        case MarkupEditorWindow::Tool::Ellipse:
            Ellipse(memoryDc, startPoint.x, startPoint.y, endPoint.x, endPoint.y);
            SelectObject(memoryDc, oldBrush);
            DeleteObject(pen);
            break;
        default:
            break;
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

    static void UpdateToolButtons(MarkupEditorWindow::State& state)
    {
        CheckRadioButton(state.hwnd, kToolPenId, kToolTextId,
            state.tool == MarkupEditorWindow::Tool::Pen ? kToolPenId :
            state.tool == MarkupEditorWindow::Tool::Line ? kToolLineId :
            state.tool == MarkupEditorWindow::Tool::Arrow ? kToolArrowId :
            state.tool == MarkupEditorWindow::Tool::Rectangle ? kToolRectId :
            state.tool == MarkupEditorWindow::Tool::Ellipse ? kToolEllipseId :
            kToolTextId);
    }

    static void DrawCanvas(MarkupEditorWindow::State& state, HDC hdc)
    {
        RECT client{};
        GetClientRect(state.canvas, &client);
        FillRect(hdc, &client, static_cast<HBRUSH>(GetStockObject(DKGRAY_BRUSH)));

        state.imageRect = ComputeImageRect(state.hwnd, state.currentImage);
        RECT canvasRect = state.imageRect;
        OffsetRect(&canvasRect, -client.left, -kToolbarHeight);

        HDC memoryDc = CreateCompatibleDC(hdc);
        HGDIOBJ previous = SelectObject(memoryDc, state.currentImage.bitmap);
        SetStretchBltMode(hdc, HALFTONE);
        StretchBlt(
            hdc,
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

        FrameRect(hdc, &canvasRect, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));

        if (state.previewActive && state.tool != MarkupEditorWindow::Tool::Pen)
        {
            const POINT start = ClientToImagePoint(state.imageRect, state.currentImage, state.dragStart);
            const POINT end = ClientToImagePoint(state.imageRect, state.currentImage, state.dragCurrent);
            const auto scaleX = static_cast<double>(canvasRect.right - canvasRect.left) / std::max(1, state.currentImage.width);
            const auto scaleY = static_cast<double>(canvasRect.bottom - canvasRect.top) / std::max(1, state.currentImage.height);
            POINT previewStart{
                canvasRect.left + static_cast<LONG>(std::round(start.x * scaleX)),
                canvasRect.top + static_cast<LONG>(std::round(start.y * scaleY))
            };
            POINT previewEnd{
                canvasRect.left + static_cast<LONG>(std::round(end.x * scaleX)),
                canvasRect.top + static_cast<LONG>(std::round(end.y * scaleY))
            };

            HPEN pen = CreatePen(PS_DOT, 2, state.color);
            HGDIOBJ oldPen = SelectObject(hdc, pen);
            HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
            switch (state.tool)
            {
            case MarkupEditorWindow::Tool::Line:
                MoveToEx(hdc, previewStart.x, previewStart.y, nullptr);
                LineTo(hdc, previewEnd.x, previewEnd.y);
                break;
            case MarkupEditorWindow::Tool::Arrow:
                MarkupEditorWindow::DrawArrow(hdc, previewStart, previewEnd, state.color, 2);
                break;
            case MarkupEditorWindow::Tool::Rectangle:
                Rectangle(hdc, previewStart.x, previewStart.y, previewEnd.x, previewEnd.y);
                break;
            case MarkupEditorWindow::Tool::Ellipse:
                Ellipse(hdc, previewStart.x, previewStart.y, previewEnd.x, previewEnd.y);
                break;
            default:
                break;
            }
            SelectObject(hdc, oldBrush);
            SelectObject(hdc, oldPen);
            DeleteObject(pen);
        }
    }

    std::optional<CapturedImage> MarkupEditorWindow::Edit(HWND owner, const CapturedImage& source)
    {
        WNDCLASSW windowClass{};
        windowClass.lpfnWndProc = &MarkupEditorWindow::WindowProc;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.lpszClassName = L"OneShotNative.MarkupEditor";
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        RegisterClassW(&windowClass);

        WNDCLASSW canvasClass{};
        canvasClass.lpfnWndProc = &MarkupEditorWindow::CanvasProc;
        canvasClass.hInstance = GetModuleHandleW(nullptr);
        canvasClass.lpszClassName = L"OneShotNative.MarkupCanvas";
        canvasClass.hCursor = LoadCursorW(nullptr, IDC_CROSS);
        canvasClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
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
            1100,
            800,
            owner,
            nullptr,
            GetModuleHandleW(nullptr),
            &state);

        if (!window)
        {
            return std::nullopt;
        }

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
        case WM_CREATE:
            CreateWindowExW(0, L"Button", L"Pen", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON, 8, 8, 70, 24, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kToolPenId)), GetModuleHandleW(nullptr), nullptr);
            CreateWindowExW(0, L"Button", L"Line", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON, 82, 8, 70, 24, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kToolLineId)), GetModuleHandleW(nullptr), nullptr);
            CreateWindowExW(0, L"Button", L"Arrow", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON, 156, 8, 70, 24, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kToolArrowId)), GetModuleHandleW(nullptr), nullptr);
            CreateWindowExW(0, L"Button", L"Rect", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON, 230, 8, 70, 24, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kToolRectId)), GetModuleHandleW(nullptr), nullptr);
            CreateWindowExW(0, L"Button", L"Ellipse", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON, 304, 8, 70, 24, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kToolEllipseId)), GetModuleHandleW(nullptr), nullptr);
            CreateWindowExW(0, L"Button", L"Text", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON, 378, 8, 70, 24, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kToolTextId)), GetModuleHandleW(nullptr), nullptr);
            CreateWindowExW(0, L"Button", L"Undo", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 470, 8, 70, 24, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kUndoId)), GetModuleHandleW(nullptr), nullptr);
            CreateWindowExW(0, L"Button", L"Redo", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 544, 8, 70, 24, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kRedoId)), GetModuleHandleW(nullptr), nullptr);
            CreateWindowExW(0, L"Button", L"Copy", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 618, 8, 70, 24, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCopyId)), GetModuleHandleW(nullptr), nullptr);
            CreateWindowExW(0, L"Button", L"Done", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 692, 8, 70, 24, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kDoneId)), GetModuleHandleW(nullptr), nullptr);
            CreateWindowExW(0, L"Button", L"Cancel", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 766, 8, 70, 24, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCancelId)), GetModuleHandleW(nullptr), nullptr);
            state->textInput = CreateWindowExW(WS_EX_CLIENTEDGE, L"Edit", L"Text", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 850, 8, 220, 24, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kTextInputId)), GetModuleHandleW(nullptr), nullptr);
            state->canvas = CreateWindowExW(0, L"OneShotNative.MarkupCanvas", L"", WS_CHILD | WS_VISIBLE, 0, kToolbarHeight, 100, 100, hwnd, nullptr, GetModuleHandleW(nullptr), state);
            UpdateToolButtons(*state);
            return 0;
        case WM_SIZE:
            if (state->canvas)
            {
                MoveWindow(state->canvas, 0, kToolbarHeight, LOWORD(lParam), HIWORD(lParam) - kToolbarHeight, TRUE);
            }
            return 0;
        case WM_COMMAND:
            switch (LOWORD(wParam))
            {
            case kToolPenId: state->tool = Tool::Pen; UpdateToolButtons(*state); return 0;
            case kToolLineId: state->tool = Tool::Line; UpdateToolButtons(*state); return 0;
            case kToolArrowId: state->tool = Tool::Arrow; UpdateToolButtons(*state); return 0;
            case kToolRectId: state->tool = Tool::Rectangle; UpdateToolButtons(*state); return 0;
            case kToolEllipseId: state->tool = Tool::Ellipse; UpdateToolButtons(*state); return 0;
            case kToolTextId: state->tool = Tool::Text; UpdateToolButtons(*state); return 0;
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
                    InvalidateRect(state->canvas, nullptr, TRUE);
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
                    InvalidateRect(state->canvas, nullptr, TRUE);
                }
                return 0;
            case kCopyId:
            {
                std::wstring error;
                (void)state->outputService->CopyToClipboard(hwnd, state->currentImage, error);
                return 0;
            }
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
            break;
        case WM_CLOSE:
            state->finished = true;
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
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
        case WM_LBUTTONDOWN:
        {
            POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) + kToolbarHeight };
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
            else if (state->tool == Tool::Text)
            {
                wchar_t text[256]{};
                GetWindowTextW(state->textInput, text, static_cast<int>(std::size(text)));
                if (text[0] != L'\0')
                {
                    PushUndoState(*state);
                    HDC screenDc = GetDC(nullptr);
                    HDC memoryDc = CreateCompatibleDC(screenDc);
                    HGDIOBJ previous = SelectObject(memoryDc, state->currentImage.bitmap);
                    SetBkMode(memoryDc, TRANSPARENT);
                    SetTextColor(memoryDc, state->color);
                    POINT mapped = ClientToImagePoint(state->imageRect, state->currentImage, point);
                    TextOutW(memoryDc, mapped.x, mapped.y, text, static_cast<int>(wcslen(text)));
                    SelectObject(memoryDc, previous);
                    DeleteDC(memoryDc);
                    ReleaseDC(nullptr, screenDc);
                    InvalidateRect(hwnd, nullptr, TRUE);
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
            if (state->isDrawing && (wParam & MK_LBUTTON))
            {
                POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) + kToolbarHeight };
                point = ClampPointToImage(state->imageRect, point);
                if (state->tool == Tool::Pen)
                {
                    HDC screenDc = GetDC(nullptr);
                    HDC memoryDc = CreateCompatibleDC(screenDc);
                    HGDIOBJ previous = SelectObject(memoryDc, state->currentImage.bitmap);
                    HPEN pen = CreatePen(PS_SOLID, state->thickness, state->color);
                    HGDIOBJ oldPen = SelectObject(memoryDc, pen);
                    POINT start = ClientToImagePoint(state->imageRect, state->currentImage, state->dragCurrent);
                    POINT end = ClientToImagePoint(state->imageRect, state->currentImage, point);
                    MoveToEx(memoryDc, start.x, start.y, nullptr);
                    LineTo(memoryDc, end.x, end.y);
                    SelectObject(memoryDc, oldPen);
                    DeleteObject(pen);
                    SelectObject(memoryDc, previous);
                    DeleteDC(memoryDc);
                    ReleaseDC(nullptr, screenDc);
                    state->dragCurrent = point;
                    InvalidateRect(hwnd, nullptr, TRUE);
                }
                else
                {
                    state->dragCurrent = point;
                    InvalidateRect(hwnd, nullptr, TRUE);
                }
            }
            return 0;
        case WM_LBUTTONUP:
            if (state->isDrawing)
            {
                POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) + kToolbarHeight };
                point = ClampPointToImage(state->imageRect, point);
                if (state->tool != Tool::Pen && state->tool != Tool::Text)
                {
                    PushUndoState(*state);
                    POINT start = ClientToImagePoint(state->imageRect, state->currentImage, state->dragStart);
                    POINT end = ClientToImagePoint(state->imageRect, state->currentImage, point);
                    CommitPreview(*state, start, end);
                    state->previewActive = false;
                    InvalidateRect(hwnd, nullptr, TRUE);
                }

                state->isDrawing = false;
                ReleaseCapture();
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
        default:
            break;
        }

        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}
