using Microsoft.UI.Xaml;

namespace AudioPlaybackConnector.WinUI3.Models;

public sealed class OutputDeviceInfo
{
    public string Id { get; init; } = string.Empty;

    public string Name { get; init; } = string.Empty;

    public bool IsDefault { get; init; }

    public bool IsUnavailable { get; init; }

    public string DisplayName =>
        string.IsNullOrWhiteSpace(Name)
            ? "默认输出设备"
            : Name;

    public Visibility DefaultBadgeVisibility => IsDefault && !IsUnavailable ? Visibility.Visible : Visibility.Collapsed;

    public Visibility UnavailableBadgeVisibility => IsUnavailable ? Visibility.Visible : Visibility.Collapsed;

    public static OutputDeviceInfo CreateSystemDefaultOption() =>
        new()
        {
            Id = string.Empty,
            Name = "跟随系统默认输出"
        };

    public static OutputDeviceInfo CreateUnavailable(string id) =>
        new()
        {
            Id = id,
            Name = $"不可用设备 ({id})",
            IsUnavailable = true
        };
}
