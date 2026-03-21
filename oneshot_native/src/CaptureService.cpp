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

    std::optional<CapturedImage> CaptureService::Crop(const CapturedImage& source, const RECT& selection) const
    {
        if (!source.IsValid())
        {
            return std::nullopt;
        }

        const LONG sourceLeft = static_cast<LONG>(source.x);
        const LONG sourceTop = static_cast<LONG>(source.y);
        const LONG sourceRight = static_cast<LONG>(source.x + source.width);
        const LONG sourceBottom = static_cast<LONG>(source.y + source.height);

        const int left = static_cast<int>(std::max(sourceLeft, selection.left));
        const int top = static_cast<int>(std::max(sourceTop, selection.top));
        const int right = static_cast<int>(std::min(sourceRight, selection.right));
        const int bottom = static_cast<int>(std::min(sourceBottom, selection.bottom));
        const int width = right - left;
        const int height = bottom - top;

        if (width <= 0 || height <= 0)
        {
            return std::nullopt;
        }

        HDC screenDc = GetDC(nullptr);
        HDC sourceDc = CreateCompatibleDC(screenDc);
        HDC targetDc = CreateCompatibleDC(screenDc);
        if (!screenDc || !sourceDc || !targetDc)
        {
            if (sourceDc) DeleteDC(sourceDc);
            if (targetDc) DeleteDC(targetDc);
            if (screenDc) ReleaseDC(nullptr, screenDc);
            return std::nullopt;
        }

        HGDIOBJ sourcePrev = SelectObject(sourceDc, source.bitmap);
        HBITMAP targetBitmap = CreateCompatibleBitmap(screenDc, width, height);
        HGDIOBJ targetPrev = SelectObject(targetDc, targetBitmap);
        BitBlt(targetDc, 0, 0, width, height, sourceDc, left - source.x, top - source.y, SRCCOPY);
        SelectObject(sourceDc, sourcePrev);
        SelectObject(targetDc, targetPrev);
        DeleteDC(sourceDc);
        DeleteDC(targetDc);
        ReleaseDC(nullptr, screenDc);

        if (!targetBitmap)
        {
            return std::nullopt;
        }

        CapturedImage image;
        image.bitmap = targetBitmap;
        image.x = left;
        image.y = top;
        image.width = width;
        image.height = height;
        image.capturedAtUtc = source.capturedAtUtc;
        return image;
    }
}
