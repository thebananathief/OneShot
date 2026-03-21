#include "oneshot_native/AppHost.h"

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const auto mode = oneshot::ParseStartupMode();
    oneshot::AppHost host(mode);
    const auto exitCode = host.Run();
    CoUninitialize();
    return exitCode;
}
