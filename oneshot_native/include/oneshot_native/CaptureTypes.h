#pragma once

#include "oneshot_native/Common.h"

namespace oneshot
{
    struct CapturedImage
    {
        HBITMAP bitmap{nullptr};
        int x{0};
        int y{0};
        int width{0};
        int height{0};
        SYSTEMTIME capturedAtUtc{};

        CapturedImage() = default;
        CapturedImage(const CapturedImage&) = delete;
        CapturedImage& operator=(const CapturedImage&) = delete;

        CapturedImage(CapturedImage&& other) noexcept
            : bitmap(std::exchange(other.bitmap, nullptr))
            , x(other.x)
            , y(other.y)
            , width(other.width)
            , height(other.height)
            , capturedAtUtc(other.capturedAtUtc)
        {
        }

        CapturedImage& operator=(CapturedImage&& other) noexcept
        {
            if (this == &other)
            {
                return *this;
            }

            Reset();
            bitmap = std::exchange(other.bitmap, nullptr);
            x = other.x;
            y = other.y;
            width = other.width;
            height = other.height;
            capturedAtUtc = other.capturedAtUtc;
            return *this;
        }

        ~CapturedImage()
        {
            Reset();
        }

        [[nodiscard]] bool IsValid() const noexcept
        {
            return bitmap != nullptr && width > 0 && height > 0;
        }

        void Reset() noexcept
        {
            if (bitmap)
            {
                DeleteObject(bitmap);
                bitmap = nullptr;
            }
        }
    };
}
