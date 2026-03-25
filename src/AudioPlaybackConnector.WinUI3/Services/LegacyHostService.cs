using AudioPlaybackConnector.WinUI3.Models;
using System.Diagnostics;
using System.IO.Pipes;
using System.Runtime.InteropServices;
using System.Text;

namespace AudioPlaybackConnector.WinUI3.Services;

public sealed class LegacyHostService
{
    private const string CommandPipeName = "AudioPlaybackConnector.Command";
    private const string WindowClassName = "AudioPlaybackConnector";
    private const uint WM_APP = 0x8000;
    private const uint WM_CLOSE = 0x0010;
    private const uint WM_SHOW_DEVICEPICKER_FROM_OTHER_INSTANCE = WM_APP + 3;
    private const uint WM_COPYDATA = 0x004A;
    private const nuint BackendCommandCopyDataId = 0x41504343;

    private readonly AppEnvironmentService _environmentService;

    public LegacyHostService(AppEnvironmentService environmentService)
    {
        _environmentService = environmentService;
    }

    public LegacyHostStatus GetStatus()
    {
        var executablePath = _environmentService.ResolveLegacyHostPath();
        var running = IsLegacyHostRunning();
        var windowHandle = FindHostWindow();

        return new LegacyHostStatus
        {
            IsRunning = running,
            HasWindow = windowHandle != IntPtr.Zero,
            ExecutablePath = executablePath,
            ConfigPath = _environmentService.ConfigPath
        };
    }

    public void Launch()
    {
        var executablePath = _environmentService.ResolveLegacyHostPath();
        if (string.IsNullOrWhiteSpace(executablePath))
        {
            throw new FileNotFoundException("Could not locate the native tray host executable.");
        }

        Process.Start(new ProcessStartInfo
        {
            FileName = executablePath,
            WorkingDirectory = Path.GetDirectoryName(executablePath),
            UseShellExecute = true
        });
    }

    public async Task ShowDevicePickerAsync()
    {
        await InvokeCommandAsync("picker");
    }

    public async Task RequestExitAsync()
    {
        await InvokeCommandAsync("exit");

        var deadline = DateTimeOffset.UtcNow.AddSeconds(5);
        while (DateTimeOffset.UtcNow < deadline)
        {
            await Task.Delay(150);
            if (FindHostWindow() == IntPtr.Zero)
            {
                return;
            }
        }
    }

    public Task ConnectDeviceAsync(string deviceId)
    {
        return InvokeCommandAsync("connect", deviceId);
    }

    public Task DisconnectDeviceAsync(string deviceId)
    {
        return InvokeCommandAsync("disconnect", deviceId);
    }

    public Task ReloadSettingsAsync()
    {
        return InvokeCommandAsync("reload-settings", allowStartWhenNotRunning: false);
    }

    public void OpenConfigFolder()
    {
        var configPath = _environmentService.ConfigPath;
        if (File.Exists(configPath))
        {
            Process.Start(new ProcessStartInfo
            {
                FileName = "explorer.exe",
                Arguments = $"/select,\"{configPath}\"",
                UseShellExecute = true
            });
            return;
        }

        Process.Start(new ProcessStartInfo
        {
            FileName = "explorer.exe",
            Arguments = $"\"{Path.GetDirectoryName(configPath)}\"",
            UseShellExecute = true
        });
    }

    public void OpenBluetoothSettings()
    {
        Process.Start(new ProcessStartInfo
        {
            FileName = "ms-settings:bluetooth",
            UseShellExecute = true
        });
    }

    private static bool IsLegacyHostRunning()
    {
        var processNames = new HashSet<string>(StringComparer.OrdinalIgnoreCase)
        {
            "AudioPlaybackConnector64",
            "AudioPlaybackConnector"
        };

        return Process.GetProcesses().Any(process => processNames.Contains(process.ProcessName));
    }

    private static IntPtr FindHostWindow()
    {
        return FindWindow(WindowClassName, null);
    }

    private async Task InvokeCommandAsync(string verb, string? payload = null, bool allowStartWhenNotRunning = true)
    {
        if (await TrySendPipeCommandAsync(verb, payload))
        {
            return;
        }

        var windowHandle = FindHostWindow();
        if (windowHandle != IntPtr.Zero)
        {
            SetForegroundWindow(windowHandle);
            if (string.Equals(verb, "picker", StringComparison.OrdinalIgnoreCase))
            {
                if (!PostMessage(windowHandle, WM_SHOW_DEVICEPICKER_FROM_OTHER_INSTANCE, IntPtr.Zero, IntPtr.Zero))
                {
                    throw new InvalidOperationException("Failed to ask the native backend to open the device picker.");
                }
                return;
            }

            if (string.Equals(verb, "exit", StringComparison.OrdinalIgnoreCase))
            {
                if (!PostMessage(windowHandle, WM_CLOSE, IntPtr.Zero, IntPtr.Zero))
                {
                    throw new InvalidOperationException("Failed to send the exit request to the native backend.");
                }
                return;
            }

            if (!SendBackendCommand(windowHandle, SerializeCommand(verb, payload)))
            {
                throw new InvalidOperationException($"Failed to send '{verb}' to the native backend.");
            }
            return;
        }

        if (!allowStartWhenNotRunning && !string.Equals(verb, "picker", StringComparison.OrdinalIgnoreCase))
        {
            return;
        }

        var executablePath = _environmentService.ResolveLegacyHostPath();
        if (string.IsNullOrWhiteSpace(executablePath))
        {
            throw new FileNotFoundException("Could not locate the native tray host executable.");
        }

        var arguments = string.IsNullOrWhiteSpace(payload)
            ? verb
            : $"{verb} \"{payload.Replace("\"", "\\\"")}\"";

        Process.Start(new ProcessStartInfo
        {
            FileName = executablePath,
            Arguments = arguments,
            WorkingDirectory = Path.GetDirectoryName(executablePath),
            UseShellExecute = true
        });

        if (allowStartWhenNotRunning)
        {
            await WaitForHostWindowAsync(TimeSpan.FromSeconds(5));
            await TrySendPipeCommandAsync(verb, payload);
        }
    }

    private static async Task<bool> TrySendPipeCommandAsync(string verb, string? payload)
    {
        try
        {
            using var pipe = new NamedPipeClientStream(".", CommandPipeName, PipeDirection.InOut, PipeOptions.Asynchronous);
            await pipe.ConnectAsync(750);

            var payloadText = SerializeCommand(verb, payload);
            var payloadBytes = Encoding.UTF8.GetBytes(payloadText);
            await pipe.WriteAsync(payloadBytes);
            await pipe.FlushAsync();

            using var reader = new StreamReader(pipe, new UTF8Encoding(false), detectEncodingFromByteOrderMarks: false, leaveOpen: true);
            _ = await reader.ReadLineAsync();
            return true;
        }
        catch (IOException)
        {
            return false;
        }
        catch (TimeoutException)
        {
            return false;
        }
    }

    private static string SerializeCommand(string verb, string? payload = null)
    {
        return string.IsNullOrWhiteSpace(payload)
            ? verb
            : $"{verb}\n{payload}";
    }

    private static bool SendBackendCommand(IntPtr windowHandle, string command)
    {
        var characters = command.ToCharArray();
        var byteCount = (characters.Length + 1) * sizeof(char);
        var payload = Marshal.AllocHGlobal(byteCount);

        try
        {
            Marshal.Copy(characters, 0, payload, characters.Length);
            Marshal.WriteInt16(payload, characters.Length * sizeof(char), 0);

            var data = new CopyDataStruct
            {
                dwData = BackendCommandCopyDataId,
                cbData = byteCount,
                lpData = payload
            };

            return SendMessage(windowHandle, WM_COPYDATA, IntPtr.Zero, ref data) != IntPtr.Zero;
        }
        finally
        {
            Marshal.FreeHGlobal(payload);
        }
    }

    private static async Task<IntPtr> WaitForHostWindowAsync(TimeSpan timeout)
    {
        var deadline = DateTimeOffset.UtcNow + timeout;
        while (DateTimeOffset.UtcNow < deadline)
        {
            var windowHandle = FindHostWindow();
            if (windowHandle != IntPtr.Zero)
            {
                return windowHandle;
            }

            await Task.Delay(150);
        }

        return IntPtr.Zero;
    }

    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern IntPtr FindWindow(string? className, string? windowName);

    [DllImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool PostMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool SetForegroundWindow(IntPtr hWnd);

    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern IntPtr SendMessage(IntPtr hWnd, uint msg, IntPtr wParam, ref CopyDataStruct lParam);

    [StructLayout(LayoutKind.Sequential)]
    private struct CopyDataStruct
    {
        public nuint dwData;
        public int cbData;
        public IntPtr lpData;
    }
}
