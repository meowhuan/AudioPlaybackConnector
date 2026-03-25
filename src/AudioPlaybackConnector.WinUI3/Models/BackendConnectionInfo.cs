namespace AudioPlaybackConnector.WinUI3.Models;

public sealed class BackendConnectionInfo
{
    public string Id { get; set; } = string.Empty;

    public string Name { get; set; } = string.Empty;

    public string State { get; set; } = "unknown";

    public int StateValue { get; set; } = -1;
}
