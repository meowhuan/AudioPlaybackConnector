using Microsoft.Win32;

namespace AudioPlaybackConnector.WinUI3.Services;

public sealed class AutoStartService
{
    private const string AutoRunKeyPath = @"Software\Microsoft\Windows\CurrentVersion\Run";
    private const string AutoRunValueName = "AudioPlaybackConnector";

    public bool IsEnabled()
    {
        using var key = Registry.CurrentUser.OpenSubKey(AutoRunKeyPath, writable: false);
        var value = key?.GetValue(AutoRunValueName) as string;
        return string.Equals(value, GetCurrentCommandLine(), StringComparison.OrdinalIgnoreCase);
    }

    public void SetEnabled(bool enabled)
    {
        using var key = Registry.CurrentUser.CreateSubKey(AutoRunKeyPath, writable: true)
            ?? throw new InvalidOperationException("Could not open the Run registry key.");

        if (enabled)
        {
            key.SetValue(AutoRunValueName, GetCurrentCommandLine(), RegistryValueKind.String);
        }
        else
        {
            key.DeleteValue(AutoRunValueName, throwOnMissingValue: false);
        }
    }

    private static string GetCurrentCommandLine()
    {
        var processPath = Environment.ProcessPath
            ?? throw new InvalidOperationException("Could not resolve the current executable path.");
        return $"\"{processPath}\"";
    }
}
