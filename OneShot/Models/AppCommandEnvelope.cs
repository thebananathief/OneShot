using System.Text.Json;

namespace OneShot.Models;

internal sealed class AppCommandEnvelope
{
    public AppCommand Command { get; init; }

    public string InvocationId { get; init; } = string.Empty;

    public DateTimeOffset ClientStartedAtUtc { get; init; }

    public long ClientStartTimestamp { get; init; }

    public bool TraceEnabled { get; init; }

    public static AppCommandEnvelope Create(AppCommand command, bool traceEnabled = true)
    {
        return new AppCommandEnvelope
        {
            Command = command,
            InvocationId = Guid.NewGuid().ToString("N"),
            ClientStartedAtUtc = DateTimeOffset.UtcNow,
            ClientStartTimestamp = Environment.TickCount64,
            TraceEnabled = traceEnabled
        };
    }

    public static AppCommandEnvelope CreateLegacy(AppCommand command)
    {
        return Create(command, traceEnabled: false);
    }

    public string ToPayload()
    {
        return JsonSerializer.Serialize(this);
    }

    public static bool TryParse(string payload, out AppCommandEnvelope? envelope)
    {
        envelope = null;
        if (string.IsNullOrWhiteSpace(payload))
        {
            return false;
        }

        try
        {
            var parsed = JsonSerializer.Deserialize<AppCommandEnvelope>(payload);
            if (parsed is null || parsed.Command == AppCommand.None || string.IsNullOrWhiteSpace(parsed.InvocationId))
            {
                return false;
            }

            envelope = parsed;
            return true;
        }
        catch (JsonException)
        {
            return false;
        }
    }
}
