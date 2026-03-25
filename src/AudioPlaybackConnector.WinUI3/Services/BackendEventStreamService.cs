using AudioPlaybackConnector.WinUI3.Models;
using System.IO.Pipes;
using System.Text;
using System.Text.Json;

namespace AudioPlaybackConnector.WinUI3.Services;

public sealed class BackendEventStreamService : IAsyncDisposable
{
    private const string EventPipeName = "AudioPlaybackConnector.Events";

    private static readonly JsonSerializerOptions SerializerOptions = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase
    };

    private CancellationTokenSource? _cts;
    private Task? _listenTask;

    public event EventHandler<BackendRuntimeState>? RuntimeStateReceived;

    public void Start()
    {
        if (_listenTask is not null)
        {
            return;
        }

        _cts = new CancellationTokenSource();
        _listenTask = Task.Run(() => ListenLoopAsync(_cts.Token));
    }

    private async Task ListenLoopAsync(CancellationToken cancellationToken)
    {
        while (!cancellationToken.IsCancellationRequested)
        {
            try
            {
                using var pipe = new NamedPipeClientStream(".", EventPipeName, PipeDirection.In, PipeOptions.Asynchronous);
                await pipe.ConnectAsync(1500, cancellationToken);

                using var reader = new StreamReader(pipe, Encoding.UTF8, detectEncodingFromByteOrderMarks: false, leaveOpen: true);
                while (!cancellationToken.IsCancellationRequested && pipe.IsConnected)
                {
                    var line = await reader.ReadLineAsync().WaitAsync(cancellationToken);
                    if (string.IsNullOrWhiteSpace(line))
                    {
                        continue;
                    }

                    var state = JsonSerializer.Deserialize<BackendRuntimeState>(line, SerializerOptions);
                    if (state is not null)
                    {
                        RuntimeStateReceived?.Invoke(this, state);
                    }
                }
            }
            catch (OperationCanceledException)
            {
                break;
            }
            catch (IOException)
            {
            }
            catch (TimeoutException)
            {
            }

            if (!cancellationToken.IsCancellationRequested)
            {
                await Task.Delay(500, cancellationToken);
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
        if (_listenTask is not null)
        {
            await _listenTask;
        }

        _cts.Dispose();
        _cts = null;
        _listenTask = null;
    }
}
