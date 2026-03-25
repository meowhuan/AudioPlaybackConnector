param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",

    [ValidateSet("x86", "x64", "ARM", "ARM64")]
    [string]$Platform = "x64",

    [switch]$BuildWinUI3,

    [switch]$SkipNative
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repoRoot = Split-Path -Parent $PSScriptRoot
$toolsDir = Join-Path $repoRoot ".tools"
$translateDir = Join-Path $repoRoot "translate"
$generatedDir = Join-Path $translateDir "generated"
$solutionPath = Join-Path $repoRoot "AudioPlaybackConnector.sln"
$winUiProjectPath = Join-Path $repoRoot "src\AudioPlaybackConnector.WinUI3\AudioPlaybackConnector.WinUI3.csproj"
$requirementsPath = Join-Path $translateDir "requirements.txt"
$po2ymoPath = Join-Path $translateDir "po2ymo.py"
$translateRcPath = Join-Path $generatedDir "translate.rc"
$dotnetCliHome = Join-Path $repoRoot ".dotnet"
$defaultNuGetPackagesPath = Join-Path $env:USERPROFILE ".nuget\packages"

New-Item -ItemType Directory -Force -Path $toolsDir | Out-Null
New-Item -ItemType Directory -Force -Path $generatedDir | Out-Null

if ($SkipNative -and -not $BuildWinUI3) {
    throw "-SkipNative requires -BuildWinUI3 so the script still has something to build."
}

function Get-VsWherePath {
    $vsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vsWhere)) {
        throw "vswhere.exe not found. Install Visual Studio 2022 or Build Tools 2022."
    }
    return $vsWhere
}

function Get-MSBuildPath {
    $vsWhere = Get-VsWherePath
    $msbuild = & $vsWhere -latest -products * -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\amd64\MSBuild.exe" | Select-Object -First 1
    if (-not $msbuild) {
        throw "MSBuild.exe not found from Visual Studio installation."
    }
    return $msbuild
}

function Get-WindowsSdkVersion {
    param(
        [string]$TargetPlatform
    )

    $includeRoot = "${env:ProgramFiles(x86)}\Windows Kits\10\Include"
    $libRoot = "${env:ProgramFiles(x86)}\Windows Kits\10\Lib"
    if (-not (Test-Path $includeRoot)) {
        throw "Windows 10 SDK not found."
    }

    $sdkVersions = Get-ChildItem $includeRoot -Directory |
        Where-Object { $_.Name -match '^\d+\.\d+\.\d+\.\d+$' } |
        Select-Object -ExpandProperty Name

    if ($TargetPlatform -eq "ARM") {
        $sdkVersions = $sdkVersions | Where-Object {
            Test-Path (Join-Path $libRoot "$_\um\arm\gdi32.lib")
        }
    }

    $sdkVersion = $sdkVersions |
        Sort-Object {
            [version]$_
        } -Descending |
        Select-Object -First 1
    if (-not $sdkVersion) {
        if ($TargetPlatform -eq "ARM") {
            throw "No Windows 10 SDK with 32-bit ARM desktop libraries found."
        }
        throw "No Windows 10 SDK version directories found."
    }
    return $sdkVersion
}

function Get-WindowsSdkRoot {
    $sdkRoot = "${env:ProgramFiles(x86)}\Windows Kits\10"
    if (-not (Test-Path $sdkRoot)) {
        throw "Windows SDK root not found."
    }
    return $sdkRoot
}

function Get-NuGetPath {
    $nugetCommand = Get-Command nuget.exe -ErrorAction SilentlyContinue
    if ($nugetCommand) {
        return $nugetCommand.Source
    }

    $nugetPath = Join-Path $toolsDir "nuget.exe"
    if (-not (Test-Path $nugetPath)) {
        Write-Host "Downloading nuget.exe..."
        Invoke-WebRequest -UseBasicParsing "https://dist.nuget.org/win-x86-commandline/latest/nuget.exe" -OutFile $nugetPath
    }
    return $nugetPath
}

function Get-PythonCommand {
    $python = Get-Command python.exe -ErrorAction SilentlyContinue
    if ($python) {
        & $python.Source "--version" | Out-Null
        if ($LASTEXITCODE -eq 0) {
            return [pscustomobject]@{
                Path = $python.Source
                PrefixArguments = @()
            }
        }
    }

    $py = Get-Command py.exe -ErrorAction SilentlyContinue
    if ($py) {
        & $py.Source "-3" "--version" | Out-Null
        if ($LASTEXITCODE -eq 0) {
            return [pscustomobject]@{
                Path = $py.Source
                PrefixArguments = @("-3")
            }
        }
    }

    throw "Python 3 not found."
}

function Get-DotNetPath {
    $dotnet = Get-Command dotnet.exe -ErrorAction SilentlyContinue
    if (-not $dotnet) {
        throw "dotnet SDK not found. Install the .NET SDK required by global.json to build the WinUI 3 shell."
    }

    return $dotnet.Source
}

function Initialize-DotNetEnvironment {
    New-Item -ItemType Directory -Force -Path $dotnetCliHome | Out-Null

    if (-not $env:DOTNET_CLI_HOME) {
        $env:DOTNET_CLI_HOME = $dotnetCliHome
    }
    if (-not $env:DOTNET_SKIP_FIRST_TIME_EXPERIENCE) {
        $env:DOTNET_SKIP_FIRST_TIME_EXPERIENCE = "1"
    }
    if (-not $env:DOTNET_NOLOGO) {
        $env:DOTNET_NOLOGO = "1"
    }
    if (-not $env:NUGET_PACKAGES -and (Test-Path $defaultNuGetPackagesPath)) {
        $env:NUGET_PACKAGES = $defaultNuGetPackagesPath
    }
}

function Get-WinUI3Platform {
    param(
        [string]$TargetPlatform
    )

    switch ($TargetPlatform) {
        "x86" { return "x86" }
        "x64" { return "x64" }
        "ARM64" { return "ARM64" }
        default { return $null }
    }
}

function Invoke-Python {
    param(
        [string[]]$Arguments
    )

    $pythonCommand = Get-PythonCommand
    & $pythonCommand.Path @($pythonCommand.PrefixArguments) @Arguments

    if ($LASTEXITCODE -ne 0) {
        throw "Python command failed: $($Arguments -join ' ')"
    }
}

function Invoke-TranslationGeneration {
    $requirements = @(Get-Content $requirementsPath |
        Where-Object {
            $line = $_.Trim()
            $line.Length -gt 0 -and -not $line.StartsWith("#")
        })

    if ($requirements.Count -gt 0) {
        Write-Host "Installing translation dependencies..."
        Invoke-Python -Arguments @("-m", "pip", "install", "-r", $requirementsPath)
    }
    else {
        Write-Host "Translation dependencies already satisfied."
    }

    Write-Host "Generating translation resources..."
    Invoke-Python -Arguments @($po2ymoPath, (Join-Path $translateDir "source\zh_CN.po"), (Join-Path $generatedDir "zh_CN.ymo"))
    Invoke-Python -Arguments @($po2ymoPath, (Join-Path $translateDir "source\zh_TW.po"), (Join-Path $generatedDir "zh_TW.ymo"))

    @(
        '#include "../../targetver.h"'
        '#include "windows.h"'
        'LANGUAGE LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED'
        '1 YMO "zh_CN.ymo"'
        'LANGUAGE LANG_CHINESE, SUBLANG_CHINESE_TRADITIONAL'
        '1 YMO "zh_TW.ymo"'
    ) | Set-Content -Path $translateRcPath -Encoding Unicode
}

function Invoke-WinUI3Build {
    param(
        [string]$DotNetPath,
        [string]$TargetConfiguration,
        [string]$TargetPlatform
    )

    if (-not (Test-Path $winUiProjectPath)) {
        throw "WinUI 3 project not found at $winUiProjectPath"
    }

    $winUiPlatform = Get-WinUI3Platform -TargetPlatform $TargetPlatform
    if (-not $winUiPlatform) {
        if ($SkipNative) {
            throw "WinUI 3 build does not support platform '$TargetPlatform'. Use x86, x64, or ARM64."
        }

        Write-Warning "Skipping WinUI 3 build for unsupported platform '$TargetPlatform'. The WinUI 3 shell currently supports x86, x64, and ARM64."
        return
    }

    Initialize-DotNetEnvironment

    $nativeSdkEnvironment = @{
        TargetPlatformIdentifier = $env:TargetPlatformIdentifier
        TargetPlatformVersion = $env:TargetPlatformVersion
        WindowsTargetPlatformVersion = $env:WindowsTargetPlatformVersion
        WindowsSDKVersion = $env:WindowsSDKVersion
        WindowsSdkDir = $env:WindowsSdkDir
        UseOSWinMdReferences = $env:UseOSWinMdReferences
    }

    try {
        Remove-Item Env:\TargetPlatformIdentifier -ErrorAction SilentlyContinue
        Remove-Item Env:\TargetPlatformVersion -ErrorAction SilentlyContinue
        Remove-Item Env:\WindowsTargetPlatformVersion -ErrorAction SilentlyContinue
        Remove-Item Env:\WindowsSDKVersion -ErrorAction SilentlyContinue
        Remove-Item Env:\WindowsSdkDir -ErrorAction SilentlyContinue
        Remove-Item Env:\UseOSWinMdReferences -ErrorAction SilentlyContinue

        Write-Host "Restoring WinUI 3 shell packages..."
        & $DotNetPath restore $winUiProjectPath "-p:Platform=$winUiPlatform" "-p:RestoreIgnoreFailedSources=true"
        if ($LASTEXITCODE -ne 0) {
            throw "dotnet restore failed for the WinUI 3 shell."
        }

        Write-Host "Building WinUI 3 shell $TargetConfiguration|$winUiPlatform..."
        & $DotNetPath build $winUiProjectPath "-c" $TargetConfiguration "-p:Platform=$winUiPlatform" "--no-restore" "-p:RestoreIgnoreFailedSources=true"
        if ($LASTEXITCODE -ne 0) {
            throw "dotnet build failed for the WinUI 3 shell."
        }
    }
    finally {
        foreach ($item in $nativeSdkEnvironment.GetEnumerator()) {
            if ([string]::IsNullOrEmpty($item.Value)) {
                Remove-Item ("Env:\" + $item.Key) -ErrorAction SilentlyContinue
            }
            else {
                Set-Item ("Env:\" + $item.Key) $item.Value
            }
        }
    }
}

function Get-NativeOutputDirectory {
    param(
        [string]$TargetConfiguration,
        [string]$TargetPlatform
    )

    $candidates = @(
        (Join-Path $repoRoot "$TargetPlatform\$TargetConfiguration"),
        (Join-Path $repoRoot $TargetConfiguration)
    )

    foreach ($candidate in $candidates) {
        if ((Test-Path $candidate) -and (Get-ChildItem $candidate -Filter "AudioPlaybackConnector*.exe" -File -ErrorAction SilentlyContinue | Select-Object -First 1)) {
            return $candidate
        }
    }

    throw "Native backend output directory not found for $TargetConfiguration|$TargetPlatform."
}

function Get-WinUI3OutputDirectory {
    param(
        [string]$TargetConfiguration,
        [string]$TargetPlatform
    )

    $basePath = Join-Path $repoRoot "src\AudioPlaybackConnector.WinUI3\bin\$TargetPlatform\$TargetConfiguration"
    if (-not (Test-Path $basePath)) {
        throw "WinUI 3 output directory not found at $basePath"
    }

    $frameworkDirectory = Get-ChildItem $basePath -Directory -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1

    if ($frameworkDirectory) {
        return $frameworkDirectory.FullName
    }

    return $basePath
}

function Sync-NativeBackendIntoWinUI3Output {
    param(
        [string]$TargetConfiguration,
        [string]$TargetPlatform
    )

    $nativeOutputDirectory = Get-NativeOutputDirectory -TargetConfiguration $TargetConfiguration -TargetPlatform $TargetPlatform
    $winUiOutputDirectory = Get-WinUI3OutputDirectory -TargetConfiguration $TargetConfiguration -TargetPlatform $TargetPlatform
    $payloadDirectory = Join-Path $winUiOutputDirectory "NativeHost"

    if (Test-Path $payloadDirectory) {
        Remove-Item -LiteralPath $payloadDirectory -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path $payloadDirectory | Out-Null

    Get-ChildItem $nativeOutputDirectory -File | Copy-Item -Destination $payloadDirectory -Force
    Write-Host "Bundled native backend into $payloadDirectory"
}

$nuget = $null
$msbuild = $null
$windowsSdkRoot = $null
$windowsSdkVersion = $null
$dotnet = $null

if (-not $SkipNative) {
    $nuget = Get-NuGetPath
    $msbuild = Get-MSBuildPath
    $windowsSdkRoot = Get-WindowsSdkRoot
    $windowsSdkVersion = Get-WindowsSdkVersion -TargetPlatform $Platform

    $env:TargetPlatformIdentifier = "Windows"
    $env:TargetPlatformVersion = $windowsSdkVersion
    $env:WindowsTargetPlatformVersion = $windowsSdkVersion
    $env:WindowsSDKVersion = "$windowsSdkVersion\"
    $env:WindowsSdkDir = "$windowsSdkRoot\"
    $env:UseOSWinMdReferences = "true"
}

if ($BuildWinUI3) {
    $dotnet = Get-DotNetPath
}

Push-Location $repoRoot
try {
    if (-not $SkipNative) {
        Invoke-TranslationGeneration

        Write-Host "Restoring NuGet packages..."
        & $nuget restore $solutionPath
        if ($LASTEXITCODE -ne 0) {
            throw "NuGet restore failed."
        }

        Write-Host "Building native backend $Configuration|$Platform..."
        Write-Host "Resolved SDK root: $windowsSdkRoot"
        Write-Host "Resolved SDK version: $windowsSdkVersion"
        & $msbuild $solutionPath "/t:Rebuild" "/p:Configuration=$Configuration;Platform=$Platform;DisableInstalledVCTargetsDefaultsUse=true;TargetPlatformIdentifier=Windows;TargetPlatformVersion=$windowsSdkVersion;WindowsTargetPlatformVersion=$windowsSdkVersion;WindowsSDKVersion=$windowsSdkVersion\\;WindowsSdkDir=$windowsSdkRoot\\;UseOSWinMdReferences=true"
        if ($LASTEXITCODE -ne 0) {
            throw "MSBuild failed."
        }
    }

    if ($BuildWinUI3) {
        Invoke-WinUI3Build -DotNetPath $dotnet -TargetConfiguration $Configuration -TargetPlatform $Platform
        try {
            Sync-NativeBackendIntoWinUI3Output -TargetConfiguration $Configuration -TargetPlatform $Platform
        }
        catch {
            if (-not $SkipNative) {
                throw
            }

            Write-Warning "Skipping native backend bundling because no native payload was found: $($_.Exception.Message)"
        }
    }
}
finally {
    Pop-Location
}
