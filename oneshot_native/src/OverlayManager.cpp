#include "oneshot_native/OverlayManager.h"

#include <windowsx.h>

namespace
{
    struct OverlayState
    {
        const oneshot::CapturedImage* image{nullptr};
        RECT selection{};
        bool selecting{false};
        bool completed{false};
        bool cancelled{true};
        POINT anchor{};
    };

    RECT NormalizeRect(POINT a, POINT b)
    {
        RECT rect{};
        rect.left = std::min(a.x, b.x);
        rect.top = std::min(a.y, b.y);
        rect.right = std::max(a.x, b.x);
        rect.bottom = std::max(a.y, b.y);
        return rect;
    }

    LRESULT CALLBACK OverlayWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        auto* state = reinterpret_cast<OverlayState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

        if (message == WM_NCCREATE)
        {
            const auto* createStruct = reinterpret_cast<LPCREATESTRUCTW>(lParam);
            state = static_cast<OverlayState*>(createStruct->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
            return TRUE;
        }

        if (!state)
        {
            return DefWindowProcW(hwnd, message, wParam, lParam);
        }

        switch (message)
        {
        case WM_SETCURSOR:
            SetCursor(LoadCursorW(nullptr, IDC_CROSS));
            return TRUE;
        case WM_ERASEBKGND:
            return TRUE;
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE)
            {
                state->cancelled = true;
                state->completed = true;
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        case WM_RBUTTONUP:
            state->cancelled = true;
            state->completed = true;
            DestroyWindow(hwnd);
            return 0;
        case WM_LBUTTONDOWN:
        {
            SetCapture(hwnd);
            state->selecting = true;
            state->cancelled = false;
            state->anchor = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            state->selection = NormalizeRect(state->anchor, state->anchor);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_MOUSEMOVE:
            if (state->selecting)
            {
                POINT current{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                state->selection = NormalizeRect(state->anchor, current);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_LBUTTONUP:
            if (state->selecting)
            {
                ReleaseCapture();
                state->selecting = false;
                POINT current{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                state->selection = NormalizeRect(state->anchor, current);
                state->cancelled = (state->selection.right - state->selection.left) < 2 || (state->selection.bottom - state->selection.top) < 2;
                state->completed = true;
                DestroyWindow(hwnd);
            }
            return 0;
        case WM_PAINT:
        {
            PAINTSTRUCT paint{};
            HDC hdc = BeginPaint(hwnd, &paint);
            HDC memoryDc = CreateCompatibleDC(hdc);
            HGDIOBJ previous = SelectObject(memoryDc, state->image->bitmap);
            BitBlt(hdc, 0, 0, state->image->width, state->image->height, memoryDc, 0, 0, SRCCOPY);
            SelectObject(memoryDc, previous);
            DeleteDC(memoryDc);

            HBRUSH shade = CreateSolidBrush(RGB(0, 0, 0));
            BLENDFUNCTION blend{ AC_SRC_OVER, 0, 96, 0 };

            HDC shadeDc = CreateCompatibleDC(hdc);
            BITMAPINFO bmi{};
            bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth = 1;
            bmi.bmiHeader.biHeight = 1;
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 32;
            bmi.bmiHeader.biCompression = BI_RGB;
            void* bits = nullptr;
            HBITMAP shadeBitmap = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
            auto* pixel = static_cast<DWORD*>(bits);
            *pixel = RGB(0, 0, 0);
            HGDIOBJ shadePrev = SelectObject(shadeDc, shadeBitmap);
            AlphaBlend(hdc, 0, 0, state->image->width, state->image->height, shadeDc, 0, 0, 1, 1, blend);
            SelectObject(shadeDc, shadePrev);
            DeleteObject(shadeBitmap);
            DeleteDC(shadeDc);

            const RECT selection = state->selection;
            if (!state->cancelled && selection.right > selection.left && selection.bottom > selection.top)
            {
                HDC sourceDc = CreateCompatibleDC(hdc);
                HGDIOBJ sourcePrev = SelectObject(sourceDc, state->image->bitmap);
                BitBlt(
                    hdc,
                    selection.left,
                    selection.top,
                    selection.right - selection.left,
                    selection.bottom - selection.top,
                    sourceDc,
                    selection.left,
                    selection.top,
                    SRCCOPY);
                SelectObject(sourceDc, sourcePrev);
                DeleteDC(sourceDc);

                HPEN pen = CreatePen(PS_SOLID, 2, RGB(61, 121, 216));
                HGDIOBJ oldPen = SelectObject(hdc, pen);
                HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
                Rectangle(hdc, selection.left, selection.top, selection.right, selection.bottom);
                SelectObject(hdc, oldBrush);
                SelectObject(hdc, oldPen);
                DeleteObject(pen);
            }

            EndPaint(hwnd, &paint);
            return 0;
        }
        default:
            break;
        }

        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}

namespace oneshot
{
    std::optional<RECT> OverlayManager::SelectRegion(const CapturedImage& image, HWND owner) const
    {
        WNDCLASSW windowClass{};
        windowClass.lpfnWndProc = OverlayWindowProc;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.lpszClassName = L"OneShotNative.Overlay";
        windowClass.hCursor = LoadCursorW(nullptr, IDC_CROSS);
        RegisterClassW(&windowClass);

        OverlayState state{};
        state.image = &image;

        HWND overlay = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
            windowClass.lpszClassName,
            L"OneShot Overlay",
            WS_POPUP,
            image.x,
            image.y,
            image.width,
            image.height,
            owner,
            nullptr,
            GetModuleHandleW(nullptr),
            &state);

        if (!overlay)
        {
            return std::nullopt;
        }

        ShowWindow(overlay, SW_SHOW);
        UpdateWindow(overlay);
        SetForegroundWindow(overlay);

        MSG message{};
        while (IsWindow(overlay) && GetMessageW(&message, nullptr, 0, 0))
        {
            if (!IsDialogMessageW(overlay, &message))
            {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }

            if (state.completed)
            {
                break;
            }
        }

        if (state.cancelled)
        {
            return std::nullopt;
        }

        RECT result = state.selection;
        result.left += image.x;
        result.top += image.y;
        result.right += image.x;
        result.bottom += image.y;
        return result;
    }
}
