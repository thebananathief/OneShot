#pragma once

#include "oneshot_native/CaptureTypes.h"

namespace oneshot
{
    class CaptureService
    {
    public:
        [[nodiscard]] std::optional<CapturedImage> CaptureVirtualScreen() const;
        [[nodiscard]] std::optional<CapturedImage> Crop(const CapturedImage& source, const RECT& selection) const;
    };
}
