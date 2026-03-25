using AudioPlaybackConnector.WinUI3.Services;
using AudioPlaybackConnector.WinUI3.ViewModels;
using Microsoft.UI.Xaml;

namespace AudioPlaybackConnector.WinUI3;

public partial class App : Application
{
    private MainWindow? _window;
    private GuiActivationService? _guiActivationService;
    private readonly string _crashLogPath = Path.Combine(AppContext.BaseDirectory, "winui3-crash.log");

    public App()
    {
        InitializeComponent();
        UnhandledException += OnUnhandledException;
    }

    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        var environmentService = new AppEnvironmentService();
        var autoStartService = new AutoStartService();
        _guiActivationService = new GuiActivationService();
        var settingsService = new SettingsService(environmentService);
        var runtimeStateService = new RuntimeStateService(environmentService);
        var backendEventStreamService = new BackendEventStreamService();
        var bluetoothDeviceService = new BluetoothPlaybackDeviceService();
        var outputDeviceService = new OutputDeviceService();
        var legacyHostService = new LegacyHostService(environmentService);

        var viewModel = new MainViewModel(
            autoStartService,
            settingsService,
            runtimeStateService,
            backendEventStreamService,
            bluetoothDeviceService,
            outputDeviceService,
            legacyHostService);

        _window = new MainWindow(viewModel);
        _window.Activate();
        _guiActivationService.ActivationRequested += OnGuiActivationRequested;
        _guiActivationService.Start();
        _window.BringToFront();
    }

    private void OnGuiActivationRequested(object? sender, string command)
    {
        if (string.Equals(command, "show", StringComparison.OrdinalIgnoreCase))
        {
            _window?.DispatcherQueue.TryEnqueue(() => _window?.BringToFront());
        }
    }

    private void OnUnhandledException(object sender, Microsoft.UI.Xaml.UnhandledExceptionEventArgs e)
    {
        File.AppendAllText(
            _crashLogPath,
            $"{DateTimeOffset.Now:O} AppUnhandledException Message={e.Message}{Environment.NewLine}{e.Exception}{Environment.NewLine}{Environment.NewLine}");
    }
}
