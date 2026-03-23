# AudioPlaybackConnector
**English** | [简体中文](https://github.com/ysc3839/AudioPlaybackConnector/blob/master/README.zh_CN.md)

Bluetooth audio playback (A2DP Sink) connector for Windows 10 2004+.

Microsoft added Bluetooth A2DP Sink to Windows 10 2004. However, a third-party app is required to manage connection.\
There is already an app can do this job. However it can't hide to notification area and it's not open-source.\
So I write this app, provide a simple, modern and open-source alternative.

# Preview
![Preview](https://cdn.jsdelivr.net/gh/ysc3839/AudioPlaybackConnector@master/AudioPlaybackConnector.gif)

# Usage
* Download and run AudioPlaybackConnector from [releases](https://github.com/ysc3839/AudioPlaybackConnector/releases).
* Add a bluetooth device in system bluetooth settings. You can right click AudioPlaybackConnector icon in notification area and select "Bluetooth Settings".
* Click AudioPlaybackConnector icon and select the device you want to connect.
* Enjoy!

# Maintainer Build
* Install Visual Studio 2022 or Build Tools 2022 with MSBuild and the Windows 10 SDK `10.0.22621.0`.
* Install Python 3.
* Run `./scripts/build.ps1 -Configuration Release -Platform x64` from PowerShell.
* The script restores NuGet packages, installs translation dependencies, generates translation resources, and builds the solution.
