#pragma once

#include "oneshot_native/CaptureTypes.h"

namespace oneshot
{
    class OverlayManager
    {
    public:
        void Prewarm(HWND owner) const;
        [[nodiscard]] std::optional<RECT> SelectRegion(const CapturedImage& image, HWND owner) const;
    };
}
