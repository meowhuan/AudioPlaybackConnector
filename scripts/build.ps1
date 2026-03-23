param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",

    [ValidateSet("x86", "x64", "ARM", "ARM64")]
    [string]$Platform = "x64"
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repoRoot = Split-Path -Parent $PSScriptRoot
$toolsDir = Join-Path $repoRoot ".tools"
$translateDir = Join-Path $repoRoot "translate"
$generatedDir = Join-Path $translateDir "generated"
$solutionPath = Join-Path $repoRoot "AudioPlaybackConnector.sln"
$requirementsPath = Join-Path $translateDir "requirements.txt"
$po2ymoPath = Join-Path $translateDir "po2ymo.py"
$translateRcPath = Join-Path $generatedDir "translate.rc"

New-Item -ItemType Directory -Force -Path $toolsDir | Out-Null
New-Item -ItemType Directory -Force -Path $generatedDir | Out-Null

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
    $sdkRoot = "${env:ProgramFiles(x86)}\Windows Kits\10\Include"
    if (-not (Test-Path $sdkRoot)) {
        throw "Windows 10 SDK not found."
    }

    $sdkVersion = Get-ChildItem $sdkRoot -Directory | Sort-Object Name -Descending | Select-Object -First 1 -ExpandProperty Name
    if (-not $sdkVersion) {
        throw "No Windows 10 SDK versions found."
    }
    return $sdkVersion
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
    $py = Get-Command py.exe -ErrorAction SilentlyContinue
    if ($py) {
        return @($py.Source, "-3")
    }

    $python = Get-Command python.exe -ErrorAction SilentlyContinue
    if ($python) {
        return @($python.Source)
    }

    throw "Python 3 not found."
}

function Invoke-Python {
    param(
        [string[]]$Arguments
    )

    $pythonCommand = Get-PythonCommand
    if ($pythonCommand.Length -gt 1) {
        & $pythonCommand[0] $pythonCommand[1] @Arguments
    }
    else {
        & $pythonCommand[0] @Arguments
    }
}

function Invoke-TranslationGeneration {
    Write-Host "Installing translation dependencies..."
    Invoke-Python -Arguments @("-m", "pip", "install", "-r", $requirementsPath)

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

$nuget = Get-NuGetPath
$msbuild = Get-MSBuildPath
$windowsSdkVersion = Get-WindowsSdkVersion

Push-Location $repoRoot
try {
    Invoke-TranslationGeneration

    Write-Host "Restoring NuGet packages..."
    & $nuget restore $solutionPath
    if ($LASTEXITCODE -ne 0) {
        throw "NuGet restore failed."
    }

    Write-Host "Building $Configuration|$Platform..."
    & $msbuild $solutionPath "/t:Build" "/p:Configuration=$Configuration;Platform=$Platform;TargetPlatformIdentifier=Windows;TargetPlatformVersion=$windowsSdkVersion;WindowsTargetPlatformVersion=$windowsSdkVersion"
    if ($LASTEXITCODE -ne 0) {
        throw "MSBuild failed."
    }
}
finally {
    Pop-Location
}
