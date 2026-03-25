namespace AudioPlaybackConnector.WinUI3.Models;

public sealed class DebugLogEntry
{
    public DateTimeOffset Timestamp { get; init; } = DateTimeOffset.Now;

    public string Category { get; init; } = string.Empty;

    public string Message { get; init; } = string.Empty;

    public string TimestampLabel => Timestamp.ToLocalTime().ToString("HH:mm:ss");
}
