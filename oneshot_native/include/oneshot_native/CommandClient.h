#pragma once

#include "oneshot_native/CommandProtocol.h"

namespace oneshot
{
    class CommandClient
    {
    public:
        bool Send(const CommandEnvelope& envelope, DWORD timeoutMilliseconds, std::wstring& responseText) const;
    };
}
