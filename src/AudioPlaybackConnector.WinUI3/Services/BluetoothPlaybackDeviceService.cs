using AudioPlaybackConnector.WinUI3.Models;
using Windows.Devices.Enumeration;
using Windows.Media.Audio;

namespace AudioPlaybackConnector.WinUI3.Services;

public sealed class BluetoothPlaybackDeviceService
{
    public async Task<IReadOnlyList<BluetoothPlaybackDeviceInfo>> GetDevicesAsync()
    {
        var selector = AudioPlaybackConnection.GetDeviceSelector();
        var devices = await DeviceInformation.FindAllAsync(selector);

        return devices
            .Select(device => new BluetoothPlaybackDeviceInfo
            {
                Id = device.Id,
                Name = device.Name,
                Kind = device.Kind.ToString(),
                IsPaired = device.Pairing?.IsPaired ?? false,
                IsEnabled = TryGetBool(device.Properties, "System.Devices.InterfaceEnabled") ?? true
            })
            .OrderBy(device => device.DisplayName, StringComparer.CurrentCultureIgnoreCase)
            .ToList();
    }

    private static bool? TryGetBool(IReadOnlyDictionary<string, object> properties, string key)
    {
        if (!properties.TryGetValue(key, out var value) || value is null)
        {
            return null;
        }

        return value switch
        {
            bool boolValue => boolValue,
            _ => null
        };
    }
}
