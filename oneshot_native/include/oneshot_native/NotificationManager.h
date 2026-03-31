#pragma once

#include "oneshot_native/CaptureTypes.h"
#include "oneshot_native/DragDropService.h"
#include "oneshot_native/MarkupEditorSettingsStore.h"
#include "oneshot_native/MarkupEditorWindow.h"
#include "oneshot_native/NotificationPlacement.h"
#include "oneshot_native/NotificationSettingsStore.h"
#include "oneshot_native/PathService.h"

namespace oneshot
{
    struct NotificationDebugState
    {
        std::wstring lastAttemptUtc;
        std::wstring lastFailedStep;
        std::wstring lastErrorText;
        DWORD lastErrorCode{0};
        bool showAttempted{false};
        bool windowCreated{false};
        bool thumbnailCreated{false};
        bool dismissButtonCreated{false};
        bool copyButtonCreated{false};
        bool markupButtonCreated{false};
        int showWindowResult{0};
        bool windowVisible{false};
        RECT windowRect{};
        bool lastDragAttempted{false};
        UINT_PTR lastDragNotificationHwnd{0};
        UINT_PTR lastDragSourceHwnd{0};
        std::wstring lastDragPath;
        bool lastDragPathExists{false};
        std::wstring lastDragResult;
        HRESULT lastDragHresult{S_OK};
        DWORD lastDragEffect{DROPEFFECT_NONE};
        NotificationPlacement placement{};
    };

    class NotificationManager
    {
    public:
        struct NotificationWindow;

        NotificationManager(AppPaths paths, OutputService& outputService);
        ~NotificationManager();

        void Show(HWND owner, CapturedImage image, const std::filesystem::path& savedPath, const std::filesystem::path& dragPath);
        void CloseAll();
        void CloseNotification(NotificationWindow* notification);
        void StartDrag(NotificationWindow* notification, HWND sourceWindow);
        void CopyToClipboard(NotificationWindow* notification);
        void OpenMarkup(NotificationWindow* notification);
        void SetPlacement(NotificationPlacement placement);
        [[nodiscard]] const NotificationPlacement& GetPlacement() const noexcept { return _placement; }
        [[nodiscard]] const NotificationDebugState& GetDebugState() const noexcept { return _debugState; }

    private:
        void RepositionAll();

        AppPaths _paths;
        DragDropService _dragDrop;
        MarkupEditorSettingsStore _markupEditorSettings;
        NotificationSettingsStore _settingsStore;
        MarkupEditorWindow _markupEditor;
        OutputService& _outputService;
        std::vector<std::unique_ptr<NotificationWindow>> _notifications;
        NotificationPlacement _placement;
        NotificationDebugState _debugState;
    };
}
