#pragma once

#include "oneshot_native/CaptureTypes.h"
#include "oneshot_native/DragDropService.h"
#include "oneshot_native/MarkupEditorWindow.h"
#include "oneshot_native/PathService.h"

namespace oneshot
{
    class NotificationManager
    {
    public:
        struct NotificationWindow;

        NotificationManager(AppPaths paths, OutputService& outputService);
        ~NotificationManager();

        void Show(HWND owner, CapturedImage image, const std::filesystem::path& savedPath, const std::filesystem::path& dragPath);
        void CloseAll();
        void CloseNotification(NotificationWindow* notification);
        void StartDrag(NotificationWindow* notification);
        void OpenMarkup(NotificationWindow* notification);

    private:
        void RepositionAll();

        AppPaths _paths;
        DragDropService _dragDrop;
        MarkupEditorWindow _markupEditor;
        OutputService& _outputService;
        std::vector<std::unique_ptr<NotificationWindow>> _notifications;
    };
}
