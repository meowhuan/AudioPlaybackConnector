using AudioPlaybackConnector.WinUI3.ViewModels;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using System.Runtime.InteropServices;
using Windows.Graphics;
using WinRT.Interop;

namespace AudioPlaybackConnector.WinUI3;

public sealed partial class MainWindow : Window
{
    private enum AppSection
    {
        Overview,
        Devices,
        Audio,
        Logs
    }

    private readonly string _crashLogPath = Path.Combine(AppContext.BaseDirectory, "winui3-crash.log");
    private bool _windowChromeInitialized;
    private AppSection _activeSection = AppSection.Overview;

    public MainViewModel ViewModel { get; }

    public MainWindow(MainViewModel viewModel)
    {
        ViewModel = viewModel;
        try
        {
            InitializeComponent();
        }
        catch (Exception ex)
        {
            File.AppendAllText(_crashLogPath, $"{DateTimeOffset.Now:O} MainWindowInitializeComponent {ex}{Environment.NewLine}{Environment.NewLine}");
            throw;
        }

        Title = "AudioPlaybackConnector";
        AppWindow.Resize(new SizeInt32(1480, 960));
        TryApplyWindowBackdrop();
        Activated += OnWindowActivated;
        ShowSection(AppSection.Overview);
        _ = ViewModel.InitializeAsync();
    }

    public void BringToFront()
    {
        Activate();

        var hwnd = WindowNative.GetWindowHandle(this);
        ShowWindow(hwnd, SW_RESTORE);
        SetForegroundWindow(hwnd);
    }

    private void OnWindowActivated(object sender, WindowActivatedEventArgs args)
    {
        if (_windowChromeInitialized)
        {
            return;
        }

        _windowChromeInitialized = true;
        TryConfigureWindowChrome();
    }

    private void TryApplyWindowBackdrop()
    {
        try
        {
            SystemBackdrop = new MicaBackdrop();
        }
        catch (Exception ex)
        {
            File.AppendAllText(_crashLogPath, $"{DateTimeOffset.Now:O} MicaBackdrop {ex}{Environment.NewLine}{Environment.NewLine}");
        }
    }

    private void TryConfigureWindowChrome()
    {
        try
        {
            ExtendsContentIntoTitleBar = true;
            SetTitleBar(WindowTitleBar);

            var hwnd = WindowNative.GetWindowHandle(this);
            var windowId = Microsoft.UI.Win32Interop.GetWindowIdFromWindow(hwnd);
            var appWindow = AppWindow.GetFromWindowId(windowId);
            if (appWindow is not null && AppWindowTitleBar.IsCustomizationSupported())
            {
                var titleBar = appWindow.TitleBar;
                titleBar.ButtonBackgroundColor = Microsoft.UI.Colors.Transparent;
                titleBar.ButtonInactiveBackgroundColor = Microsoft.UI.Colors.Transparent;
                titleBar.ButtonHoverBackgroundColor = Microsoft.UI.ColorHelper.FromArgb(36, 0, 103, 192);
                titleBar.ButtonPressedBackgroundColor = Microsoft.UI.ColorHelper.FromArgb(64, 0, 103, 192);
                titleBar.PreferredHeightOption = TitleBarHeightOption.Tall;
                UpdateTitleBarInsets(titleBar);
            }
        }
        catch (Exception ex)
        {
            File.AppendAllText(_crashLogPath, $"{DateTimeOffset.Now:O} ConfigureWindowChrome {ex}{Environment.NewLine}{Environment.NewLine}");
        }
    }

    private void UpdateTitleBarInsets(AppWindowTitleBar titleBar)
    {
        try
        {
            var scale = Content.XamlRoot?.RasterizationScale ?? 1.0;
            TitleBarLeftInsetColumn.Width = new GridLength(titleBar.LeftInset / scale);
            TitleBarRightInsetColumn.Width = new GridLength(titleBar.RightInset / scale);
        }
        catch
        {
        }
    }

    private void OnShowOverviewSection(object sender, RoutedEventArgs e) => ShowSection(AppSection.Overview);

    private void OnShowDevicesSection(object sender, RoutedEventArgs e) => ShowSection(AppSection.Devices);

    private void OnShowAudioSection(object sender, RoutedEventArgs e) => ShowSection(AppSection.Audio);

    private void OnShowLogsSection(object sender, RoutedEventArgs e) => ShowSection(AppSection.Logs);

    private void OnLanguageChanged(object sender, SelectionChangedEventArgs e)
    {
        if (LanguageBox.SelectedIndex <= 0)
        {
            TitleBarLanguageChipText.Text = "简体中文";
        }
        else
        {
            TitleBarLanguageChipText.Text = "English";
        }
    }

    private void ShowSection(AppSection section)
    {
        _activeSection = section;

        OverviewSection.Visibility = section == AppSection.Overview ? Visibility.Visible : Visibility.Collapsed;
        DevicesSection.Visibility = section == AppSection.Devices ? Visibility.Visible : Visibility.Collapsed;
        AudioSection.Visibility = section == AppSection.Audio ? Visibility.Visible : Visibility.Collapsed;
        LogsSection.Visibility = section == AppSection.Logs ? Visibility.Visible : Visibility.Collapsed;

        switch (section)
        {
        case AppSection.Overview:
            CurrentSectionTitleText.Text = "概览";
            CurrentSectionSubtitleText.Text = "查看实时状态、后端连接和当前蓝牙接收主线。";
            break;
        case AppSection.Devices:
            CurrentSectionTitleText.Text = "设备";
            CurrentSectionSubtitleText.Text = "浏览蓝牙播放设备与渲染输出端点，并通过后端执行连接动作。";
            break;
        case AppSection.Audio:
            CurrentSectionTitleText.Text = "音频";
            CurrentSectionSubtitleText.Text = "管理播放音量、压低策略和输出路由偏好。";
            break;
        case AppSection.Logs:
            CurrentSectionTitleText.Text = "日志";
            CurrentSectionSubtitleText.Text = "追踪前端操作、后端事件和实时状态变化。";
            break;
        }

        UpdateNavigationVisuals();
    }

    private void UpdateNavigationVisuals()
    {
        UpdateNavButton(OverviewSectionButton, _activeSection == AppSection.Overview);
        UpdateNavButton(DevicesSectionButton, _activeSection == AppSection.Devices);
        UpdateNavButton(AudioSectionButton, _activeSection == AppSection.Audio);
        UpdateNavButton(LogsSectionButton, _activeSection == AppSection.Logs);
    }

    private void UpdateNavButton(Button button, bool active)
    {
        button.Background = active
            ? (Brush)Application.Current.Resources["AppNavActiveBrush"]
            : (Brush)Application.Current.Resources["AppNavInactiveBrush"];
    }

    private const int SW_RESTORE = 9;

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool SetForegroundWindow(IntPtr hWnd);
}
