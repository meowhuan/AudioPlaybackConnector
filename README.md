# AudioPlaybackConnector

Windows 10 2004+ 蓝牙音频接收 (A2DP Sink) 连接工具。

微软在 Windows 10 2004 加入了蓝牙 A2DP Sink 支持。但是需要第三方软件来管理连接。\
已经有了一个可以实现此功能的 app，但是它不可以隐藏到通知区域，而且不开源。\
所以这个项目提供了一个简单、现代且开源的替代品。

# 预览
![预览](https://cdn.jsdelivr.net/gh/meowhuan/AudioPlaybackConnector@master/AudioPlaybackConnector.gif)

# 使用方法
* 从 [releases](https://github.com/meowhuan/AudioPlaybackConnector/releases) 下载并运行 AudioPlaybackConnector。
* 在系统蓝牙设置中添加蓝牙设备。你可以右键点击通知区域的 AudioPlaybackConnector 图标，然后选择“蓝牙设置”。
* 点击 AudioPlaybackConnector 图标，然后选择要连接的设备。
* 开始使用。

# 维护说明
* 这个 fork 作为当前的活跃维护分支，优先处理稳定性、音频体验和构建发布问题。
* 当前 `v1.2.x` 已完成音量链路修复；默认手动连接和自动重连现在固定走稳定播放主线。输出设备选择保留为“已连接时的实验热切线”：只在连接已建立后改 `Output device` 时，才会尝试 active session 确认和最多一次软重连；打开设置页只会刷新 UI，不会触发重连。
* 当前稳定能力以连接、重连、音量和 ducking 为主，`v1.1.0` 作为历史稳定性阶段保留。
* 路线图、已知限制和原仓库 issue 映射见 [docs/MAINTENANCE.md](docs/MAINTENANCE.md)。
* issue 分诊规则和建议补充的信息见 [docs/ISSUE_TRIAGE.md](docs/ISSUE_TRIAGE.md)。

# 维护者构建
* 安装 Visual Studio 2022 或 Build Tools 2022，并包含 MSBuild 与 Windows 10 SDK。
* 安装 Python 3。
* 在 PowerShell 中运行 `./scripts/build.ps1 -Configuration Release -Platform x64`。
* 脚本会自动恢复 NuGet 依赖、安装翻译依赖、生成翻译资源并构建解决方案。

---

## English

Bluetooth audio playback (A2DP Sink) connector for Windows 10 version 2004 and newer.

Microsoft added Bluetooth A2DP Sink support in Windows 10 version 2004, but a third-party app is still required to manage the connection. Existing solutions can do the job, but they are not open source and usually do not behave well as a tray utility. This project provides a simple, modern, and open-source alternative.

### Preview
![Preview](https://cdn.jsdelivr.net/gh/meowhuan/AudioPlaybackConnector@master/AudioPlaybackConnector.gif)

### Usage
* Download and run AudioPlaybackConnector from [releases](https://github.com/meowhuan/AudioPlaybackConnector/releases).
* Add a Bluetooth device in Windows Bluetooth settings. You can right click the tray icon and select "Bluetooth Settings".
* Click the AudioPlaybackConnector tray icon and select the device you want to connect.
* Enjoy.

### Maintenance
* This fork is the active maintenance branch focused on stability, audio experience, and release health.
* The current `v1.2.x` line includes the completed volume-pipeline fixes; normal manual connections and auto-reconnects now stay on the stable playback path. Output device selection remains an experimental hot-switch path that is only triggered when the user changes `Output device` while already connected, with active-session confirmation and at most one soft reconnect. Opening the settings flyout only refreshes UI state and must not trigger reconnects.
* Stable capabilities currently center on connection, reconnect, volume, and ducking, while `v1.1.0` remains the historical stability stage.
* Roadmap, known constraints, and upstream issue mapping are documented in [docs/MAINTENANCE.md](docs/MAINTENANCE.md).
* Issue triage rules and required diagnostic info are documented in [docs/ISSUE_TRIAGE.md](docs/ISSUE_TRIAGE.md).

### Maintainer Build
* Install Visual Studio 2022 or Build Tools 2022 with MSBuild and the Windows 10 SDK.
* Install Python 3.
* Run `./scripts/build.ps1 -Configuration Release -Platform x64` from PowerShell.
* The script restores NuGet packages, installs translation dependencies, generates translation resources, and builds the solution.
