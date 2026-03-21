#include "oneshot_native/CaptureService.h"

namespace oneshot
{
    std::optional<CapturedImage> CaptureService::CaptureVirtualScreen() const
    {
        const int originX = GetSystemMetrics(SM_XVIRTUALSCREEN);
        const int originY = GetSystemMetrics(SM_YVIRTUALSCREEN);
        const int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        const int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);

        if (width <= 0 || height <= 0)
        {
            return std::nullopt;
        }

        HDC screenDc = GetDC(nullptr);
        if (!screenDc)
        {
            return std::nullopt;
        }

        HDC memoryDc = CreateCompatibleDC(screenDc);
        if (!memoryDc)
        {
            ReleaseDC(nullptr, screenDc);
            return std::nullopt;
        }

        HBITMAP bitmap = CreateCompatibleBitmap(screenDc, width, height);
        if (!bitmap)
        {
            DeleteDC(memoryDc);
            ReleaseDC(nullptr, screenDc);
            return std::nullopt;
        }

        HGDIOBJ previousBitmap = SelectObject(memoryDc, bitmap);
        const BOOL copied = BitBlt(memoryDc, 0, 0, width, height, screenDc, originX, originY, SRCCOPY | CAPTUREBLT);
        SelectObject(memoryDc, previousBitmap);
        DeleteDC(memoryDc);
        ReleaseDC(nullptr, screenDc);

        if (!copied)
        {
            DeleteObject(bitmap);
            return std::nullopt;
        }

        CapturedImage image;
        image.bitmap = bitmap;
        image.x = originX;
        image.y = originY;
        image.width = width;
        image.height = height;
        GetSystemTime(&image.capturedAtUtc);
        return image;
    }
}
