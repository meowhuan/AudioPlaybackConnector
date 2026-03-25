namespace AudioPlaybackConnector.WinUI3.Models;

public sealed class LegacyHostStatus
{
    public bool IsRunning { get; init; }

    public bool HasWindow { get; init; }

    public string? ExecutablePath { get; init; }

    public string ConfigPath { get; init; } = string.Empty;

    public string StateLabel => IsRunning
        ? (HasWindow ? "运行中 / 就绪" : "运行中 / 启动中")
        : "已停止";
}
