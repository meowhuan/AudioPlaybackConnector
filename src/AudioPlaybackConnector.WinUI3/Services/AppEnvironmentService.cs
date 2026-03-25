namespace AudioPlaybackConnector.WinUI3.Services;

public sealed class AppEnvironmentService
{
    private const string ConfigFileName = "AudioPlaybackConnector.json";
    private const string RuntimeStateFileName = "AudioPlaybackConnector.runtime.json";

    private static readonly string[] LegacyHostCandidates =
    [
        Path.Combine("NativeHost", "AudioPlaybackConnector64.exe"),
        Path.Combine("NativeHost", "AudioPlaybackConnector.exe"),
        Path.Combine("x64", "Debug", "AudioPlaybackConnector64.exe"),
        Path.Combine("x64", "Release", "AudioPlaybackConnector64.exe"),
        Path.Combine("Debug", "AudioPlaybackConnector64.exe"),
        Path.Combine("Release", "AudioPlaybackConnector64.exe"),
        "AudioPlaybackConnector64.exe",
        "AudioPlaybackConnector.exe"
    ];

    public AppEnvironmentService()
    {
        RepositoryRoot = ResolveRepositoryRoot();
        ConfigPath = Path.Combine(ResolveLegacyHostDirectory(), ConfigFileName);
        RuntimeStatePath = Path.Combine(ResolveLegacyHostDirectory(), RuntimeStateFileName);
    }

    public string RepositoryRoot { get; }

    public string ConfigPath { get; }

    public string RuntimeStatePath { get; }

    public string? ResolveLegacyHostPath()
    {
        var localAppCandidates = new[]
        {
            Path.Combine(AppContext.BaseDirectory, "NativeHost", "AudioPlaybackConnector64.exe"),
            Path.Combine(AppContext.BaseDirectory, "NativeHost", "AudioPlaybackConnector.exe"),
            Path.Combine(AppContext.BaseDirectory, "AudioPlaybackConnector64.exe"),
            Path.Combine(AppContext.BaseDirectory, "AudioPlaybackConnector.exe")
        };

        foreach (var candidate in localAppCandidates)
        {
            if (File.Exists(candidate))
            {
                return candidate;
            }
        }

        foreach (var relativePath in LegacyHostCandidates)
        {
            var candidate = Path.Combine(RepositoryRoot, relativePath);
            if (File.Exists(candidate))
            {
                return candidate;
            }
        }

        return null;
    }

    private string ResolveLegacyHostDirectory()
    {
        var hostPath = ResolveLegacyHostPath();
        return !string.IsNullOrWhiteSpace(hostPath)
            ? Path.GetDirectoryName(hostPath) ?? RepositoryRoot
            : RepositoryRoot;
    }

    private static string ResolveRepositoryRoot()
    {
        var current = new DirectoryInfo(AppContext.BaseDirectory);
        while (current is not null)
        {
            if (File.Exists(Path.Combine(current.FullName, "AudioPlaybackConnector.sln")) ||
                Directory.Exists(Path.Combine(current.FullName, ".git")))
            {
                return current.FullName;
            }

            current = current.Parent;
        }

        return AppContext.BaseDirectory;
    }
}
