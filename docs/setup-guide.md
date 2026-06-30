# OpenAMP 异构多核通信部署指南

> **更新**: 2026-05-28 | **当前架构**: Linux 侧接收 + FreeRTOS 主控侧实际 CPU1（设备树写 CPU3）+ GD32 主控移植路线
>
> **Dashboard 说明**: 当前唯一面板是 `state_estimation/dashboard_server.py`，端口 5000；本文中旧板端 8080/8000 Web 面板记录已废弃，不再作为当前路线。

## 前置条件

- 飞腾派 CEK8903 开发板 (IP: 192.168.88.10)
- 开发机: Ubuntu/Debian (本机: /home/alientek)
- 工具: `dtc`, `mkimage`, `dumpimage`, `ssh`, `scp`

## 步骤 1: 修改设备树

### 1.1 获取当前 fitImage

```bash
# 从开发板提取
ssh user@192.168.88.10 "sudo dd if=/dev/mmcblk0 bs=1M skip=4 count=60" > current_fitImage

# 拆出内核和 dtb
dumpimage -T flat_dt -p 0 -o kernel.gz current_fitImage
dumpimage -T flat_dt -p 1 -o board.dtb current_fitImage
```

### 1.2 修改 dtb 添加 OpenAMP 和 LoRa UART 节点

```bash
dtc -I dtb -O dts board.dtb > board.dts
```

在 `board.dts` 的根节点 `/` 内，`memory@0` 之前添加:

```dts
reserved-memory {
    #address-cells = <0x02>;
    #size-cells = <0x02>;
    ranges;
    rproc: rproc@b0100000 {
        no-map;
        reg = <0x0 0xb0100000 0x0 0x19900000>;
    };
};

homo_rproc: homo_rproc@0 {
    compatible = "homo,rproc";
    status = "okay";
    homo_core0: homo_core0@b0100000 {
        compatible = "homo,rproc-core";
    /* 设备树目前仍写 remote-processor = <3>；实测 FreeRTOS 运行在 CPU1，调试时以此口径记录 */
    remote-processor = <3>;
        inter-processor-interrupt = <9>;
        memory-region = <&rproc>;
        firmware-name = "openamp_core0.elf";
    };
};

&uart3 {
    status = "okay";
    pinctrl-names = "default";
    pinctrl-0 = <&uart3_pins>;
};

&pio2 {
    uart3_pins: uart3-pins {
        pins = "GPIO2_8", "GPIO2_9", "GPIO2_10", "GPIO2_11";
        function = "uart3";
        drive-strength = <8>;
        bias-pull-up;
    };
};
```

**说明**:
- 当前 LoRa 实际接线以 J1 Pin8/Pin10 的 UART2 口径为准；上面 `uart3` 片段是早期设备树记录，不能再作为当前 LoRa 接线事实
- 设备树目前仍写 `remote-processor = <3>`，但实测 FreeRTOS 运行在 CPU1
- 预留 `0xB0100000` (409MB) 给 OpenAMP 共享内存

### 1.3 重新编译和打包

```bash
dtc -I dts -O dtb board.dts > new_board.dtb

# 创建 ITS 文件并打包
mkimage -f new_fit.its new_fitImage
```

### 1.4 刷入开发板

```bash
scp new_fitImage user@192.168.88.10:~/fitImage
ssh user@192.168.88.10 "sudo runtime_replace_bootloader.sh fitImage && sudo reboot"
```

## 步骤 2: 编译 FreeRTOS 固件 (GD32主控移植版)

### 2.1 工具链准备

- **必须**: `aarch64-none-elf-gcc` (ARM bare-metal, 非 Linux 版本)
- **位置**: `/home/alientek/Phytium_syscode/GCC编译器/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-elf/`

### 2.2 编译流程

本项目的 FreeRTOS 代码位于 [/home/alientek/Phytium/freertos/](file:///home/alientek/Phytium/freertos/)，依赖飞腾官方 SDK。

```bash
# 环境变量
export AARCH64_CROSS_PATH="/home/alientek/Phytium_syscode/GCC编译器/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-elf"

# 进入飞腾 FreeRTOS SDK
cd /home/alientek/Phytium_syscode/phytium-free-rtos-sdk-master/example/system/amp/openamp_for_linux

# 配置 + 编译
make config_pe2204_phytiumpi_aarch64
make clean && make all
```

**输出**: `pe2204_aarch64_phytiumpi_openamp_for_linux.elf`

> **备选**: 如需编译 Bare-metal 版本，参考 [knowledge-base.md](knowledge-base.md)。

## 步骤 3: 部署固件

```bash
scp pe2204_aarch64_phytiumpi_openamp_for_linux.elf user@192.168.88.10:/tmp/openamp_core0.elf
ssh user@192.168.88.10 "sudo cp /tmp/openamp_core0.elf /lib/firmware/ && sudo chmod 644 /lib/firmware/openamp_core0.elf"
```

## 步骤 4: 编译 Linux 侧应用

```bash
# 交叉编译 master_receiver (使用 Linux 编译器)
export CROSS_COMPILE="/home/alientek/Phytium_syscode/GCC编译器/gcc-arm-10.2-2020.11-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-"
cd /home/alientek/Phytium/src/openamp-demo/linux-master
make clean && make

# 部署
scp master_receiver user@192.168.88.10:~/
```

## 步骤 5: 启动 OpenAMP

```bash
# 加载模块
ssh user@192.168.88.10 "sudo modprobe rpmsg_char rpmsg_ctrl"

# 启动远程处理器
ssh user@192.168.88.10 "echo start | sudo tee /sys/class/remoteproc/remoteproc0/state"

# 验证状态
ssh user@192.168.88.10 "cat /sys/class/remoteproc/remoteproc0/state"
# 应输出: running

# 验证 RPMsg 通道
ssh user@192.168.88.10 "ls /sys/bus/rpmsg/devices/"
# 应看到: virtio0.rpmsg-openamp-demo-channel.-1.0
```

## 步骤 6: 绑定通道并运行

```bash
# 绑定驱动
ssh user@192.168.88.10 "
sudo sh -c 'echo rpmsg_chrdev > /sys/bus/rpmsg/devices/virtio0.rpmsg-openamp-demo-channel.-1.0/driver_override'
sudo sh -c 'echo virtio0.rpmsg-openamp-demo-channel.-1.0 > /sys/bus/rpmsg/drivers/rpmsg_chrdev/bind'
sudo chmod 666 /dev/rpmsg0 /dev/rpmsg_ctrl0
"

# 运行 master_receiver 接收命令
ssh user@192.168.88.10 "./master_receiver"
```

### 预期输出

```
Opening rpmsg channel...
Waiting for messages...
[READY] master_receiver running, press Ctrl+C to exit.
```

此时，如果 FreeRTOS 侧有命令生成，会在这里打印出来：

```
[CMD] node=0 cmd=REQ_WAVE(0x10)
[CMD] node=2 cmd=REQ_FAULT_LIST(0x11)
```

## 完整启动脚本 (一次执行)

```bash
# 一键启动
sudo modprobe rpmsg_char rpmsg_ctrl
echo start | sudo tee /sys/class/remoteproc/remoteproc0/state
sleep 0.5
echo rpmsg_chrdev | sudo tee /sys/bus/rpmsg/devices/virtio0.rpmsg-openamp-demo-channel.-1.0/driver_override
echo virtio0.rpmsg-openamp-demo-channel.-1.0 | sudo tee /sys/bus/rpmsg/drivers/rpmsg_chrdev/bind
sudo chmod 666 /dev/rpmsg0 /dev/rpmsg_ctrl0
./master_receiver
```

## 步骤 7: 停止

```bash
# Ctrl+C 退出 master_receiver
echo stop | sudo tee /sys/class/remoteproc/remoteproc0/state
# 状态变为: offline
```

## 完整停止流程

```bash
# 停止 master_receiver (Ctrl+C)
# 停止从核
echo stop | sudo tee /sys/class/remoteproc/remoteproc0/state
# 确认: cat /sys/class/remoteproc/remoteproc0/state → offline

# 卸载内核模块 (顺序: 先 ctrl 后 char)
sudo rmmod rpmsg_ctrl
sudo rmmod rpmsg_char

# 验证清理完成
ls /dev/rpmsg*          # 应无输出
ls /sys/bus/rpmsg/devices/  # 应无输出或为空
```

## 快速重启流程

```bash
# 1. 启动从核
echo start | sudo tee /sys/class/remoteproc/remoteproc0/state

# 2. 加载模块 (如已卸载)
sudo modprobe rpmsg_char rpmsg_ctrl

# 3. 绑定通道 (如已解绑)
echo rpmsg_chrdev | sudo tee /sys/bus/rpmsg/devices/virtio0.rpmsg-openamp-demo-channel.-1.0/driver_override
echo virtio0.rpmsg-openamp-demo-channel.-1.0 | sudo tee /sys/bus/rpmsg/drivers/rpmsg_chrdev/bind
sudo chmod 666 /dev/rpmsg0 /dev/rpmsg_ctrl0

# 4. 启动接收
./master_receiver
```

## 步骤 8: 当前 UKF Dashboard

当前唯一 Web 面板是 `state_estimation/dashboard_server.py`，在虚拟机/开发环境运行，端口 5000：

```bash
cd /home/alientek/Phytium/state_estimation
python dashboard_server.py
# 浏览器打开 http://localhost:5000
```

旧板端 `./dashboard_server`、8080/8000 Web 监控面板和 `/tmp/dashboard_data.csv` 记录已废弃，不再作为当前路线；如发现相关旧代码且不被 `state_estimation/dashboard_server.py` 使用，应删除或标记为历史。

当前面板显示内容：
- UKF 发电机转子角度/转速状态估计
- 5s 和 15s 模拟故障时段
- 节点传输状态与故障回放
- 自然灾害风险预警
- 飞书/微信通知集成

### 8.1 命令行监控 (LoRa/RPMsg 轻量替代)

如果不需要 Web 界面，可以用终端版的 `master_receiver`：

```bash
ssh user@192.168.88.10 "./master_receiver"
```

输出示例：
```
==========================================
 OpenAMP Master Data Receiver
==========================================
[CMD  #001] node=1 cmd=REQ_WAVE(0x01)
[CMD  #002] node=2 cmd=REQ_FAULT_LIST(0x02)
[PING #1] Linux→FreeRTOS→Linux round-trip OK!
```

---

## 步骤 9: 自动化测试套件

### 9.1 测试架构

```
开发主机 (x86_64)                 飞腾派 (192.168.88.10)
┌──────────────────┐    SSH      ┌──────────────────────────┐
│ test_runner.sh   │──────────► │ /home/user/demo/tests/    │
│ (总控脚本)       │             │ ├── test_rpmsg_link       │
│ ├── TC01: PING   │             │ ├── test_fault_inject     │
│ ├── TC02: 故障注入│             │ ├── test_command          │
│ ├── TC03: 命令监听│             │ ├── test_encrypt          │
│ ├── TC04: 加密验证│             │ ├── test_stress           │
│ └── TC05: 压力测试│             │ └── test_panel (交互式)   │
│                  │             │        │                  │
│ 输出: 测试报告    │             │  /dev/rpmsg0 ←→ FreeRTOS │
└──────────────────┘             └──────────────────────────┘
```

### 9.2 编译和部署测试程序

```bash
cd /home/alientek/Phytium/tests
make deploy
```

这会自动：
1. 用 `aarch64-none-linux-gnu-gcc` 交叉编译所有 `test_*.c` → `build/`
2. 通过 `sshpass` + `scp` 部署到 `192.168.88.10:/home/user/demo/tests/`
3. 自动设置可执行权限

### 9.3 运行测试

```bash
# 跑全部 5 项自动化测试 + 生成报告
make run-all

# 或只跑单项
make run-link      # TC01: RPMsg PING (5次, 20s)
make run-fault     # TC02: 故障注入全覆盖 (3节点×5类型×3等级, 60s)
make run-cmd       # TC03: 命令传输监听 (20s)
make run-encrypt   # TC04: 混沌加解密验证 (10s, 本地计算, 无需RPMsg)
make run-stress    # TC05: 压力测试 (5s高速连续注入, 15s)
```

### 9.4 测试报告

每次运行后自动生成 Markdown 报告：
```
docs/test_report_YYYYMMDD_HHMMSS.md
```

报告包含：测试概要、通过率、每项明细、完整日志、环境信息。

### 9.5 5 项测试明细

| 编号 | 名称 | 内容 | 通过标准 |
|:---:|------|------|------|
| TC01 | RPMsg Link PING | 双向 PING (5次) | 5/5 PONG, RTT<100ms |
| TC02 | Fault Injection | 所有节点×所有故障类型组合 | 每次返回 FAULT_SENT |
| TC03 | Command TX | 监听 FreeRTOS→Linux 命令 | 至少收到 1 条 CMD |
| TC04 | Chaos Encrypt | 本地加解密往返验证 | 多种长度全部一致 |
| TC05 | Stress Test | 5秒高速连续注入 | ACK率≥70%, >10 faults/s |

---

## 步骤 10: 交互式测试面板

如果不想跑全自动测试，可以用交互式面板手动操作：

```bash
make run-panel
```

进入菜单：
```
┌──────────────────────────────────────────────────────┐
│  1. PING测试         验证RPMsg链路往返                │
│  2. 单次故障注入      注入指定节点/类型故障帧          │
│  3. 连续故障生成      持续向FreeRTOS发送仿真故障数据   │
│  4. 停止测试          停止连续模式                     │
│  5. Flash状态查询     查询节点Flash保存统计            │
│  6. 混沌加密验证      测试encrypt/decrypt往返一致性    │
│  7. 连续收包监控      观察FreeRTOS侧生成的所有命令      │
│  a. 自动化测试套件    运行全部自动化测试               │
│  q. 退出                                              │
└──────────────────────────────────────────────────────┘
```

---

## 故障排查

常见问题及解决方法参见 [debug-log.md](debug-log.md)。
