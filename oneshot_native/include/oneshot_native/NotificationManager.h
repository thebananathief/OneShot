#pragma once

#include "oneshot_native/CaptureTypes.h"
#include "oneshot_native/DragDropService.h"
#include "oneshot_native/PathService.h"

namespace oneshot
{
    class NotificationManager
    {
    public:
        struct NotificationWindow;

        explicit NotificationManager(AppPaths paths);
        ~NotificationManager();

        void Show(HWND owner, const CapturedImage& image, const std::filesystem::path& savedPath, const std::filesystem::path& dragPath);
        void CloseAll();
        void CloseNotification(NotificationWindow* notification);
        void StartDrag(NotificationWindow* notification);

    private:
        void RepositionAll();

        AppPaths _paths;
        DragDropService _dragDrop;
        std::vector<std::unique_ptr<NotificationWindow>> _notifications;
    };
}
