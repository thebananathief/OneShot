#include "oneshot_native/CommandClient.h"

namespace oneshot
{
    bool CommandClient::Send(const CommandEnvelope& envelope, DWORD timeoutMilliseconds, std::wstring& responseText) const
    {
        if (!WaitNamedPipeW(kPipeName, timeoutMilliseconds))
        {
            return false;
        }

        HANDLE pipe = CreateFileW(kPipeName, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (pipe == INVALID_HANDLE_VALUE)
        {
            return false;
        }

        const auto request = SerializeEnvelope(envelope);
        DWORD written = 0;
        const auto writeOk = WriteFile(
            pipe,
            request.c_str(),
            static_cast<DWORD>(request.size() * sizeof(wchar_t)),
            &written,
            nullptr);

        if (!writeOk)
        {
            CloseHandle(pipe);
            return false;
        }

        wchar_t buffer[2048]{};
        DWORD read = 0;
        const auto readOk = ReadFile(pipe, buffer, sizeof(buffer) - sizeof(wchar_t), &read, nullptr);
        CloseHandle(pipe);

        if (!readOk)
        {
            return false;
        }

        responseText.assign(buffer, read / sizeof(wchar_t));
        return true;
    }
}
