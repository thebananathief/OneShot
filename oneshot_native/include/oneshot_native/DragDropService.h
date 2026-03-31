#pragma once

#include "oneshot_native/Common.h"

namespace oneshot
{
    struct DragDropDebugInfo
    {
        HRESULT hresult{S_OK};
        DWORD effect{DROPEFFECT_NONE};
        std::wstring result;
    };

    class DragDropService
    {
    public:
        [[nodiscard]] bool StartFileDrag(HWND originWindow, const std::filesystem::path& filePath, std::wstring& error, DragDropDebugInfo* debugInfo = nullptr) const;
    };
}
