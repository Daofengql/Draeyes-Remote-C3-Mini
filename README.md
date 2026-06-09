# Draeyes Microcontroller 简化版遥控器

Draeyes Microcontroller 是 Draeyes 兽装可动眼系列的简化版五键遥控器固件与硬件资料仓库。该版本面向 ESP32-C3 主控硬件，使用上、下、左、右、中五个按键，通过 ESP-NOW 向接收端发送预设式控制数据。

与 ESP32-C6 完整版 Minicontroller 相比，简化版去掉了摇杆、光照传感器、EEPROM 摇杆校准、AP/MQTT 辅助功能和外壳资料，保留自动眼动与自动眨眼算法，适合低成本、小体积、少按键的控制器硬件。

## 硬件配置

| 项目 | 说明 |
| --- | --- |
| 主控 | ESP32-C3 |
| 无线协议 | ESP-NOW |
| 输入方式 | 五个独立按键 |
| 按键 | Up、Down、Left、Right、Center |

### 引脚定义

| 按键 | GPIO |
| --- | --- |
| Up | 3 |
| Center | 4 |
| Down | 7 |
| Left | 6 |
| Right | 5 |

固件使用 `INPUT_PULLUP`，因此按键按下时应将 GPIO 拉到 GND。

## 目录结构

```text
.
├── Draeyes-Microcontroller.ino     # Arduino 主程序入口
├── RealisticEyeAnimation.h         # 自动眼动与自动眨眼算法
├── hardware/
│   └── pcb/                        # PCB 工程或导出文件
├── .gitignore
├── LICENSE
└── README.md
```

为了兼容 Arduino IDE，`.ino` 文件保留在仓库根目录。

## Arduino 依赖

请在 Arduino IDE 或 PlatformIO 中安装：

| 库 | 用途 |
| --- | --- |
| ArduinoJson | 生成 ESP-NOW JSON 数据包 |

同时需要安装支持 ESP32-C3 的 ESP32 Arduino Core。

## 编译与烧录

1. 用 Arduino IDE 打开 `Draeyes-Microcontroller.ino`。
2. 选择 ESP32-C3 对应开发板。
3. 安装 ArduinoJson。
4. 使用 USB 连接遥控器。
5. 编译并上传固件。
6. 打开串口监视器，波特率设为 `115200`，查看模式、按键和发送数据调试信息。

## 通信数据格式

控制器通过 ESP-NOW 发送 JSON 数据。

常规控制数据：

```json
{
  "req": "controller",
  "data": {
    "type": "preset",
    "j1PotX": 0.0,
    "j1PotY": 0.0,
    "bkl": 1.0,
    "bkr": 1.0
  }
}
```

配对请求：

```json
{ "req": "pairing" }
```

### 字段范围

| 字段 | 范围 | 含义 |
| --- | --- | --- |
| `type` | `"preset"` | 表示来自简化版预设遥控器 |
| `j1PotX` | `-1.0..1.0` | 眼球水平位置 |
| `j1PotY` | `-1.0..1.0` | 眼球垂直位置 |
| `bkl` | `0.0..1.0` | 左眼皮开合度 |
| `bkr` | `0.0..1.0` | 右眼皮开合度 |

## 按键布局

```text
            [ Up ]

[ Left ]  [ Center ]  [ Right ]

           [ Down ]
```

## 启动状态

开机后默认进入：

```text
手动方向 + 自动眨眼
```

串口会约每 500 ms 输出一次当前模式、按键状态、发送的 X/Y 值和眼皮开合值。

## 配对说明

1. 打开接收端设备。
2. 打开遥控器。
3. 同时按住 `Left + Right` 约 2 秒。
4. 遥控器发送 `{ "req": "pairing" }`。
5. 接收端开始响应控制后即可松开按键。

## Center 键锁定

Center 键有两个用途：三击切换模式，以及在特定模式下作为手动眨眼键。

切换 Center 键锁定：

1. 同时按住 `Up + Down` 约 2 秒。
2. 遥控器切换 Center 键锁定状态。
3. 锁定后，Center 三击切换模式会被禁用。
4. 在“锁定视线 + 手动眨眼”模式中，锁定后的 Center 键用于控制眨眼。

## 模式切换

在 Center 键未锁定时，1 秒内连续按下 `Center` 3 次，即可循环切换模式。

| 模式 | 视线控制 | 眼皮控制 |
| --- | --- | --- |
| 手动方向 + 自动眨眼 | 方向键直接发送固定方向 | 自动眨眼 |
| 全自动 | 自动仿生眼动 | 自动眨眼 |
| 锁定视线 + 自动眨眼 | 方向键微调一个锁定 X/Y 坐标 | 自动眨眼 |
| 锁定视线 + 手动眨眼 | 方向键微调一个锁定 X/Y 坐标 | Center 锁定后按下闭眼，松开睁眼 |

## 各模式操作

### 手动方向 + 自动眨眼

| 按键 | 功能 |
| --- | --- |
| Left | 发送 `j1PotX = -1.0` |
| Right | 发送 `j1PotX = 1.0` |
| Up | 发送 `j1PotY = 1.0` |
| Down | 发送 `j1PotY = -1.0` |
| Center | 在未锁定时让 X/Y 回中 |

眼皮开合由 `RealisticEyeAnimation.h` 中的自动眨眼算法控制。

### 全自动

固件自动生成眼球移动和眨眼，方向键不会直接控制 X/Y。

### 锁定视线 + 自动眨眼

方向键不再发送瞬时方向，而是微调一个保存于内存中的 X/Y 锁定值。

| 按键 | 功能 |
| --- | --- |
| Up | 增加锁定 Y |
| Down | 减少锁定 Y |
| Left | 减少锁定 X |
| Right | 增加锁定 X |

每次按下步进 `0.04`。长按方向键 400 ms 后开始连续步进，每 80 ms 更新一次。

### 锁定视线 + 手动眨眼

该模式使用同样的锁定 X/Y 微调方式。

当 Center 键已锁定：

| Center 状态 | 眼皮开合 |
| --- | --- |
| 松开 | `bkl = 1.0`，`bkr = 1.0` |
| 按下 | `bkl = 0.0`，`bkr = 0.0` |

当 Center 键未锁定时，眼皮保持睁开，Center 仍用于三击切换模式。

## 常见问题

### 接收端没有响应

确认接收端已开机，并重新执行 `Left + Right` 配对。还需要确认双方使用同一个 ESP-NOW 信道。

### Center 三击无法切换模式

Center 键可能处于锁定状态。长按 `Up + Down` 约 2 秒切换锁定状态。

### 手动眨眼没有反应

手动眨眼只在“锁定视线 + 手动眨眼”模式中生效，并且需要先启用 Center 键锁定。

### 编译时出现 ESP-NOW 回调类型错误

请升级 ESP32 Arduino Core。ESP32-C3 需要较新的 ESP-NOW 支持。

## 硬件资料

硬件资料放在 `hardware/pcb/`。

打样前请核对 ESP32-C3 模块封装、按键方向、GPIO 接线和上拉方式。

## 许可证

本项目使用 MIT License 开源，详见 `LICENSE`。
