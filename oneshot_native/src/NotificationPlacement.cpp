#include "oneshot_native/NotificationPlacement.h"

#include <cwctype>

namespace
{
    std::wstring NormalizeToken(std::wstring_view value)
    {
        std::wstring normalized;
        normalized.reserve(value.size());
        for (const wchar_t ch : value)
        {
            if (ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n' || ch == L'-' || ch == L'_')
            {
                continue;
            }

            normalized.push_back(static_cast<wchar_t>(std::towlower(ch)));
        }

        return normalized;
    }
}

namespace oneshot
{
    NotificationPlacement DefaultNotificationPlacement() noexcept
    {
        return NotificationPlacement{};
    }

    NotificationGrowDirection DefaultGrowDirection(NotificationAnchor anchor) noexcept
    {
        switch (anchor)
        {
        case NotificationAnchor::TopLeft:
        case NotificationAnchor::TopRight:
            return NotificationGrowDirection::Down;
        case NotificationAnchor::BottomLeft:
        case NotificationAnchor::BottomRight:
            return NotificationGrowDirection::Up;
        default:
            return NotificationGrowDirection::Down;
        }
    }

    bool IsValidGrowDirection(NotificationAnchor anchor, NotificationGrowDirection growDirection) noexcept
    {
        switch (anchor)
        {
        case NotificationAnchor::TopLeft:
            return growDirection == NotificationGrowDirection::Right || growDirection == NotificationGrowDirection::Down;
        case NotificationAnchor::BottomLeft:
            return growDirection == NotificationGrowDirection::Right || growDirection == NotificationGrowDirection::Up;
        case NotificationAnchor::TopRight:
            return growDirection == NotificationGrowDirection::Left || growDirection == NotificationGrowDirection::Down;
        case NotificationAnchor::BottomRight:
            return growDirection == NotificationGrowDirection::Left || growDirection == NotificationGrowDirection::Up;
        default:
            return false;
        }
    }

    std::array<NotificationGrowDirection, 2> ValidGrowDirections(NotificationAnchor anchor) noexcept
    {
        switch (anchor)
        {
        case NotificationAnchor::TopLeft:
            return { NotificationGrowDirection::Right, NotificationGrowDirection::Down };
        case NotificationAnchor::BottomLeft:
            return { NotificationGrowDirection::Right, NotificationGrowDirection::Up };
        case NotificationAnchor::TopRight:
            return { NotificationGrowDirection::Left, NotificationGrowDirection::Down };
        case NotificationAnchor::BottomRight:
            return { NotificationGrowDirection::Left, NotificationGrowDirection::Up };
        default:
            return { NotificationGrowDirection::Left, NotificationGrowDirection::Down };
        }
    }

    NotificationPlacement CoercePlacementForAnchor(NotificationAnchor anchor, NotificationGrowDirection preferredDirection) noexcept
    {
        NotificationPlacement placement{};
        placement.anchor = anchor;
        placement.growDirection = IsValidGrowDirection(anchor, preferredDirection)
            ? preferredDirection
            : DefaultGrowDirection(anchor);
        return placement;
    }

    NotificationPlacement NormalizeNotificationPlacement(NotificationPlacement placement) noexcept
    {
        return CoercePlacementForAnchor(placement.anchor, placement.growDirection);
    }

    std::wstring_view NotificationAnchorToString(NotificationAnchor anchor) noexcept
    {
        switch (anchor)
        {
        case NotificationAnchor::TopLeft:
            return L"top-left";
        case NotificationAnchor::BottomLeft:
            return L"bottom-left";
        case NotificationAnchor::TopRight:
            return L"top-right";
        case NotificationAnchor::BottomRight:
            return L"bottom-right";
        default:
            return L"top-right";
        }
    }

    std::wstring_view NotificationGrowDirectionToString(NotificationGrowDirection growDirection) noexcept
    {
        switch (growDirection)
        {
        case NotificationGrowDirection::Left:
            return L"left";
        case NotificationGrowDirection::Right:
            return L"right";
        case NotificationGrowDirection::Up:
            return L"up";
        case NotificationGrowDirection::Down:
            return L"down";
        default:
            return L"down";
        }
    }

    std::optional<NotificationAnchor> ParseNotificationAnchor(std::wstring_view value)
    {
        const std::wstring normalized = NormalizeToken(value);
        if (normalized == L"topleft")
        {
            return NotificationAnchor::TopLeft;
        }
        if (normalized == L"bottomleft")
        {
            return NotificationAnchor::BottomLeft;
        }
        if (normalized == L"topright")
        {
            return NotificationAnchor::TopRight;
        }
        if (normalized == L"bottomright")
        {
            return NotificationAnchor::BottomRight;
        }

        return std::nullopt;
    }

    std::optional<NotificationGrowDirection> ParseNotificationGrowDirection(std::wstring_view value)
    {
        const std::wstring normalized = NormalizeToken(value);
        if (normalized == L"left")
        {
            return NotificationGrowDirection::Left;
        }
        if (normalized == L"right")
        {
            return NotificationGrowDirection::Right;
        }
        if (normalized == L"up")
        {
            return NotificationGrowDirection::Up;
        }
        if (normalized == L"down")
        {
            return NotificationGrowDirection::Down;
        }

        return std::nullopt;
    }
}
