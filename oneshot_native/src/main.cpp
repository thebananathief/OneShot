#include "oneshot_native/AppHost.h"

#include <ole2.h>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    const HRESULT initHr = OleInitialize(nullptr);
    if (FAILED(initHr))
    {
        return static_cast<int>(initHr);
    }

    const auto mode = oneshot::ParseStartupMode();
    oneshot::AppHost host(mode);
    const auto exitCode = host.Run();

    if (initHr == S_OK || initHr == S_FALSE)
    {
        OleUninitialize();
    }

    return exitCode;
}
