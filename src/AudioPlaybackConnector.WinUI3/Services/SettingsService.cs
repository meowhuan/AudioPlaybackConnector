using AudioPlaybackConnector.WinUI3.Models;
using System.Text.Json;

namespace AudioPlaybackConnector.WinUI3.Services;

public sealed class SettingsService
{
    private static readonly JsonSerializerOptions SerializerOptions = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        WriteIndented = true
    };

    private readonly AppEnvironmentService _environmentService;

    public SettingsService(AppEnvironmentService environmentService)
    {
        _environmentService = environmentService;
    }

    public string ConfigPath => _environmentService.ConfigPath;

    public async Task<AppSettings> LoadAsync()
    {
        if (!File.Exists(ConfigPath))
        {
            return AppSettings.CreateDefault();
        }

        await using var stream = File.OpenRead(ConfigPath);
        var settings = await JsonSerializer.DeserializeAsync<AppSettings>(stream, SerializerOptions);
        return settings ?? AppSettings.CreateDefault();
    }

    public async Task SaveAsync(AppSettings settings)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(ConfigPath)!);
        await using var stream = File.Create(ConfigPath);
        await JsonSerializer.SerializeAsync(stream, settings, SerializerOptions);
    }
}
