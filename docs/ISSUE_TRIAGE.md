# Issue 分诊指南

## 目标
- 让 issue 保持可处理。
- 让路线图状态对外可见。
- 区分应用自身问题与 Windows、驱动或蓝牙栈限制。

## 缺少信息时统一补充这些内容
- Windows 版本和构建号
- 应用版本或提交哈希
- 手机或音源设备型号
- 蓝牙适配器型号或驱动信息
- 当前是否输出到默认音频设备
- 精确错误信息、HRESULT 或截图
- 是否在断开重连或重启应用后仍可复现

在可复现信息不足前，统一加 `needs-info`。

## 分类规则
- `stability`：重连失败、已连接但无声、状态残留、随机断开
- `audio`：残余声、音量不稳定、输出路由、爆音、杂音、静音行为
- `compatibility`：Windows 11、Windows Server、ARM、特定设备或驱动
- `ui`：托盘行为、设置弹层、Win11 视觉适配
- `upstream-limitation`：大概率依赖 Windows 蓝牙栈能力的请求

## 回复规则
- 如果问题已经在本 fork 修复，直接回复对应版本或提交，并加 `done-in-fork`。
- 如果问题已经纳入路线图，加 `planned` 并指向维护路线文档中的对应章节。
- 如果明显是平台限制，要明确说明，不要长期挂成“待修复”。
- 如果只是重连不稳定的重复症状，统一关联到重连修复主线，尽量合并讨论。

## 发布前关闭检查
关闭高优先级 issue 前，至少确认：
- 有明确复现步骤
- 有受影响平台信息
- 有明确预期行为
- 有测试通过或人工验证记录
- 若涉及用户可见变化，发布说明中有对应描述

---

# Issue Triage Guide

## Goals
- Keep issues actionable.
- Make roadmap status visible.
- Separate app bugs from Windows, driver, or Bluetooth stack limitations.

## Required Reproduction Info
Ask for the following when missing:
- Windows version and build number
- app version or commit
- phone or source device model
- Bluetooth adapter model or driver package when relevant
- whether audio is routed to the default output device
- exact error message, HRESULT, or screenshot
- whether the issue reproduces after disconnect/reconnect and app restart

Apply `needs-info` until the report is reproducible enough to classify.

## Classification Rules
- Use `stability` for reconnect failures, stale connected state, random drops, and no-audio reports.
- Use `audio` for residual sound, unstable volume, output routing, pops, glitches, or mute behavior.
- Use `compatibility` for Windows 11, Windows Server, ARM, device-class, or driver-specific reports.
- Use `ui` for tray behavior, flyout appearance, and Windows 11 visual polish.
- Use `upstream-limitation` when the requested behavior likely depends on Windows Bluetooth stack support.

## Response Rules
- If the issue is already fixed in this fork, reply with the version or commit where it landed and add `done-in-fork`.
- If the issue matches a planned roadmap item, add `planned` and link the relevant roadmap section.
- If the issue is blocked by platform limitations, say that directly instead of leaving it open without context.
- If the issue is a duplicate symptom of reconnect instability, link the reconnect work and consolidate discussion.

## Release Gate Checks
Before closing a high-priority issue, confirm:
- reproduction steps exist
- affected platform is recorded
- expected behavior is explicit
- a test pass or manual verification exists for the fix
- release notes mention the user-visible behavior change when relevant
