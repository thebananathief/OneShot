#pragma once

#include "oneshot_native/Common.h"

namespace oneshot
{
    struct CommandEnvelope
    {
        int version{1};
        std::wstring command;
        std::wstring requestId;
        std::wstring sentAt;
    };

    struct CommandResponse
    {
        bool ok{false};
        std::wstring requestId;
        std::wstring message;
    };

    std::wstring NewRequestId();
    std::wstring CurrentIso8601Utc();
    std::wstring SerializeEnvelope(const CommandEnvelope& envelope);
    std::wstring SerializeResponse(const CommandResponse& response);
    std::optional<CommandEnvelope> ParseEnvelope(const std::wstring& jsonLine);
    std::optional<CommandResponse> ParseResponse(const std::wstring& jsonLine);
}
