# CoreS3 AirPlay 真播放完整接入方案（双模式 + AP 配网）

## Summary
在现有工程上实现“可被 iPhone/iPad/Mac 发现并真实播放”的 AirPlay 音箱能力，同时保留现有人脸追踪模式与开机触屏模式选择。  
首版采用“集成现成 AirPlay/RAOP 协议栈 + CoreS3 本地音频输出适配 + AP 配网页配网”，不再扩展当前占位 RTSP 壳层。  
目标完成标准：苹果设备可稳定连接、发起播放、设备有声音输出，模式切换不影响原有人脸功能。

## Key Changes
- 协议层替换：
  将当前 `AirPlayReceiver` 的占位实现替换为真实 AirPlay/RAOP 接入（优先集成 `emlynmac/airplay-esp32s`，固定 commit/tag；若该版本与当前 Arduino/SDK不兼容，则按预设回退到 `rbouteiller/airplay-esp32` 适配分支）。
- 音频输出适配：
  保留 `AudioOutput` 作为统一音频抽象，对接 `CoreS3.Speaker`，由协议层 PCM 回调驱动 `playRaw()`，统一采样率/声道/缓冲策略，避免协议库直接操作硬件。
- 模式架构保持双模式：
  保留当前开机触摸选择页，`FaceTracking` 与 `AirPlayReceiver` 互斥初始化，AirPlay 模式不触发相机/舵机链路。
- 新增 AP 配网页配网模块：
  当无可用 Wi-Fi 凭据或连接失败时，设备启动 AP + Captive Portal（`DNSServer + WebServer`）输入 SSID/密码，保存到 `Preferences`；下次开机优先自动连接。
- AirPlay 状态展示：
  复用现有 AirPlay 状态页，显示设备名、Wi-Fi 状态、IP、播放状态、音量；错误态（配网失败/协议初始化失败）有可读提示而非死循环。
- 配置与依赖管理：
  `platformio.ini` 保持已验证上传参数（`m5stack-cores3 + 115200 + --no-stub`）；新增 AirPlay 协议栈依赖与配网依赖（`WebServer`/`DNSServer`/`Preferences` 若未显式引入则按 Arduino 内置库使用）。

## Important Interfaces / Behavior Changes
- `AirPlayReceiver` 对外接口固定为：
  - `init()`
  - `start()`
  - `update()`
  - `stop()`
  - `isReady()`
  - `isPlaying()`
  - `getStatusText()`
  - `getIPAddress()`
- 新增 `WiFiProvisioningManager`（或等价模块）接口：
  - `begin()`
  - `update()`
  - `loadCredentials()`
  - `saveCredentials(ssid, pass)`
  - `connectSaved()`
  - `startConfigPortal()`
  - `isProvisioned()`
- `main.cpp` 行为改为：
  - 开机模式选择后，若进 AirPlay：先尝试已保存 Wi-Fi -> 失败则进配网门户 -> 成功后启动 AirPlay 协议栈。
  - 人脸模式逻辑保持原行为，不受配网/协议库影响。

## Test Plan
- 构建验证：
  `pio run` 成功，双模式均可编译通过。
- 配网验证：
  1. 清空凭据后上电，出现 AP 配网页。  
  2. 手机连接 AP 并提交家用 Wi-Fi。  
  3. 设备保存并成功联网，状态页显示有效 IP。
- AirPlay 发现与播放验证：
  1. iPhone/iPad/Mac 可看到设备名。  
  2. 发起 AirPlay 播放后，CoreS3 有稳定音频输出。  
  3. 暂停/停止后状态从 Playing 回到 Ready。
- 回归验证：
  1. 选择 FaceTracking 后，相机/检测/舵机行为与当前一致。  
  2. 选择 AirPlay 后，不访问相机/舵机路径。  
  3. 模式切换与超时默认模式行为正常。
- 稳定性验证：
  连续播放 30 分钟无明显断流/卡死；Wi-Fi 断开后状态提示明确并可恢复。

## Assumptions
- 采用“集成现成 AirPlay 栈”而非自研 RAOP 协议。
- 首版配网采用 AP 配网页，不做屏幕键盘输入密码。
- 双模式并存，默认模式继续保守设置为 `FaceTracking`。
- 当前占位 RTSP/7001 PCM 调试逻辑不作为最终 AirPlay 成功标准，将被真实协议接入替换。
