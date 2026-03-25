namespace AudioPlaybackConnector.WinUI3.Models;

public sealed class BluetoothPlaybackDeviceInfo
{
    public string Id { get; init; } = string.Empty;

    public string Name { get; init; } = string.Empty;

    public string Kind { get; init; } = string.Empty;

    public bool IsPaired { get; init; }

    public bool IsEnabled { get; init; } = true;

    public bool IsConnected { get; init; }

    public string ConnectionState { get; init; } = "可用";

    public string DisplayName => string.IsNullOrWhiteSpace(Name) ? "未命名播放设备" : Name;

    public string PairingLabel => IsPaired ? "已配对" : "未配对";

    public string EnabledLabel => IsEnabled ? "已启用" : "已禁用";

    public string ConnectionLabel => IsConnected ? $"已连接 / {ConnectionState}" : "未连接";
}
