using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using System.Threading;
using System.Globalization;
using WinRT;
using AudioPlaybackConnector.WinUI3.Services;
using System.Runtime.InteropServices;

namespace AudioPlaybackConnector.WinUI3;

public static class Program
{
    public static Mutex? InstanceMutex { get; private set; }
    private static readonly string StartupLogPath = Path.Combine(AppContext.BaseDirectory, "winui3-startup.log");

    [STAThread]
    public static void Main(string[] args)
    {
        XamlCheckProcessRequirements();
        ComWrappersSupport.InitializeComWrappers();
        CultureInfo.DefaultThreadCurrentCulture = new CultureInfo("zh-CN");
        CultureInfo.DefaultThreadCurrentUICulture = new CultureInfo("zh-CN");

        var createdNew = false;
        InstanceMutex = new Mutex(true, GuiActivationService.InstanceMutexName, out createdNew);
        File.AppendAllText(StartupLogPath, $"{DateTimeOffset.Now:O} Main createdNew={createdNew} args={string.Join(' ', args)}{Environment.NewLine}");
        if (!createdNew)
        {
            var signaled = GuiActivationService.TrySignalExistingInstanceAsync("show").GetAwaiter().GetResult();
            File.AppendAllText(StartupLogPath, $"{DateTimeOffset.Now:O} Main redirected signaled={signaled}{Environment.NewLine}");
            return;
        }

        Application.Start(_ =>
        {
            var dispatcherQueue = DispatcherQueue.GetForCurrentThread();
            SynchronizationContext.SetSynchronizationContext(new DispatcherQueueSynchronizationContext(dispatcherQueue));
            var app = new App();
            File.AppendAllText(StartupLogPath, $"{DateTimeOffset.Now:O} Main started application loop{Environment.NewLine}");
        });
    }

    [DllImport("Microsoft.ui.xaml.dll")]
    private static extern void XamlCheckProcessRequirements();
}
