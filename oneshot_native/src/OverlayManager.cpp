#include "oneshot_native/OverlayManager.h"

#include <windowsx.h>

namespace
{
    constexpr UINT kWmMouseMove = 0x0200;
    constexpr UINT kWmLButtonUp = 0x0202;
    constexpr UINT kWmRButtonDown = 0x0204;
    constexpr UINT kWmKeyDown = 0x0100;
    constexpr DWORD kVkEscape = 0x1B;
    constexpr BYTE kDimAlpha = 85;
    constexpr BYTE kSelectionFillAlpha = 51;
    constexpr COLORREF kSelectionBorderColor = RGB(255, 255, 255);
    constexpr int kSelectionBorderWidth = 2;

    struct OverlaySession;
    LRESULT CALLBACK OverlayWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    struct OverlayWindowState
    {
        OverlaySession* session{nullptr};
        RECT monitorBounds{};
        HDC backBufferDc{nullptr};
        HBITMAP backBufferBitmap{nullptr};
        HGDIOBJ backBufferOldBitmap{nullptr};
        HDC dimmedDc{nullptr};
        HBITMAP dimmedBitmap{nullptr};
        HGDIOBJ dimmedOldBitmap{nullptr};
        HDC selectionFillDc{nullptr};
        HBITMAP selectionFillBitmap{nullptr};
        HGDIOBJ selectionFillOldBitmap{nullptr};
        HPEN selectionBorderPen{nullptr};
        int surfaceWidth{0};
        int surfaceHeight{0};
    };

    struct OverlaySession
    {
        const oneshot::CapturedImage* image{nullptr};
        RECT selection{};
        bool selecting{false};
        bool completed{false};
        bool cancelled{true};
        POINT anchor{};
        std::vector<HWND> windows;
        HHOOK keyboardHook{nullptr};
        HHOOK mouseHook{nullptr};
    };

    OverlaySession* g_activeSession = nullptr;

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

    void RemoveHooks(OverlaySession& session)
    {
        if (session.keyboardHook)
        {
            UnhookWindowsHookEx(session.keyboardHook);
            session.keyboardHook = nullptr;
        }

        if (session.mouseHook)
        {
            UnhookWindowsHookEx(session.mouseHook);
            session.mouseHook = nullptr;
        }

        if (g_activeSession == &session)
        {
            g_activeSession = nullptr;
        }
    }

    void FinishSession(OverlaySession& session, bool cancelled)
    {
        session.cancelled = cancelled;
        session.completed = true;
        session.selecting = false;
        RemoveHooks(session);
        CloseAll(session);
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

    void ReleaseSurface(HDC& dc, HBITMAP& bitmap, HGDIOBJ& oldBitmap)
    {
        if (dc)
        {
            if (oldBitmap)
            {
                SelectObject(dc, oldBitmap);
                oldBitmap = nullptr;
            }
            if (bitmap)
            {
                DeleteObject(bitmap);
                bitmap = nullptr;
            }
            DeleteDC(dc);
            dc = nullptr;
        }
    }

    void ReleaseOverlayResources(OverlayWindowState& state)
    {
        ReleaseSurface(state.backBufferDc, state.backBufferBitmap, state.backBufferOldBitmap);
        ReleaseSurface(state.dimmedDc, state.dimmedBitmap, state.dimmedOldBitmap);
        ReleaseSurface(state.selectionFillDc, state.selectionFillBitmap, state.selectionFillOldBitmap);
        if (state.selectionBorderPen)
        {
            DeleteObject(state.selectionBorderPen);
            state.selectionBorderPen = nullptr;
        }
        state.surfaceWidth = 0;
        state.surfaceHeight = 0;
    }

    HBITMAP CreateSolidPixelBitmap(HDC referenceDc, COLORREF color)
    {
        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = 1;
        bmi.bmiHeader.biHeight = 1;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        void* bits = nullptr;
        HBITMAP bitmap = CreateDIBSection(referenceDc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!bitmap || !bits)
        {
            if (bitmap)
            {
                DeleteObject(bitmap);
            }
            return nullptr;
        }

        *static_cast<DWORD*>(bits) = RGB(GetRValue(color), GetGValue(color), GetBValue(color));
        return bitmap;
    }

    bool BuildDimmedMonitorBitmap(OverlayWindowState& state)
    {
        if (!state.session || !state.session->image || !state.dimmedDc)
        {
            return false;
        }

        HDC sourceDc = CreateCompatibleDC(state.dimmedDc);
        if (!sourceDc)
        {
            return false;
        }

        HGDIOBJ sourcePrevious = SelectObject(sourceDc, state.session->image->bitmap);
        const int sourceX = state.monitorBounds.left - state.session->image->x;
        const int sourceY = state.monitorBounds.top - state.session->image->y;
        BitBlt(state.dimmedDc, 0, 0, state.surfaceWidth, state.surfaceHeight, sourceDc, sourceX, sourceY, SRCCOPY);
        SelectObject(sourceDc, sourcePrevious);
        DeleteDC(sourceDc);

        HDC shadeDc = CreateCompatibleDC(state.dimmedDc);
        if (!shadeDc)
        {
            return false;
        }

        HBITMAP shadeBitmap = CreateSolidPixelBitmap(state.dimmedDc, RGB(0, 0, 0));
        if (!shadeBitmap)
        {
            DeleteDC(shadeDc);
            return false;
        }

        HGDIOBJ shadePrevious = SelectObject(shadeDc, shadeBitmap);
        const BLENDFUNCTION dimBlend{ AC_SRC_OVER, 0, kDimAlpha, 0 };
        AlphaBlend(state.dimmedDc, 0, 0, state.surfaceWidth, state.surfaceHeight, shadeDc, 0, 0, 1, 1, dimBlend);
        SelectObject(shadeDc, shadePrevious);
        DeleteObject(shadeBitmap);
        DeleteDC(shadeDc);
        return true;
    }

    bool EnsureOverlayResources(HWND hwnd, OverlayWindowState& state)
    {
        const int width = state.monitorBounds.right - state.monitorBounds.left;
        const int height = state.monitorBounds.bottom - state.monitorBounds.top;
        if (width <= 0 || height <= 0)
        {
            return false;
        }

        if (state.surfaceWidth == width &&
            state.surfaceHeight == height &&
            state.backBufferDc &&
            state.dimmedDc &&
            state.selectionFillDc &&
            state.selectionBorderPen)
        {
            return true;
        }

        ReleaseOverlayResources(state);

        HDC windowDc = GetDC(hwnd);
        if (!windowDc)
        {
            return false;
        }

        state.backBufferDc = CreateCompatibleDC(windowDc);
        state.backBufferBitmap = CreateCompatibleBitmap(windowDc, width, height);
        state.dimmedDc = CreateCompatibleDC(windowDc);
        state.dimmedBitmap = CreateCompatibleBitmap(windowDc, width, height);
        state.selectionFillDc = CreateCompatibleDC(windowDc);
        state.selectionFillBitmap = CreateSolidPixelBitmap(windowDc, RGB(255, 255, 255));
        state.selectionBorderPen = CreatePen(PS_SOLID, kSelectionBorderWidth, kSelectionBorderColor);
        ReleaseDC(hwnd, windowDc);

        if (!state.backBufferDc || !state.backBufferBitmap || !state.dimmedDc || !state.dimmedBitmap ||
            !state.selectionFillDc || !state.selectionFillBitmap || !state.selectionBorderPen)
        {
            ReleaseOverlayResources(state);
            return false;
        }

        state.backBufferOldBitmap = SelectObject(state.backBufferDc, state.backBufferBitmap);
        state.dimmedOldBitmap = SelectObject(state.dimmedDc, state.dimmedBitmap);
        state.selectionFillOldBitmap = SelectObject(state.selectionFillDc, state.selectionFillBitmap);
        state.surfaceWidth = width;
        state.surfaceHeight = height;

        if (!BuildDimmedMonitorBitmap(state))
        {
            ReleaseOverlayResources(state);
            return false;
        }

        return true;
    }

    void ComposeOverlayFrame(OverlayWindowState& state, HDC targetDc)
    {
        if (!targetDc)
        {
            return;
        }

        if (state.dimmedDc)
        {
            BitBlt(targetDc, 0, 0, state.surfaceWidth, state.surfaceHeight, state.dimmedDc, 0, 0, SRCCOPY);
        }
        else
        {
            PatBlt(targetDc, 0, 0, state.surfaceWidth, state.surfaceHeight, BLACKNESS);
        }

        if (!state.session || state.session->cancelled)
        {
            return;
        }

        const RECT visibleSelection = IntersectRectangles(state.session->selection, state.monitorBounds);
        if (visibleSelection.right <= visibleSelection.left || visibleSelection.bottom <= visibleSelection.top)
        {
            return;
        }

        const int destX = visibleSelection.left - state.monitorBounds.left;
        const int destY = visibleSelection.top - state.monitorBounds.top;
        const int visibleWidth = visibleSelection.right - visibleSelection.left;
        const int visibleHeight = visibleSelection.bottom - visibleSelection.top;

        HDC sourceDc = CreateCompatibleDC(targetDc);
        if (sourceDc)
        {
            HGDIOBJ sourcePrevious = SelectObject(sourceDc, state.session->image->bitmap);
            BitBlt(
                targetDc,
                destX,
                destY,
                visibleWidth,
                visibleHeight,
                sourceDc,
                visibleSelection.left - state.session->image->x,
                visibleSelection.top - state.session->image->y,
                SRCCOPY);
            SelectObject(sourceDc, sourcePrevious);
            DeleteDC(sourceDc);
        }

        if (state.selectionFillDc)
        {
            const BLENDFUNCTION fillBlend{ AC_SRC_OVER, 0, kSelectionFillAlpha, 0 };
            AlphaBlend(targetDc, destX, destY, visibleWidth, visibleHeight, state.selectionFillDc, 0, 0, 1, 1, fillBlend);
        }

        if (state.selectionBorderPen)
        {
            HGDIOBJ oldPen = SelectObject(targetDc, state.selectionBorderPen);
            HGDIOBJ oldBrush = SelectObject(targetDc, GetStockObject(HOLLOW_BRUSH));
            Rectangle(targetDc, destX, destY, destX + visibleWidth, destY + visibleHeight);
            SelectObject(targetDc, oldBrush);
            SelectObject(targetDc, oldPen);
        }
    }

    void EnsureOverlayClassRegistered()
    {
        static const bool registered = []()
        {
            WNDCLASSW windowClass{};
            windowClass.lpfnWndProc = OverlayWindowProc;
            windowClass.hInstance = GetModuleHandleW(nullptr);
            windowClass.lpszClassName = L"OneShotNative.Overlay";
            windowClass.hCursor = LoadCursorW(nullptr, IDC_CROSS);
            RegisterClassW(&windowClass);
            return true;
        }();

        (void)registered;
    }

    LRESULT CALLBACK KeyboardHookProc(int code, WPARAM wParam, LPARAM lParam)
    {
        if (code >= 0 && g_activeSession && wParam == kWmKeyDown)
        {
            const auto* keyboard = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
            if (keyboard && keyboard->vkCode == kVkEscape)
            {
                FinishSession(*g_activeSession, true);
                return 1;
            }
        }

        return CallNextHookEx(nullptr, code, wParam, lParam);
    }

    LRESULT CALLBACK MouseHookProc(int code, WPARAM wParam, LPARAM lParam)
    {
        if (code >= 0 && g_activeSession)
        {
            const auto* mouse = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
            if (mouse)
            {
                if (wParam == kWmMouseMove && g_activeSession->selecting)
                {
                    g_activeSession->selection = NormalizeRect(g_activeSession->anchor, mouse->pt);
                    InvalidateAll(*g_activeSession);
                }
                else if (wParam == kWmLButtonUp && g_activeSession->selecting)
                {
                    g_activeSession->selection = NormalizeRect(g_activeSession->anchor, mouse->pt);
                    const bool cancelled = (g_activeSession->selection.right - g_activeSession->selection.left) < 2 ||
                        (g_activeSession->selection.bottom - g_activeSession->selection.top) < 2;
                    FinishSession(*g_activeSession, cancelled);
                    return 1;
                }
                else if (wParam == kWmRButtonDown)
                {
                    FinishSession(*g_activeSession, true);
                    return 1;
                }
            }
        }

        return CallNextHookEx(nullptr, code, wParam, lParam);
    }

    void InstallHooks(OverlaySession& session)
    {
        g_activeSession = &session;
        session.keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardHookProc, GetModuleHandleW(nullptr), 0);
        session.mouseHook = SetWindowsHookExW(WH_MOUSE_LL, MouseHookProc, GetModuleHandleW(nullptr), 0);
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

        if (!state)
        {
            return DefWindowProcW(hwnd, message, wParam, lParam);
        }

        auto* session = state->session;

        switch (message)
        {
        case WM_CREATE:
            EnsureOverlayResources(hwnd, *state);
            return 0;
        case WM_SIZE:
            EnsureOverlayResources(hwnd, *state);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_SETCURSOR:
            SetCursor(LoadCursorW(nullptr, IDC_CROSS));
            return TRUE;
        case WM_ERASEBKGND:
            return TRUE;
        case WM_KEYDOWN:
            if (session && wParam == VK_ESCAPE)
            {
                FinishSession(*session, true);
                return 0;
            }
            break;
        case WM_RBUTTONUP:
            if (session)
            {
                FinishSession(*session, true);
                return 0;
            }
            break;
        case WM_LBUTTONDOWN:
            if (session)
            {
                SetForegroundWindow(hwnd);
                session->selecting = true;
                session->cancelled = false;
                session->anchor = ClientToScreenPoint(hwnd, lParam);
                session->selection = NormalizeRect(session->anchor, session->anchor);
                InstallHooks(*session);
                InvalidateAll(*session);
                return 0;
            }
            break;
        case WM_PAINT:
        {
            PAINTSTRUCT paint{};
            HDC hdc = BeginPaint(hwnd, &paint);

            if (EnsureOverlayResources(hwnd, *state) && state->backBufferDc)
            {
                ComposeOverlayFrame(*state, state->backBufferDc);
                BitBlt(hdc, 0, 0, state->surfaceWidth, state->surfaceHeight, state->backBufferDc, 0, 0, SRCCOPY);
            }
            else
            {
                const int width = state->monitorBounds.right - state->monitorBounds.left;
                const int height = state->monitorBounds.bottom - state->monitorBounds.top;
                PatBlt(hdc, 0, 0, width, height, BLACKNESS);
            }

            EndPaint(hwnd, &paint);
            return 0;
        }
        case WM_DESTROY:
            ReleaseOverlayResources(*state);
            return 0;
        default:
            break;
        }

        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}

namespace oneshot
{
    void OverlayManager::Prewarm(HWND owner) const
    {
        EnsureOverlayClassRegistered();

        std::vector<RECT> monitorBounds;
        MonitorEnumData enumData{ &monitorBounds };
        EnumDisplayMonitors(nullptr, nullptr, EnumMonitorsProc, reinterpret_cast<LPARAM>(&enumData));
        if (monitorBounds.empty())
        {
            return;
        }

        std::vector<HWND> windows;
        windows.reserve(monitorBounds.size());

        for (const auto& bounds : monitorBounds)
        {
            auto* state = new OverlayWindowState();
            state->monitorBounds = bounds;

            HWND overlay = CreateWindowExW(
                WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
                L"OneShotNative.Overlay",
                L"OneShot Overlay",
                WS_POPUP,
                bounds.left,
                bounds.top,
                bounds.right - bounds.left,
                bounds.bottom - bounds.top,
                owner,
                nullptr,
                GetModuleHandleW(nullptr),
                state);

            if (!overlay)
            {
                delete state;
                continue;
            }

            windows.push_back(overlay);
            ShowWindow(overlay, SW_HIDE);
            UpdateWindow(overlay);
        }

        for (const auto overlay : windows)
        {
            auto* state = reinterpret_cast<OverlayWindowState*>(GetWindowLongPtrW(overlay, GWLP_USERDATA));
            DestroyWindow(overlay);
            delete state;
        }
    }

    std::optional<RECT> OverlayManager::SelectRegion(const CapturedImage& image, HWND owner) const
    {
        EnsureOverlayClassRegistered();

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
                L"OneShotNative.Overlay",
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
                RemoveHooks(session);
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
            RemoveHooks(session);
            return std::nullopt;
        }

        RemoveHooks(session);
        return session.selection;
    }
}
