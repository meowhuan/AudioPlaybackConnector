namespace AudioPlaybackConnector.WinUI3.Models;

public sealed class AppSettings
{
    public bool Reconnect { get; set; }

    public double Volume { get; set; } = 1.0;

    public bool DuckOtherApps { get; set; } = true;

    public double DuckedAppsVolume { get; set; } = 0.35;

    public bool ShowStartupToast { get; set; }

    public string OutputDeviceId { get; set; } = string.Empty;

    public List<string> LastDevices { get; set; } = [];

    public static AppSettings CreateDefault() => new();
}
