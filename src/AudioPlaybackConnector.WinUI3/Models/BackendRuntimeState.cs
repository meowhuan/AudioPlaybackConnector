namespace AudioPlaybackConnector.WinUI3.Models;

public sealed class BackendRuntimeState
{
    public bool Running { get; set; }

    public string Reason { get; set; } = string.Empty;

    public uint ProcessId { get; set; }

    public bool Reconnect { get; set; }

    public double Volume { get; set; } = 1.0;

    public bool DuckOtherApps { get; set; } = true;

    public double DuckedAppsVolume { get; set; } = 0.35;

    public bool ShowStartupToast { get; set; }

    public string OutputDeviceId { get; set; } = string.Empty;

    public string ActiveOutputDeviceId { get; set; } = string.Empty;

    public bool SoftReconnectInProgress { get; set; }

    public List<BackendConnectionInfo> Connections { get; set; } = [];
}
