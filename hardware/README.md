# 硬件资料

这里存放 Draeyes Remote C3 Mini 简化版遥控器的硬件参考资料。

## 内容

```text
pcb/  PCB 工程或导出文件
```

## 打样前检查

- 核对 ESP32-C3 模块封装。
- 核对五个按键的方向和丝印。
- 固件使用内部上拉，按键按下时应将对应 GPIO 拉到 GND。
- 确认 PCB 上的 GPIO 与 README 中的引脚定义一致。
