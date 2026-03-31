#pragma once

#include "oneshot_native/Common.h"

#include <array>

namespace oneshot
{
    enum class NotificationAnchor
    {
        TopLeft,
        BottomLeft,
        TopRight,
        BottomRight
    };

    enum class NotificationGrowDirection
    {
        Left,
        Right,
        Up,
        Down
    };

    struct NotificationPlacement
    {
        NotificationAnchor anchor{NotificationAnchor::TopRight};
        NotificationGrowDirection growDirection{NotificationGrowDirection::Down};
    };

    [[nodiscard]] NotificationPlacement DefaultNotificationPlacement() noexcept;
    [[nodiscard]] NotificationGrowDirection DefaultGrowDirection(NotificationAnchor anchor) noexcept;
    [[nodiscard]] bool IsValidGrowDirection(NotificationAnchor anchor, NotificationGrowDirection growDirection) noexcept;
    [[nodiscard]] std::array<NotificationGrowDirection, 2> ValidGrowDirections(NotificationAnchor anchor) noexcept;
    [[nodiscard]] NotificationPlacement CoercePlacementForAnchor(NotificationAnchor anchor, NotificationGrowDirection preferredDirection) noexcept;
    [[nodiscard]] NotificationPlacement NormalizeNotificationPlacement(NotificationPlacement placement) noexcept;
    [[nodiscard]] std::wstring_view NotificationAnchorToString(NotificationAnchor anchor) noexcept;
    [[nodiscard]] std::wstring_view NotificationGrowDirectionToString(NotificationGrowDirection growDirection) noexcept;
    [[nodiscard]] std::optional<NotificationAnchor> ParseNotificationAnchor(std::wstring_view value);
    [[nodiscard]] std::optional<NotificationGrowDirection> ParseNotificationGrowDirection(std::wstring_view value);
}
