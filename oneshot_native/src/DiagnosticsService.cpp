#include "oneshot_native/DiagnosticsService.h"

namespace
{
    std::wstring BoolText(bool value)
    {
        return value ? L"true" : L"false";
    }
}

namespace oneshot
{
    DiagnosticsService::DiagnosticsService(AppPaths paths)
        : _paths(std::move(paths))
    {
    }

    std::wstring DiagnosticsService::BuildDiagnosticsText(bool startupEnabled, bool snapshotActive) const
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
                << L"  state: \"" << (snapshotActive ? L"busy" : L"idle") << L"\"\n"
                << L"}\n";
        return builder.str();
    }
}
