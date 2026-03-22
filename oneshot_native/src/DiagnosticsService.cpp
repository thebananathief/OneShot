#include "oneshot_native/DiagnosticsService.h"

namespace
{
    std::wstring BoolText(bool value)
    {
        return value ? L"true" : L"false";
    }

    std::wstring RectText(const RECT& rect)
    {
        std::wstringstream builder;
        builder << L"{ left: " << rect.left
                << L", top: " << rect.top
                << L", right: " << rect.right
                << L", bottom: " << rect.bottom
                << L" }";
        return builder.str();
    }
}

namespace oneshot
{
    DiagnosticsService::DiagnosticsService(AppPaths paths)
        : _paths(std::move(paths))
    {
    }

    std::wstring DiagnosticsService::BuildDiagnosticsText(bool startupEnabled, bool snapshotActive, const NotificationDebugState& notificationDebug) const
    {
        std::wstringstream builder;
        builder << L"{\n"
                << L"  release: \"" << ONESHOT_VERSION << L"\",\n"
                << L"  process_id: " << GetCurrentProcessId() << L",\n"
                << L"  startup_enabled: " << BoolText(startupEnabled) << L",\n"
                << L"  pipe_name: \"" << kPipeName << L"\",\n"
                << L"  save_dir: \"" << ToWideString(_paths.screenshotsDirectory) << L"\",\n"
                << L"  temp_dir: \"" << ToWideString(_paths.tempDirectory) << L"\",\n"
                << L"  log_dir: \"" << ToWideString(_paths.logDirectory) << L"\",\n"
                << L"  monitor_count: " << GetSystemMetrics(SM_CMONITORS) << L",\n"
                << L"  running: true,\n"
                << L"  snapshot_active: " << BoolText(snapshotActive) << L",\n"
                << L"  state: \"" << (snapshotActive ? L"busy" : L"idle") << L"\",\n"
                << L"  notification: {\n"
                << L"    last_attempt_utc: \"" << notificationDebug.lastAttemptUtc << L"\",\n"
                << L"    last_failed_step: \"" << notificationDebug.lastFailedStep << L"\",\n"
                << L"    last_error_code: " << notificationDebug.lastErrorCode << L",\n"
                << L"    last_error_text: \"" << notificationDebug.lastErrorText << L"\",\n"
                << L"    show_attempted: " << BoolText(notificationDebug.showAttempted) << L",\n"
                << L"    window_created: " << BoolText(notificationDebug.windowCreated) << L",\n"
                << L"    thumbnail_created: " << BoolText(notificationDebug.thumbnailCreated) << L",\n"
                << L"    dismiss_button_created: " << BoolText(notificationDebug.dismissButtonCreated) << L",\n"
                << L"    markup_button_created: " << BoolText(notificationDebug.markupButtonCreated) << L",\n"
                << L"    show_window_result: " << notificationDebug.showWindowResult << L",\n"
                << L"    set_window_pos_succeeded: " << BoolText(notificationDebug.setWindowPosSucceeded) << L",\n"
                << L"    has_visible_style: " << BoolText(notificationDebug.hasVisibleStyle) << L",\n"
                << L"    rect_intersects_work_area: " << BoolText(notificationDebug.rectIntersectsWorkArea) << L",\n"
                << L"    cloaked: " << BoolText(notificationDebug.cloaked) << L",\n"
                << L"    window_placement_show_cmd: " << notificationDebug.windowPlacementShowCmd << L",\n"
                << L"    displayed_on_screen: " << BoolText(notificationDebug.displayedOnScreen) << L",\n"
                << L"    window_rect: " << RectText(notificationDebug.windowRect) << L",\n"
                << L"    work_area_rect: " << RectText(notificationDebug.workAreaRect) << L"\n"
                << L"  }\n"
                << L"}\n";
        return builder.str();
    }
}
