# Claude 调试记录

> 历史 UART3 调试记录（2026-05-20）。当前 LoRa 实际接线和主控链路以 UART2 / J1 Pin8/Pin10 / `0x2800E000` 为准，FreeRTOS 实际运行在 CPU1（设备树/remoteproc 仍写 CPU3）。本文件只保留早期排错过程，不再作为当前接线或实现口径。
> 与另一AI协作，通过 `/home/alientek/Phytium/docs/调试.md` 交换进展

## 当前进展 (2026-05-20)

### 已回答的用户问题

**Q: UART是Linux管还是FreeRTOS管？**
A: **两者都在管！** Linux设备树`uart@2800f000 status="okay"`，Linux已注册pl011驱动(`2800f000.uart: ttyAMA3 at MMIO 0x2800f000`)。同时FreeRTOS也在直接操作UART3寄存器。双方冲突。

**Q: FreeRTOS能否有效驱动UART？**
A: **UART寄存器层面可以读写** (UART_CR=0x0301确认是我们写入的配置)。但Linux同时也在操作同一硬件。

**Q: 能否用USB-TTL验证？**
A: **历史上部署过 UART3 TX 测试**。当时代码每2秒发送`UART3_TX_TEST:<counter>\r\n`到按 UART3 口径记录的 J1 Pin8；当前接线口径已纠正为 UART2。

### 历史 UART3 TX 测试方法（当前不再按此口径执行）

```
接线: USB-TTL RXD → J1 Pin8 (UART3_TXD)
      USB-TTL GND → J1 Pin6 (GND)
PC端: 串口工具 115200-8N1
预期: 每2秒看到 "UART3_TX_TEST:0" "UART3_TX_TEST:1" ...
```

### 心跳诊断最新结果

- UART_FR=0x197: RXFE=1(FIFO空)
- UART_CR=0x0301: 配置正确(UARTEN+RXE+TXE)
- **UART_RIS=0x00000020 (TXRIS bit=1)**: TX中断触发，因为正在发TX测试数据 → **UART3 TX硬件通路正常！**
- UART_MIS=0x00: 未使能TX中断所以masked状态为0 (正常)
- ISR count=0: ISR仍未触发 (没有收到数据所以RX ISR不触发，正常)
- GPIO3_DDR=0x0980: MD0(bit1)仍是INPUT — 写入失败未解决
- GPIO3_DR=0x0002: MD0浮空高电平

### 关键待解决问题

1. **历史 UART3 占用问题** — 当时 `/dev/ttyAMA3` 存在并怀疑 Linux/FreeRTOS 同时访问；当前 LoRa 已改以 UART2 为准。
2. **GPIO3_DDR写不进去** — 尝试过偏移0x04(fgpio_hw.h)和0x400，加DSB屏障。都不行。可能原因: GPIO控制器时钟未使能、IOMUX未配置、或写保护。
3. **UART3 RX中断** — ISR count=0，但可能是因为Linux同时处理了RX数据。

### 对另一AI的协作请求

1. Linux UART3解绑问题：能否用其他方式解除Linux对UART3的占用？是否必须修改设备树重编dtb？
2. GPIO3 DDR写入：你那边查到PE2204 GPIO时钟使能或IOMUX配置的方法了吗？
3. **USB-TTL测试前务必断开LoRa模块**(至少断开TXD)，否则TX测试数据会发到LoRa被当成AT命令。

### 固件改动记录

1. `lora_uart.c`: 修复GPIO偏移为DR=0x00/DDR=0x04/EXT=0x08，添加DSB内存屏障，添加`lora_uart_send_str()`函数
2. `master_recv.c`: 历史上添加48字节诊断心跳包(GPIO寄存器值)，并添加每2秒 UART3 TX 测试字符串（当前不再作为接线口径）
3. `master_receiver.c` (Linux侧): 接收并显示DEVICE_LORA_DATA的hex数据

### 编译部署命令

```bash
export AARCH64_CROSS_PATH="/home/alientek/Phytium_syscode/GCC编译器/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-elf"
cd /home/alientek/Phytium_syscode/phytium-free-rtos-sdk-master/example/system/amp/openamp_for_linux
make all -j$(nproc)
# 部署:
scp *.elf user@192.168.88.11:/tmp/openamp_core0.elf
# 然后在开发板上: stop core → cp firmware → start core → bind rpmsg
```
