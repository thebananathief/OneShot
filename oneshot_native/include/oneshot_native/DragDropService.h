#pragma once

#include "oneshot_native/Common.h"

namespace oneshot
{
    class DragDropService
    {
    public:
        [[nodiscard]] bool StartFileDrag(HWND sourceWindow, const std::filesystem::path& filePath, std::wstring& error) const;
    };
}
