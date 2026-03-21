#pragma once

#include "oneshot_native/CaptureTypes.h"

namespace oneshot
{
    class OverlayManager
    {
    public:
        [[nodiscard]] std::optional<RECT> SelectRegion(const CapturedImage& image, HWND owner) const;
    };
}
