using System.IO.Pipes;
using System.Text;

namespace AudioPlaybackConnector.WinUI3.Services;

public sealed class GuiActivationService : IAsyncDisposable
{
    public const string ActivationPipeName = "AudioPlaybackConnector.WinUI3.Activation";
    public const string InstanceMutexName = @"Local\AudioPlaybackConnector.WinUI3.Main";

    private CancellationTokenSource? _cts;
    private Task? _listenTask;

    public event EventHandler<string>? ActivationRequested;

    public void Start()
    {
        if (_listenTask is not null)
        {
            return;
        }

        _cts = new CancellationTokenSource();
        _listenTask = Task.Run(() => ListenLoopAsync(_cts.Token));
    }

    public static async Task<bool> TrySignalExistingInstanceAsync(string command)
    {
        try
        {
            using var pipe = new NamedPipeClientStream(".", ActivationPipeName, PipeDirection.Out, PipeOptions.Asynchronous);
            await pipe.ConnectAsync(1000);
            var payload = Encoding.UTF8.GetBytes(command);
            await pipe.WriteAsync(payload);
            await pipe.FlushAsync();
            return true;
        }
        catch
        {
            return false;
        }
    }

    private async Task ListenLoopAsync(CancellationToken cancellationToken)
    {
        while (!cancellationToken.IsCancellationRequested)
        {
            try
            {
                using var pipe = new NamedPipeServerStream(ActivationPipeName, PipeDirection.In, 1, PipeTransmissionMode.Message, PipeOptions.Asynchronous);
                await pipe.WaitForConnectionAsync(cancellationToken);

                using var reader = new StreamReader(pipe, new UTF8Encoding(false), detectEncodingFromByteOrderMarks: false, leaveOpen: true);
                var text = await reader.ReadToEndAsync(cancellationToken);
                if (!string.IsNullOrWhiteSpace(text))
                {
                    ActivationRequested?.Invoke(this, text.Trim());
                }
            }
            catch (OperationCanceledException)
            {
                break;
            }
            catch (IOException)
            {
            }
        }
    }

    public async ValueTask DisposeAsync()
    {
        if (_cts is null)
        {
            return;
        }

        _cts.Cancel();
        await TrySignalExistingInstanceAsync("shutdown-listener");
        if (_listenTask is not null)
        {
            await _listenTask;
        }

        _cts.Dispose();
        _cts = null;
        _listenTask = null;
    }
}
