using AudioPlaybackConnector.WinUI3.Models;
using AudioPlaybackConnector.WinUI3.Services;
using System.Collections.ObjectModel;

namespace AudioPlaybackConnector.WinUI3.ViewModels;

public sealed class MainViewModel : ViewModelBase
{
    private readonly AutoStartService _autoStartService;
    private readonly SettingsService _settingsService;
    private readonly RuntimeStateService _runtimeStateService;
    private readonly BackendEventStreamService _backendEventStreamService;
    private readonly BluetoothPlaybackDeviceService _bluetoothDeviceService;
    private readonly OutputDeviceService _outputDeviceService;
    private readonly LegacyHostService _legacyHostService;
    private readonly SynchronizationContext _syncContext;

    private bool _initialized;
    private bool _isBusy;
    private bool _autoStart;
    private bool _reconnect;
    private bool _duckOtherApps = true;
    private bool _showStartupToast;
    private double _volume = 100.0;
    private double _duckedAppsVolume = 35.0;
    private string _selectedOutputDeviceId = string.Empty;
    private string _statusHeadline = "准备就绪";
    private string _statusDetail = "加载共享配置、比对设备状态，并在需要时启动原生后端。";
    private string _legacyHostPath = "未找到原生后端。";
    private string _legacyHostState = "未知";
    private BluetoothPlaybackDeviceInfo? _selectedBluetoothDevice;
    private BackendRuntimeState? _runtimeState;

    public MainViewModel(
        AutoStartService autoStartService,
        SettingsService settingsService,
        RuntimeStateService runtimeStateService,
        BackendEventStreamService backendEventStreamService,
        BluetoothPlaybackDeviceService bluetoothDeviceService,
        OutputDeviceService outputDeviceService,
        LegacyHostService legacyHostService)
    {
        _autoStartService = autoStartService;
        _settingsService = settingsService;
        _runtimeStateService = runtimeStateService;
        _backendEventStreamService = backendEventStreamService;
        _bluetoothDeviceService = bluetoothDeviceService;
        _outputDeviceService = outputDeviceService;
        _legacyHostService = legacyHostService;
        _syncContext = SynchronizationContext.Current ?? new SynchronizationContext();

        _backendEventStreamService.RuntimeStateReceived += OnRuntimeStateReceived;

        RefreshCommand = new AsyncRelayCommand(RefreshAsync, () => !IsBusy);
        ReloadConfigCommand = new AsyncRelayCommand(ReloadConfigAsync, () => !IsBusy);
        SaveSettingsCommand = new AsyncRelayCommand(SaveSettingsAsync, () => !IsBusy);
        LaunchLegacyHostCommand = new AsyncRelayCommand(LaunchLegacyHostAsync, () => !IsBusy);
        OpenDevicePickerCommand = new AsyncRelayCommand(OpenDevicePickerAsync, () => !IsBusy);
        StopLegacyHostCommand = new AsyncRelayCommand(StopLegacyHostAsync, () => !IsBusy);
        ConnectSelectedDeviceCommand = new AsyncRelayCommand(ConnectSelectedDeviceAsync, () => !IsBusy && SelectedBluetoothDevice is not null);
        DisconnectSelectedDeviceCommand = new AsyncRelayCommand(DisconnectSelectedDeviceAsync, () => !IsBusy && SelectedBluetoothDevice is not null);
        OpenBluetoothSettingsCommand = new AsyncRelayCommand(OpenBluetoothSettingsAsync, () => !IsBusy);
        OpenConfigFolderCommand = new AsyncRelayCommand(OpenConfigFolderAsync, () => !IsBusy);
    }

    public ObservableCollection<BluetoothPlaybackDeviceInfo> BluetoothDevices { get; } = [];

    public ObservableCollection<OutputDeviceInfo> OutputDevices { get; } = [];

    public ObservableCollection<string> LastConnectedDevices { get; } = [];

    public ObservableCollection<DebugLogEntry> LogEntries { get; } = [];

    public AsyncRelayCommand RefreshCommand { get; }

    public AsyncRelayCommand ReloadConfigCommand { get; }

    public AsyncRelayCommand SaveSettingsCommand { get; }

    public AsyncRelayCommand LaunchLegacyHostCommand { get; }

    public AsyncRelayCommand OpenDevicePickerCommand { get; }

    public AsyncRelayCommand StopLegacyHostCommand { get; }

    public AsyncRelayCommand ConnectSelectedDeviceCommand { get; }

    public AsyncRelayCommand DisconnectSelectedDeviceCommand { get; }

    public AsyncRelayCommand OpenBluetoothSettingsCommand { get; }

    public AsyncRelayCommand OpenConfigFolderCommand { get; }

    public string ConfigPath => _settingsService.ConfigPath;

    public string ConfigFileName => Path.GetFileName(ConfigPath);

    public bool IsBusy
    {
        get => _isBusy;
        private set
        {
            if (SetProperty(ref _isBusy, value))
            {
                RefreshCommand.RaiseCanExecuteChanged();
                ReloadConfigCommand.RaiseCanExecuteChanged();
                SaveSettingsCommand.RaiseCanExecuteChanged();
                LaunchLegacyHostCommand.RaiseCanExecuteChanged();
                OpenDevicePickerCommand.RaiseCanExecuteChanged();
                StopLegacyHostCommand.RaiseCanExecuteChanged();
                ConnectSelectedDeviceCommand.RaiseCanExecuteChanged();
                DisconnectSelectedDeviceCommand.RaiseCanExecuteChanged();
                OpenBluetoothSettingsCommand.RaiseCanExecuteChanged();
                OpenConfigFolderCommand.RaiseCanExecuteChanged();
            }
        }
    }

    public bool AutoStart
    {
        get => _autoStart;
        set => SetProperty(ref _autoStart, value);
    }

    public BluetoothPlaybackDeviceInfo? SelectedBluetoothDevice
    {
        get => _selectedBluetoothDevice;
        set
        {
            if (SetProperty(ref _selectedBluetoothDevice, value))
            {
                ConnectSelectedDeviceCommand.RaiseCanExecuteChanged();
                DisconnectSelectedDeviceCommand.RaiseCanExecuteChanged();
                OnPropertyChanged(nameof(SelectedBluetoothDeviceSummary));
            }
        }
    }

    public string SelectedBluetoothDeviceSummary => SelectedBluetoothDevice is null
        ? "请选择一个蓝牙播放设备，通过原生后端执行连接或断开。"
        : $"{SelectedBluetoothDevice.DisplayName}（{SelectedBluetoothDevice.ConnectionLabel}）";

    public bool Reconnect
    {
        get => _reconnect;
        set => SetProperty(ref _reconnect, value);
    }

    public bool DuckOtherApps
    {
        get => _duckOtherApps;
        set => SetProperty(ref _duckOtherApps, value);
    }

    public bool ShowStartupToast
    {
        get => _showStartupToast;
        set => SetProperty(ref _showStartupToast, value);
    }

    public double Volume
    {
        get => _volume;
        set
        {
            if (SetProperty(ref _volume, value))
            {
                OnPropertyChanged(nameof(VolumeText));
            }
        }
    }

    public string VolumeText => $"{Math.Round(Volume):0}%";

    public double DuckedAppsVolume
    {
        get => _duckedAppsVolume;
        set
        {
            if (SetProperty(ref _duckedAppsVolume, value))
            {
                OnPropertyChanged(nameof(DuckedAppsVolumeText));
            }
        }
    }

    public string DuckedAppsVolumeText => $"{Math.Round(DuckedAppsVolume):0}%";

    public string SelectedOutputDeviceId
    {
        get => _selectedOutputDeviceId;
        set
        {
            if (SetProperty(ref _selectedOutputDeviceId, value ?? string.Empty))
            {
                OnPropertyChanged(nameof(SelectedOutputDeviceSummary));
                OnPropertyChanged(nameof(PreferredOutputSummaryText));
            }
        }
    }

    public string SelectedOutputDeviceSummary
    {
        get
        {
            if (string.IsNullOrWhiteSpace(SelectedOutputDeviceId))
            {
                return "原生后端将跟随当前 Windows 默认输出设备。";
            }

            var selected = OutputDevices.FirstOrDefault(device => device.Id == SelectedOutputDeviceId);
            return selected is null
                ? $"已持久化设备 Id：{SelectedOutputDeviceId}"
                : $"已持久化设备：{selected.DisplayName}";
        }
    }

    public string BluetoothDeviceCountText => BluetoothDevices.Count.ToString();

    public string ConnectedBluetoothDeviceCountText => BluetoothDevices.Count(device => device.IsConnected).ToString();

    public string OutputDeviceCountText => Math.Max(OutputDevices.Count - 1, 0).ToString();

    public string PreferredOutputSummaryText
    {
        get
        {
            if (string.IsNullOrWhiteSpace(SelectedOutputDeviceId))
            {
                return "系统默认";
            }

            var selected = OutputDevices.FirstOrDefault(device => device.Id == SelectedOutputDeviceId);
            return selected?.DisplayName ?? "未解析";
        }
    }

    public string StatusHeadline
    {
        get => _statusHeadline;
        private set => SetProperty(ref _statusHeadline, value);
    }

    public string StatusDetail
    {
        get => _statusDetail;
        private set => SetProperty(ref _statusDetail, value);
    }

    public string LegacyHostPath
    {
        get => _legacyHostPath;
        private set => SetProperty(ref _legacyHostPath, value);
    }

    public string LegacyHostState
    {
        get => _legacyHostState;
        private set => SetProperty(ref _legacyHostState, value);
    }

    public async Task InitializeAsync()
    {
        if (_initialized)
        {
            return;
        }

        _initialized = true;
        _backendEventStreamService.Start();
        await ReloadConfigAsync();
    }

    private async Task ReloadConfigAsync()
    {
        await RunBusyAsync(async () =>
        {
            var settings = await _settingsService.LoadAsync();
            AutoStart = _autoStartService.IsEnabled();
            ApplySettings(settings);
            AppendLog("配置", $"已从 {ConfigPath} 载入共享配置。");
            await RefreshAsyncCore();
        });
    }

    private async Task RefreshAsync()
    {
        await RunBusyAsync(RefreshAsyncCore);
    }

    private async Task SaveSettingsAsync()
    {
        await RunBusyAsync(async () =>
        {
            _autoStartService.SetEnabled(AutoStart);
            var settings = BuildSettings();
            await _settingsService.SaveAsync(settings);
            await _legacyHostService.ReloadSettingsAsync();
            AppendLog("配置", "已保存共享配置，并通知原生后端重新加载。");
            StatusHeadline = "设置已保存";
            StatusDetail = "WinUI 3 主界面已经更新共享配置，并要求原生后端立即重新加载。";
        });
    }

    private async Task LaunchLegacyHostAsync()
    {
        await RunBusyAsync(() =>
        {
            _legacyHostService.Launch();
            AppendLog("后端", "已启动原生托盘后端。");
            RefreshLegacyHostStatus();
            StatusHeadline = "原生后端已启动";
            StatusDetail = "当前连接、重连、路由与托盘交互均由原生后端负责。";
            return Task.CompletedTask;
        });
    }

    private async Task OpenDevicePickerAsync()
    {
        await RunBusyAsync(async () =>
        {
            await _legacyHostService.ShowDevicePickerAsync();
            AppendLog("后端", "已请求原生后端打开设备选择器。");
            RefreshLegacyHostStatus();
            StatusHeadline = "设备选择器已打开";
            StatusDetail = "WinUI 3 只负责控制面，真实连接动作仍然通过原生 A2DP 主线执行。";
        });
    }

    private async Task StopLegacyHostAsync()
    {
        await RunBusyAsync(async () =>
        {
            await _legacyHostService.RequestExitAsync();
            AppendLog("后端", "已请求原生后端退出。");
            RefreshLegacyHostStatus();
            StatusHeadline = "已请求后端退出";
            StatusDetail = "前端已要求原生托盘后端执行干净退出。";
        });
    }

    private async Task OpenConfigFolderAsync()
    {
        await RunBusyAsync(() =>
        {
            _legacyHostService.OpenConfigFolder();
            AppendLog("后端", "已在资源管理器中打开共享配置目录。");
            return Task.CompletedTask;
        });
    }

    private async Task OpenBluetoothSettingsAsync()
    {
        await RunBusyAsync(() =>
        {
            _legacyHostService.OpenBluetoothSettings();
            AppendLog("后端", "已打开 Windows 蓝牙设置。");
            return Task.CompletedTask;
        });
    }

    private async Task ConnectSelectedDeviceAsync()
    {
        if (SelectedBluetoothDevice is null)
        {
            return;
        }

        await RunBusyAsync(async () =>
        {
            await _legacyHostService.ConnectDeviceAsync(SelectedBluetoothDevice.Id);
            AppendLog("后端", $"已请求连接 {SelectedBluetoothDevice.DisplayName}。");
            StatusHeadline = "已发起连接";
            StatusDetail = $"原生后端正在沿现有 A2DP 主线打开 {SelectedBluetoothDevice.DisplayName}。";
            await Task.Delay(800);
            await RefreshAsyncCore();
        });
    }

    private async Task DisconnectSelectedDeviceAsync()
    {
        if (SelectedBluetoothDevice is null)
        {
            return;
        }

        await RunBusyAsync(async () =>
        {
            await _legacyHostService.DisconnectDeviceAsync(SelectedBluetoothDevice.Id);
            AppendLog("后端", $"已请求断开 {SelectedBluetoothDevice.DisplayName}。");
            StatusHeadline = "已发起断开";
            StatusDetail = $"原生后端正在关闭 {SelectedBluetoothDevice.DisplayName}。";
            await Task.Delay(500);
            await RefreshAsyncCore();
        });
    }

    private async Task RefreshAsyncCore()
    {
        RefreshLegacyHostStatus();
        _runtimeState = await _runtimeStateService.LoadAsync();

        var bluetoothDevices = await _bluetoothDeviceService.GetDevicesAsync();
        var projectedBluetoothDevices = ProjectBluetoothDevices(bluetoothDevices, _runtimeState);
        ReplaceCollection(BluetoothDevices, projectedBluetoothDevices);
        if (SelectedBluetoothDevice is not null)
        {
            SelectedBluetoothDevice = BluetoothDevices.FirstOrDefault(device => device.Id == SelectedBluetoothDevice.Id);
        }
        AppendLog("设备", $"发现 {bluetoothDevices.Count} 个蓝牙播放设备。");

        var outputDevices = await _outputDeviceService.GetRenderDevicesAsync();
        ReplaceOutputDevices(outputDevices);
        AppendLog("设备", $"发现 {outputDevices.Count} 个活动渲染端点。");

        ApplyRuntimeState(_runtimeState, updateStatusText: true, appendLog: false);
    }

    private void OnRuntimeStateReceived(object? sender, BackendRuntimeState runtimeState)
    {
        _syncContext.Post(_ =>
        {
            _runtimeState = runtimeState;
            ApplyRuntimeState(runtimeState, updateStatusText: true, appendLog: true);
        }, null);
    }

    private void ApplyRuntimeState(BackendRuntimeState? runtimeState, bool updateStatusText, bool appendLog)
    {
        if (runtimeState is null)
        {
            if (updateStatusText)
            {
                StatusHeadline = "调试快照已刷新";
                StatusDetail = $"蓝牙播放设备：{BluetoothDevices.Count} 个，后端连接：0 个，输出端点：{Math.Max(OutputDevices.Count - 1, 0)} 个，当前路由：{PreferredOutputSummaryText}。";
            }
            return;
        }

        Reconnect = runtimeState.Reconnect;
        Volume = runtimeState.Volume * 100.0;
        DuckOtherApps = runtimeState.DuckOtherApps;
        DuckedAppsVolume = runtimeState.DuckedAppsVolume * 100.0;
        ShowStartupToast = runtimeState.ShowStartupToast;
        SelectedOutputDeviceId = runtimeState.OutputDeviceId ?? string.Empty;

        if (BluetoothDevices.Count > 0)
        {
            ReplaceCollection(BluetoothDevices, ProjectBluetoothDevices(BluetoothDevices, runtimeState));
            if (SelectedBluetoothDevice is not null)
            {
                SelectedBluetoothDevice = BluetoothDevices.FirstOrDefault(device => device.Id == SelectedBluetoothDevice.Id);
            }
        }

        if (appendLog)
        {
            AppendLog("事件", $"后端状态已变化：{runtimeState.Reason}。");
        }

        if (updateStatusText)
        {
            StatusHeadline = runtimeState.Running ? "后端已连接" : "后端已停止";
            StatusDetail = $"蓝牙播放设备：{BluetoothDevices.Count} 个，后端连接：{runtimeState.Connections.Count} 个，输出端点：{Math.Max(OutputDevices.Count - 1, 0)} 个，当前路由：{PreferredOutputSummaryText}。";
        }
    }

    private void RefreshLegacyHostStatus()
    {
        var status = _legacyHostService.GetStatus();
        LegacyHostPath = status.ExecutablePath ?? "在当前仓库输出目录中未找到原生后端可执行文件。";
        LegacyHostState = status.StateLabel;
        AppendLog("后端", $"原生后端状态：{LegacyHostState}。");
    }

    private static List<BluetoothPlaybackDeviceInfo> ProjectBluetoothDevices(IEnumerable<BluetoothPlaybackDeviceInfo> devices, BackendRuntimeState? runtimeState)
    {
        var connectedById = runtimeState?.Connections.ToDictionary(connection => connection.Id, StringComparer.Ordinal)
            ?? new Dictionary<string, BackendConnectionInfo>(StringComparer.Ordinal);

        return devices.Select(device =>
        {
            connectedById.TryGetValue(device.Id, out var connection);
            return new BluetoothPlaybackDeviceInfo
            {
                Id = device.Id,
                Name = device.Name,
                Kind = device.Kind,
                IsPaired = device.IsPaired,
                IsEnabled = device.IsEnabled,
                IsConnected = connection is not null,
                ConnectionState = connection?.State ?? "可用"
            };
        }).ToList();
    }

    private void ApplySettings(AppSettings settings)
    {
        Reconnect = settings.Reconnect;
        Volume = settings.Volume * 100.0;
        DuckOtherApps = settings.DuckOtherApps;
        DuckedAppsVolume = settings.DuckedAppsVolume * 100.0;
        ShowStartupToast = settings.ShowStartupToast;
        SelectedOutputDeviceId = settings.OutputDeviceId ?? string.Empty;
        ReplaceCollection(LastConnectedDevices, settings.LastDevices);
        OnPropertyChanged(nameof(SelectedOutputDeviceSummary));
    }

    private AppSettings BuildSettings()
    {
        return new AppSettings
        {
            Reconnect = Reconnect,
            Volume = Math.Clamp(Volume / 100.0, 0.0, 1.0),
            DuckOtherApps = DuckOtherApps,
            DuckedAppsVolume = Math.Clamp(DuckedAppsVolume / 100.0, 0.0, 1.0),
            ShowStartupToast = ShowStartupToast,
            OutputDeviceId = SelectedOutputDeviceId ?? string.Empty,
            LastDevices = [.. LastConnectedDevices]
        };
    }

    private void ReplaceOutputDevices(IReadOnlyList<OutputDeviceInfo> devices)
    {
        var items = new List<OutputDeviceInfo> { OutputDeviceInfo.CreateSystemDefaultOption() };
        items.AddRange(devices);

        if (!string.IsNullOrWhiteSpace(SelectedOutputDeviceId) &&
            items.All(device => !string.Equals(device.Id, SelectedOutputDeviceId, StringComparison.Ordinal)))
        {
            items.Add(OutputDeviceInfo.CreateUnavailable(SelectedOutputDeviceId));
        }

        ReplaceCollection(OutputDevices, items);
        OnPropertyChanged(nameof(SelectedOutputDeviceSummary));
    }

    private void ReplaceCollection<T>(ObservableCollection<T> collection, IEnumerable<T> values)
    {
        collection.Clear();
        foreach (var value in values)
        {
            collection.Add(value);
        }

        OnPropertyChanged(nameof(BluetoothDeviceCountText));
        OnPropertyChanged(nameof(ConnectedBluetoothDeviceCountText));
        OnPropertyChanged(nameof(OutputDeviceCountText));
        OnPropertyChanged(nameof(PreferredOutputSummaryText));
    }

    private void AppendLog(string category, string message)
    {
        LogEntries.Insert(0, new DebugLogEntry
        {
            Category = category,
            Message = message,
            Timestamp = DateTimeOffset.Now
        });

        while (LogEntries.Count > 80)
        {
            LogEntries.RemoveAt(LogEntries.Count - 1);
        }
    }

    private async Task RunBusyAsync(Func<Task> action)
    {
        if (IsBusy)
        {
            return;
        }

        try
        {
            IsBusy = true;
            await action();
        }
        catch (Exception ex)
        {
            AppendLog("错误", ex.Message);
            StatusHeadline = "操作失败";
            StatusDetail = ex.Message;
        }
        finally
        {
            IsBusy = false;
        }
    }
}
