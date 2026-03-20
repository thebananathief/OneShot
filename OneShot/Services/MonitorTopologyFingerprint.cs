namespace OneShot.Services;

internal sealed class MonitorTopologyFingerprint : IEquatable<MonitorTopologyFingerprint>
{
    private readonly string _key;

    private MonitorTopologyFingerprint(IReadOnlyList<System.Drawing.Rectangle> monitorBounds, string key)
    {
        MonitorBounds = monitorBounds;
        _key = key;
    }

    public IReadOnlyList<System.Drawing.Rectangle> MonitorBounds { get; }

    public static MonitorTopologyFingerprint Create(IReadOnlyList<System.Drawing.Rectangle> monitorBounds)
    {
        var normalized = monitorBounds
            .Select(bounds => new System.Drawing.Rectangle(bounds.X, bounds.Y, bounds.Width, bounds.Height))
            .ToArray();
        var key = string.Join(
            "|",
            normalized.Select(bounds => $"{bounds.Left},{bounds.Top},{bounds.Width},{bounds.Height}"));
        return new MonitorTopologyFingerprint(normalized, key);
    }

    public bool Equals(MonitorTopologyFingerprint? other)
    {
        return other is not null && string.Equals(_key, other._key, StringComparison.Ordinal);
    }

    public override bool Equals(object? obj)
    {
        return Equals(obj as MonitorTopologyFingerprint);
    }

    public override int GetHashCode()
    {
        return StringComparer.Ordinal.GetHashCode(_key);
    }
}
