#include "oneshot_native/CommandProtocol.h"

#include <iomanip>
#include <random>

namespace
{
    std::wstring EscapeJsonString(std::wstring_view value)
    {
        std::wstring escaped;
        escaped.reserve(value.size());
        for (const wchar_t ch : value)
        {
            switch (ch)
            {
            case L'\\':
                escaped += L"\\\\";
                break;
            case L'"':
                escaped += L"\\\"";
                break;
            case L'\n':
                escaped += L"\\n";
                break;
            case L'\r':
                escaped += L"\\r";
                break;
            case L'\t':
                escaped += L"\\t";
                break;
            default:
                escaped.push_back(ch);
                break;
            }
        }

        return escaped;
    }

    std::optional<std::wstring> ExtractJsonString(std::wstring_view json, std::wstring_view key)
    {
        const auto probe = std::wstring(L"\"") + std::wstring(key) + L"\"";
        const auto keyPos = json.find(probe);
        if (keyPos == std::wstring_view::npos)
        {
            return std::nullopt;
        }

        const auto colonPos = json.find(L':', keyPos + probe.size());
        if (colonPos == std::wstring_view::npos)
        {
            return std::nullopt;
        }

        const auto quoteStart = json.find(L'"', colonPos + 1);
        if (quoteStart == std::wstring_view::npos)
        {
            return std::nullopt;
        }

        std::wstring value;
        bool escape = false;
        for (size_t index = quoteStart + 1; index < json.size(); ++index)
        {
            const wchar_t ch = json[index];
            if (escape)
            {
                switch (ch)
                {
                case L'n':
                    value.push_back(L'\n');
                    break;
                case L'r':
                    value.push_back(L'\r');
                    break;
                case L't':
                    value.push_back(L'\t');
                    break;
                case L'\\':
                    value.push_back(L'\\');
                    break;
                case L'"':
                    value.push_back(L'"');
                    break;
                default:
                    value.push_back(ch);
                    break;
                }

                escape = false;
                continue;
            }

            if (ch == L'\\')
            {
                escape = true;
                continue;
            }

            if (ch == L'"')
            {
                return value;
            }

            value.push_back(ch);
        }

        return std::nullopt;
    }

    int ExtractJsonInt(std::wstring_view json, std::wstring_view key, int fallback)
    {
        const auto probe = std::wstring(L"\"") + std::wstring(key) + L"\"";
        const auto keyPos = json.find(probe);
        if (keyPos == std::wstring_view::npos)
        {
            return fallback;
        }

        const auto colonPos = json.find(L':', keyPos + probe.size());
        if (colonPos == std::wstring_view::npos)
        {
            return fallback;
        }

        const auto valueStart = json.find_first_of(L"-0123456789", colonPos + 1);
        if (valueStart == std::wstring_view::npos)
        {
            return fallback;
        }

        const auto valueEnd = json.find_first_not_of(L"0123456789", valueStart + 1);
        return std::stoi(std::wstring(json.substr(valueStart, valueEnd - valueStart)));
    }
}

namespace oneshot
{
    std::wstring NewRequestId()
    {
        static thread_local std::mt19937_64 generator{std::random_device{}()};
        std::uniform_int_distribution<unsigned long long> distribution;

        std::wstringstream builder;
        builder << std::hex << distribution(generator) << distribution(generator);
        return builder.str();
    }

    std::wstring CurrentIso8601Utc()
    {
        SYSTEMTIME systemTime{};
        GetSystemTime(&systemTime);

        std::wstringstream builder;
        builder << std::setfill(L'0')
                << std::setw(4) << systemTime.wYear
                << L"-" << std::setw(2) << systemTime.wMonth
                << L"-" << std::setw(2) << systemTime.wDay
                << L"T" << std::setw(2) << systemTime.wHour
                << L":" << std::setw(2) << systemTime.wMinute
                << L":" << std::setw(2) << systemTime.wSecond
                << L"Z";
        return builder.str();
    }

    std::wstring SerializeEnvelope(const CommandEnvelope& envelope)
    {
        std::wstringstream builder;
        builder << L"{\"v\":" << envelope.version
                << L",\"command\":\"" << EscapeJsonString(envelope.command)
                << L"\",\"request_id\":\"" << EscapeJsonString(envelope.requestId)
                << L"\",\"sent_at\":\"" << EscapeJsonString(envelope.sentAt)
                << L"\"}\n";
        return builder.str();
    }

    std::wstring SerializeResponse(const CommandResponse& response)
    {
        std::wstringstream builder;
        builder << L"{\"ok\":" << (response.ok ? L"true" : L"false")
                << L",\"request_id\":\"" << EscapeJsonString(response.requestId)
                << L"\",\"message\":\"" << EscapeJsonString(response.message)
                << L"\"}\n";
        return builder.str();
    }

    std::optional<CommandEnvelope> ParseEnvelope(const std::wstring& jsonLine)
    {
        auto command = ExtractJsonString(jsonLine, L"command");
        auto requestId = ExtractJsonString(jsonLine, L"request_id");
        auto sentAt = ExtractJsonString(jsonLine, L"sent_at");

        if (!command.has_value() || !requestId.has_value())
        {
            return std::nullopt;
        }

        CommandEnvelope envelope{};
        envelope.version = ExtractJsonInt(jsonLine, L"v", 1);
        envelope.command = *command;
        envelope.requestId = *requestId;
        envelope.sentAt = sentAt.value_or(L"");
        return envelope;
    }

    std::optional<CommandResponse> ParseResponse(const std::wstring& jsonLine)
    {
        auto requestId = ExtractJsonString(jsonLine, L"request_id");
        auto message = ExtractJsonString(jsonLine, L"message");

        if (!requestId.has_value() || !message.has_value())
        {
            return std::nullopt;
        }

        CommandResponse response{};
        response.ok = jsonLine.find(L"\"ok\":true") != std::wstring::npos;
        response.requestId = *requestId;
        response.message = *message;
        return response;
    }
}
