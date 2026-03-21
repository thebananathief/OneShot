#pragma once

#include "oneshot_native/CommandProtocol.h"

namespace oneshot
{
    class CommandServer
    {
    public:
        using CommandHandler = std::function<CommandResponse(const CommandEnvelope&)>;

        explicit CommandServer(CommandHandler handler);
        ~CommandServer();

        bool Start();
        void Stop();

    private:
        void Run();

        CommandHandler _handler;
        std::atomic<bool> _running{false};
        std::thread _thread;
    };
}
