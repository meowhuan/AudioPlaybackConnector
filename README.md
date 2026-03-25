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
* 左键点击通知区域图标会打开新的 WinUI 3 主界面；右键点击通知区域图标会打开快捷设置菜单。
* 在 WinUI 3 主界面中查看蓝牙播放设备、输出设备、连接状态，并发起连接/断开。
* 开始使用。

# 维护说明
* 这个 fork 作为当前的活跃维护分支，优先处理稳定性、音频体验和构建发布问题。
* 当前 `v1.2.x` 已完成音量链路修复；默认手动连接和自动重连现在固定走稳定播放主线。输出设备选择保留为“已连接时的实验热切线”：只在连接已建立后改 `Output device` 时，才会尝试 active session 确认和最多一次软重连；打开设置页只会刷新 UI，不会触发重连。
* 当前 GUI 已切换为独立的 `WinUI 3` 前端，路径为 `src/AudioPlaybackConnector.WinUI3`。托盘后端继续保留为原生宿主，但旧的 XAML Islands 托盘 UI 已移除：左键打开 WinUI 3 主界面，右键打开快捷设置。
* 当前前后端已通过命名管道接通命令与事件流：WinUI 3 可实时接收连接状态变化，并可请求后端执行连接、断开、打开设备选择器、重载设置和退出。
* 当前构建输出会把原生托盘后端打包到 WinUI 3 输出目录下的 `NativeHost`，便于一起分发与测试。
* 当前 WinUI 3 主界面默认使用简体中文，并已按 `Android-Cam-Bridge` 的桌面布局思路重构为“左侧导航 + 顶部摘要 + 分区内容”的 Win11 风格界面。
* 当前稳定能力以连接、重连、音量和 ducking 为主，`v1.1.0` 作为历史稳定性阶段保留。
* 路线图、已知限制和原仓库 issue 映射见 [docs/MAINTENANCE.md](docs/MAINTENANCE.md)。
* issue 分诊规则和建议补充的信息见 [docs/ISSUE_TRIAGE.md](docs/ISSUE_TRIAGE.md)。

# 维护者构建
* 安装 Visual Studio 2022 或 Build Tools 2022，并包含 MSBuild 与 Windows 10 SDK。
* 安装 Python 3。
* 在 PowerShell 中运行 `./scripts/build.ps1 -Configuration Release -Platform x64` 以构建原生托盘后端。
* 追加 `-BuildWinUI3` 可同时构建新的 WinUI 3 调试前端；使用 `-BuildWinUI3 -SkipNative` 可只构建 WinUI 3 前端。
* 当启用 `-BuildWinUI3` 且原生后端可用时，脚本还会把原生后端产物同步到 WinUI 3 输出目录下的 `NativeHost`，方便一起分发和调试。
* 脚本会自动恢复 NuGet 依赖、生成翻译资源并构建解决方案；WinUI 3 前端部分会走 `dotnet restore/build`。
* 独立的 WinUI 3 调试前端仍可通过 `dotnet build src/AudioPlaybackConnector.WinUI3/AudioPlaybackConnector.WinUI3.csproj` 单独构建。

---

## English

Bluetooth audio playback (A2DP Sink) connector for Windows 10 version 2004 and newer.

Microsoft added Bluetooth A2DP Sink support in Windows 10 version 2004, but a third-party app is still required to manage the connection. Existing solutions can do the job, but they are not open source and usually do not behave well as a tray utility. This project provides a simple, modern, and open-source alternative.

### Preview
![Preview](https://cdn.jsdelivr.net/gh/meowhuan/AudioPlaybackConnector@master/AudioPlaybackConnector.gif)

### Usage
* Download and run AudioPlaybackConnector from [releases](https://github.com/meowhuan/AudioPlaybackConnector/releases).
* Add a Bluetooth device in Windows Bluetooth settings. You can right click the tray icon and select "Bluetooth Settings".
* Left click the tray icon to open the WinUI 3 main window. Right click the tray icon to open quick settings.
* Use the WinUI 3 window to inspect Bluetooth playback devices, render outputs, live backend state, and connection actions.
* Enjoy.

### Maintenance
* This fork is the active maintenance branch focused on stability, audio experience, and release health.
* The current `v1.2.x` line includes the completed volume-pipeline fixes; normal manual connections and auto-reconnects now stay on the stable playback path. Output device selection remains an experimental hot-switch path that is only triggered when the user changes `Output device` while already connected, with active-session confirmation and at most one soft reconnect. Opening the settings flyout only refreshes UI state and must not trigger reconnects.
* The GUI now runs through a separate `WinUI 3` frontend at `src/AudioPlaybackConnector.WinUI3`, while the tray/backend host remains native. The old XAML Islands tray UI has been removed: left click opens the WinUI 3 window and right click opens quick settings.
* The frontend and backend now communicate through named-pipe commands plus a live event stream. The WinUI 3 app can request connect/disconnect, picker opening, settings reload, and clean exit, and it receives live backend state updates without manual refresh.
* Build outputs now bundle the native tray backend into the WinUI 3 output under `NativeHost` for distribution and local testing.
* The WinUI 3 frontend now defaults to Simplified Chinese and has been refactored into a Windows 11 style layout inspired by `Android-Cam-Bridge`: left navigation, top summary cards, and sectioned content areas.
* Stable capabilities currently center on connection, reconnect, volume, and ducking, while `v1.1.0` remains the historical stability stage.
* Roadmap, known constraints, and upstream issue mapping are documented in [docs/MAINTENANCE.md](docs/MAINTENANCE.md).
* Issue triage rules and required diagnostic info are documented in [docs/ISSUE_TRIAGE.md](docs/ISSUE_TRIAGE.md).

### Maintainer Build
* Install Visual Studio 2022 or Build Tools 2022 with MSBuild and the Windows 10 SDK.
* Install Python 3.
* Run `./scripts/build.ps1 -Configuration Release -Platform x64` from PowerShell to build the native tray backend.
* Add `-BuildWinUI3` to also build the new WinUI 3 debug shell, or use `-BuildWinUI3 -SkipNative` to build only the WinUI 3 shell.
* When `-BuildWinUI3` is enabled and the native backend exists, the script also bundles the native backend into the WinUI 3 output under `NativeHost` for distribution and local testing.
* The script restores NuGet packages, generates translation resources, and builds the solution; the WinUI 3 shell portion uses `dotnet restore/build`.
* The standalone WinUI 3 debug shell can still be built directly with `dotnet build src/AudioPlaybackConnector.WinUI3/AudioPlaybackConnector.WinUI3.csproj`.
