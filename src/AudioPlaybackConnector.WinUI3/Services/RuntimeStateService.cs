using AudioPlaybackConnector.WinUI3.Models;
using System.Text.Json;

namespace AudioPlaybackConnector.WinUI3.Services;

public sealed class RuntimeStateService
{
    private static readonly JsonSerializerOptions SerializerOptions = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase
    };

    private readonly AppEnvironmentService _environmentService;

    public RuntimeStateService(AppEnvironmentService environmentService)
    {
        _environmentService = environmentService;
    }

    public async Task<BackendRuntimeState?> LoadAsync()
    {
        if (!File.Exists(_environmentService.RuntimeStatePath))
        {
            return null;
        }

        await using var stream = File.OpenRead(_environmentService.RuntimeStatePath);
        return await JsonSerializer.DeserializeAsync<BackendRuntimeState>(stream, SerializerOptions);
    }
}
