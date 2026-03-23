# 维护路线图

这个 fork 是 AudioPlaybackConnector 当前的活跃维护分支。

## 概要
- 优先处理稳定性、构建发布和回归风险。
- 第二阶段补齐音量、输出路由和 Win11 体验。
- 在核心体验稳定后，再推进自动化和分发完善。
- 当前准备发布版本定为 `v1.2.0`，后续稳定性与兼容性修复继续在 `v1.2.x` 中推进。

## 当前 fork 已吸收的改动
- 已增强重连处理，降低重连后无声和设备列表状态残留问题。
- 已加入单实例启动行为。
- 已加入可选启动通知、开机自启、播放音量和连接时自动压低其他应用音量。
- 已更新构建与发布流程，适配当前 Visual Studio 2022 和 GitHub Actions。

## 标签体系
- `bug`：错误行为或回归
- `stability`：重连、设备状态、无声、崩溃或偶发异常
- `audio`：播放、音量、路由、编码或会话行为
- `ui`：托盘交互、设置弹层、Win11 视觉体验
- `enhancement`：非 bug 的功能增强
- `compatibility`：Windows 版本、设备、驱动或架构相关问题
- `needs-info`：缺少可复现信息
- `upstream-limitation`：大概率受 Windows 蓝牙栈或第三方驱动限制
- `planned`：已纳入路线图但尚未实现
- `done-in-fork`：已经在本 fork 中修复或显著改善

## Issue 映射
### 已在 fork 中解决或明显改善
- `#17` Don't allow multiple instances
- `#37` 中与单实例启动相关的部分
- `#42`、`#40`、`#19` 中与重连和状态残留相关的症状
- `#50` 音量为 0 时仍有残余声
- `#49` 音量忽大忽小

### 高优先级计划
- `#28`、`#31`、`#43` 输出设备选择
- `#35`、`#25`、`#40` Win11 与连接稳定性问题

### 中优先级计划
- `#6`、`#21`、`#36` 命令行自动化与手动唤起界面
- `#32` 安装器
- `#24` winget
- 基于 `#1` 延伸的 Win11 界面现代化

### 延后处理或大概率受平台限制
- `#12`、`#48` 多设备同时播放
- `#26`、`#33` 编码切换和第三方 A2DP 驱动
- `#27`、`#44` AVRCP 媒体控制与电脑端播放按钮联动
- `#38` 通话功能

## 版本路线
### 历史阶段：v1.1.0
- CI、依赖恢复和发布流程健康
- 重连修复和状态残留清理
- Windows 11 兼容性修复
- 无声问题排查与归档
- ARM 构建所需旧版 SDK 选择策略

### 当前发布线：v1.2.x
- 音量链路修复
- 源端音量为 0 时的显式静音行为
- 输出设备选择
- Win11 风格托盘设置和界面刷新
- `v1.2.1+` 继续承接稳定性、兼容性和回归修复，不再维护独立 `v1.1.x` 分支。

### 下一阶段：v1.3.0
- `connect`、`disconnect`、`show` 命令行参数
- 适配任务计划程序的调用方式
- 安装器打包
- winget 发布

## 已知平台限制
- 最低支持系统仍为 Windows 10 2004 及以上。
- 32 位 ARM 构建需要比最新 Windows 11 SDK 更旧的 SDK。
- 多设备同时播放、编码切换和通话能力，可能更多受 Windows 蓝牙栈限制，而不是单纯应用逻辑问题。
- 第三方 A2DP 驱动联动不在近期版本范围内。

---

# Maintenance Roadmap

This fork is the active maintenance branch for AudioPlaybackConnector.

## Summary
- Prioritize stability, build health, and release reliability first.
- Improve volume, output routing, and Windows 11 experience second.
- Add automation and distribution polish after the core experience is reliable.
- The current target release is `v1.2.0`, and follow-up stability work will continue in `v1.2.x`.

## Current Fork Deltas
- Reconnect handling has been hardened to reduce no-audio reconnect failures and stale picker state.
- Single-instance startup behavior has been added.
- Optional startup notification, auto-start, playback volume, and ducking of other apps have been added.
- The build and release workflow has been updated for current Visual Studio 2022 and GitHub Actions.

## Label Taxonomy
- `bug`: incorrect behavior or regression
- `stability`: reconnect, device state, no-audio, crash, or flaky behavior
- `audio`: playback, volume, routing, codec, or session behavior
- `ui`: tray UX, flyout behavior, Windows 11 visual polish
- `enhancement`: non-bug feature request
- `compatibility`: Windows version, device, driver, or architecture-specific issues
- `needs-info`: issue is missing reproduction details
- `upstream-limitation`: likely constrained by the Windows Bluetooth stack or third-party drivers
- `planned`: accepted for the roadmap but not yet implemented
- `done-in-fork`: already fixed or materially improved in this fork

## Issue Mapping
### Done in fork or materially improved
- `#17` Don't allow multiple instances
- the single-instance part of `#37`
- reconnect and stale-state symptoms from `#42`, `#40`, and `#19`

### Planned high priority
- `#50` residual audio when source volume is zero
- `#49` unstable volume
- `#28`, `#31`, `#43` output device selection
- `#35`, `#25`, `#40` Windows 11 and connection reliability issues

### Planned medium priority
- `#6`, `#21`, `#36` command-line automation and manual UI activation
- `#32` installer
- `#24` winget
- Windows 11 UI modernization extending the old light-theme request from `#1`

### Deferred or likely limited by platform
- `#12`, `#48` multi-device playback
- `#26`, `#33` codec selection and alternative A2DP drivers
- `#27`, `#44` AVRCP media control and PC playback buttons
- `#38` call support

## Version Roadmap
### Historical stage: v1.1.0
- CI, restore, and release pipeline health
- reconnect fixes and stale-state cleanup
- Windows 11 compatibility fixes
- no-audio diagnostics and issue triage
- ARM build strategy for legacy SDK selection

### Current release line: v1.2.x
- volume pipeline fixes
- explicit mute behavior at zero source volume
- output device selection
- Windows 11-style tray settings and visual refresh
- `v1.2.1+` continues to carry stability, compatibility, and regression fixes; there is no separate `v1.1.x` maintenance line anymore.

### Next stage: v1.3.0
- `connect`, `disconnect`, and `show` command-line options
- task scheduler friendly invocation
- installer packaging
- winget publishing

## Known Platform Constraints
- Minimum supported OS remains Windows 10 version 2004 or newer.
- 32-bit ARM builds require an older Windows SDK than the newest Windows 11 SDKs.
- Multi-device playback, codec switching, and telephony support may be blocked more by Windows Bluetooth stack behavior than by app logic alone.
- Third-party A2DP driver integration is out of scope for short-term releases.
