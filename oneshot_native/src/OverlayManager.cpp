#include "oneshot_native/OverlayManager.h"

#include <windowsx.h>

namespace
{
    struct OverlaySession;

    struct OverlayWindowState
    {
        OverlaySession* session{nullptr};
        RECT monitorBounds{};
    };

    struct OverlaySession
    {
        const oneshot::CapturedImage* image{nullptr};
        RECT selection{};
        bool selecting{false};
        bool completed{false};
        bool cancelled{true};
        POINT anchor{};
        HWND captureWindow{nullptr};
        std::vector<HWND> windows;
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

    RECT IntersectRectangles(const RECT& a, const RECT& b)
    {
        RECT result{};
        result.left = std::max(a.left, b.left);
        result.top = std::max(a.top, b.top);
        result.right = std::min(a.right, b.right);
        result.bottom = std::min(a.bottom, b.bottom);
        if (result.right <= result.left || result.bottom <= result.top)
        {
            return RECT{};
        }

        return result;
    }

    POINT ClientToScreenPoint(HWND hwnd, LPARAM lParam)
    {
        POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ClientToScreen(hwnd, &point);
        return point;
    }

    void InvalidateAll(const OverlaySession& session)
    {
        for (const auto hwnd : session.windows)
        {
            if (IsWindow(hwnd))
            {
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        }
    }

    void CloseAll(OverlaySession& session)
    {
        auto windows = session.windows;
        session.windows.clear();
        for (const auto hwnd : windows)
        {
            if (IsWindow(hwnd))
            {
                DestroyWindow(hwnd);
            }
        }
    }

    struct MonitorEnumData
    {
        std::vector<RECT>* monitors;
    };

    BOOL CALLBACK EnumMonitorsProc(HMONITOR monitor, HDC, LPRECT, LPARAM lParam)
    {
        auto* data = reinterpret_cast<MonitorEnumData*>(lParam);
        MONITORINFO info{};
        info.cbSize = sizeof(info);
        if (GetMonitorInfoW(monitor, &info))
        {
            data->monitors->push_back(info.rcMonitor);
        }

        return TRUE;
    }

    LRESULT CALLBACK OverlayWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        auto* state = reinterpret_cast<OverlayWindowState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

        if (message == WM_NCCREATE)
        {
            const auto* createStruct = reinterpret_cast<LPCREATESTRUCTW>(lParam);
            state = static_cast<OverlayWindowState*>(createStruct->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
            return TRUE;
        }

        if (!state || !state->session)
        {
            return DefWindowProcW(hwnd, message, wParam, lParam);
        }

        auto& session = *state->session;

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
                session.cancelled = true;
                session.completed = true;
                CloseAll(session);
                return 0;
            }
            break;
        case WM_RBUTTONUP:
            session.cancelled = true;
            session.completed = true;
            CloseAll(session);
            return 0;
        case WM_LBUTTONDOWN:
            SetForegroundWindow(hwnd);
            SetCapture(hwnd);
            session.captureWindow = hwnd;
            session.selecting = true;
            session.cancelled = false;
            session.anchor = ClientToScreenPoint(hwnd, lParam);
            session.selection = NormalizeRect(session.anchor, session.anchor);
            InvalidateAll(session);
            return 0;
        case WM_MOUSEMOVE:
            if (session.selecting && session.captureWindow == hwnd)
            {
                const POINT current = ClientToScreenPoint(hwnd, lParam);
                session.selection = NormalizeRect(session.anchor, current);
                InvalidateAll(session);
            }
            return 0;
        case WM_LBUTTONUP:
            if (session.selecting && session.captureWindow == hwnd)
            {
                ReleaseCapture();
                session.selecting = false;
                session.captureWindow = nullptr;
                const POINT current = ClientToScreenPoint(hwnd, lParam);
                session.selection = NormalizeRect(session.anchor, current);
                session.cancelled = (session.selection.right - session.selection.left) < 2 || (session.selection.bottom - session.selection.top) < 2;
                session.completed = true;
                CloseAll(session);
            }
            return 0;
        case WM_PAINT:
        {
            PAINTSTRUCT paint{};
            HDC hdc = BeginPaint(hwnd, &paint);

            const RECT& monitorBounds = state->monitorBounds;
            const int width = monitorBounds.right - monitorBounds.left;
            const int height = monitorBounds.bottom - monitorBounds.top;
            const int sourceX = monitorBounds.left - session.image->x;
            const int sourceY = monitorBounds.top - session.image->y;

            HDC memoryDc = CreateCompatibleDC(hdc);
            HGDIOBJ previous = SelectObject(memoryDc, session.image->bitmap);
            BitBlt(hdc, 0, 0, width, height, memoryDc, sourceX, sourceY, SRCCOPY);
            SelectObject(memoryDc, previous);
            DeleteDC(memoryDc);

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
            *static_cast<DWORD*>(bits) = RGB(0, 0, 0);
            HGDIOBJ shadePrevious = SelectObject(shadeDc, shadeBitmap);
            AlphaBlend(hdc, 0, 0, width, height, shadeDc, 0, 0, 1, 1, blend);
            SelectObject(shadeDc, shadePrevious);
            DeleteObject(shadeBitmap);
            DeleteDC(shadeDc);

            RECT visibleSelection = IntersectRectangles(session.selection, monitorBounds);
            if (!session.cancelled && visibleSelection.right > visibleSelection.left && visibleSelection.bottom > visibleSelection.top)
            {
                HDC sourceDc = CreateCompatibleDC(hdc);
                HGDIOBJ sourcePrev = SelectObject(sourceDc, session.image->bitmap);
                const int destX = visibleSelection.left - monitorBounds.left;
                const int destY = visibleSelection.top - monitorBounds.top;
                const int visibleWidth = visibleSelection.right - visibleSelection.left;
                const int visibleHeight = visibleSelection.bottom - visibleSelection.top;

                BitBlt(
                    hdc,
                    destX,
                    destY,
                    visibleWidth,
                    visibleHeight,
                    sourceDc,
                    visibleSelection.left - session.image->x,
                    visibleSelection.top - session.image->y,
                    SRCCOPY);
                SelectObject(sourceDc, sourcePrev);
                DeleteDC(sourceDc);

                HPEN pen = CreatePen(PS_SOLID, 2, RGB(61, 121, 216));
                HGDIOBJ oldPen = SelectObject(hdc, pen);
                HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
                Rectangle(hdc, destX, destY, destX + visibleWidth, destY + visibleHeight);
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

        std::vector<RECT> monitorBounds;
        MonitorEnumData enumData{ &monitorBounds };
        EnumDisplayMonitors(nullptr, nullptr, EnumMonitorsProc, reinterpret_cast<LPARAM>(&enumData));
        if (monitorBounds.empty())
        {
            monitorBounds.push_back(RECT{ image.x, image.y, image.x + image.width, image.y + image.height });
        }

        std::sort(monitorBounds.begin(), monitorBounds.end(), [](const RECT& left, const RECT& right)
        {
            if (left.top != right.top)
            {
                return left.top < right.top;
            }

            return left.left < right.left;
        });

        OverlaySession session{};
        session.image = &image;
        std::vector<std::unique_ptr<OverlayWindowState>> windowStates;
        windowStates.reserve(monitorBounds.size());

        for (const auto& bounds : monitorBounds)
        {
            auto state = std::make_unique<OverlayWindowState>();
            state->session = &session;
            state->monitorBounds = bounds;

            HWND overlay = CreateWindowExW(
                WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
                windowClass.lpszClassName,
                L"OneShot Overlay",
                WS_POPUP,
                bounds.left,
                bounds.top,
                bounds.right - bounds.left,
                bounds.bottom - bounds.top,
                owner,
                nullptr,
                GetModuleHandleW(nullptr),
                state.get());

            if (!overlay)
            {
                CloseAll(session);
                return std::nullopt;
            }

            session.windows.push_back(overlay);
            windowStates.push_back(std::move(state));
        }

        for (const auto hwnd : session.windows)
        {
            ShowWindow(hwnd, SW_SHOW);
            UpdateWindow(hwnd);
        }

        if (!session.windows.empty())
        {
            SetForegroundWindow(session.windows.front());
        }

        MSG message{};
        while (!session.completed && !session.windows.empty() && GetMessageW(&message, nullptr, 0, 0))
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        if (session.cancelled)
        {
            return std::nullopt;
        }

        return session.selection;
    }
}
