# AudioPlaybackConnector
[English](https://github.com/ysc3839/AudioPlaybackConnector/blob/master/README.md) | **简体中文**

Windows 10 2004+ 蓝牙音频接收 (A2DP Sink) 连接工具。

微软在 Windows 10 2004 加入了蓝牙 A2DP Sink 支持。但是需要第三方软件来管理连接。\
已经有了一个可以实现此功能的 app。但是它不可以隐藏到通知区域，而且不开源。\
所以我写了这个 app，提供一个简单的，现代且开源的替代品。

# 预览
![预览](https://cdn.jsdelivr.net/gh/ysc3839/AudioPlaybackConnector@master/AudioPlaybackConnector.gif)

# 使用方法
* 从 [releases](https://github.com/ysc3839/AudioPlaybackConnector/releases) 下载并运行 AudioPlaybackConnector。
* 在系统蓝牙设置中添加蓝牙设备。你可以右键点击通知区域的 AudioPlaybackConnector 图标然后选择“蓝牙设置”。
* 点击 AudioPlaybackConnector 图标然后选择想要连接的设备。
* 尽情享受吧！

# 维护者构建
* 安装 Visual Studio 2022 或 Build Tools 2022，并包含 MSBuild 与 Windows 10 SDK `10.0.22621.0`。
* 安装 Python 3。
* 在 PowerShell 中运行 `./scripts/build.ps1 -Configuration Release -Platform x64`。
* 该脚本会自动恢复 NuGet 依赖、安装翻译依赖、生成翻译资源并构建解决方案。
