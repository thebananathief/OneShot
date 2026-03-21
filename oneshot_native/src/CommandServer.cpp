#include "oneshot_native/CommandServer.h"

namespace oneshot
{
    CommandServer::CommandServer(CommandHandler handler)
        : _handler(std::move(handler))
    {
    }

    CommandServer::~CommandServer()
    {
        Stop();
    }

    bool CommandServer::Start()
    {
        if (_running.exchange(true))
        {
            return true;
        }

        _thread = std::thread(&CommandServer::Run, this);
        return true;
    }

    void CommandServer::Stop()
    {
        if (!_running.exchange(false))
        {
            return;
        }

        HANDLE wakePipe = CreateFileW(kPipeName, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (wakePipe != INVALID_HANDLE_VALUE)
        {
            CloseHandle(wakePipe);
        }

        if (_thread.joinable())
        {
            _thread.join();
        }
    }

    void CommandServer::Run()
    {
        while (_running.load())
        {
            HANDLE pipe = CreateNamedPipeW(
                kPipeName,
                PIPE_ACCESS_DUPLEX,
                PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                PIPE_UNLIMITED_INSTANCES,
                4096,
                4096,
                0,
                nullptr);

            if (pipe == INVALID_HANDLE_VALUE)
            {
                return;
            }

            const auto connected = ConnectNamedPipe(pipe, nullptr) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
            if (!connected)
            {
                CloseHandle(pipe);
                continue;
            }

            wchar_t buffer[2048]{};
            DWORD read = 0;
            if (ReadFile(pipe, buffer, sizeof(buffer) - sizeof(wchar_t), &read, nullptr) && read > 0)
            {
                std::wstring request(buffer, read / sizeof(wchar_t));
                CommandResponse response{};

                if (const auto parsed = ParseEnvelope(request))
                {
                    response = _handler(*parsed);
                }
                else
                {
                    response.ok = false;
                    response.message = L"invalid command envelope";
                }

                const auto payload = SerializeResponse(response);
                DWORD written = 0;
                WriteFile(pipe, payload.c_str(), static_cast<DWORD>(payload.size() * sizeof(wchar_t)), &written, nullptr);
            }

            FlushFileBuffers(pipe);
            DisconnectNamedPipe(pipe);
            CloseHandle(pipe);
        }
    }
}
