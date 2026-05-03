# 模块设计文档

## 1. Linux应用层模块 (src/linux-app/)

### 1.1 LED控制模块 (led_control.c/h)

**功能**: 控制开发板上的LED灯，提供开关和闪烁功能

**接口函数**:
- `int led_init(const char* led_name)` - 初始化LED设备
- `int led_set_status(int led_fd, LedStatus status)` - 设置LED状态（ON/OFF）
- `int led_blink(int led_fd, int interval_ms, int count)` - LED闪烁
- `void led_cleanup(int led_fd)` - 清理资源

**依赖**: Linux sysfs GPIO接口

**配置**: config.json中的`led`字段

---

### 1.2 主程序 (main.c)

**功能**: 程序入口，命令行参数解析，信号处理

**使用方法**:
```bash
./iot-main <led_name> [--on|--off|--blink] [--interval ms] [--count n]
```

**特性**:
- 支持Ctrl+C优雅退出
- 自动清理GPIO资源
- 详细的日志输出

---

## 2. 公共工具模块 (src/common/)

### 待实现:
- 日志系统 (logger.c)
- 配置解析 (config_parser.c)
- 工具函数 (utils.c)

---

## 3. 驱动模块 (src/drivers/)

### 待实现:
- ADC驱动 (adc_driver.c) - 电压/电流采集
- LoRa驱动 (lora_driver.c) - LoRa通信
- GPIO驱动 (gpio_driver.c) - 通用GPIO控制

---

## 4. 通信协议设计

### 数据帧格式（待实现）

#### 正常模式帧 (NORMAL)
```
[0xAA][0x55][LEN][NODE_ID][TYPE:0x01][DATA...][CRC]
```

#### 预警模式帧 (WARNING)
```
[0xAA][0x55][LEN][NODE_ID][TYPE:0x02][TIMESTAMP][DATA...][CRC]
```

#### 紧急模式帧 (DANGER)
```
[0xAA][0x55][LEN][NODE_ID][TYPE:0x03][TIMESTAMP][PRIORITY][DATA...][CRC]
```

---

## 5. 状态机设计（终端节点）

```
           ┌─────────────┐
           │   INIT      │
           └──────┬──────┘
                  │
           ┌──────▼──────┐
     ┌────►│   NORMAL    │◄────┐
     │     └──────┬──────┘     │
     │            │ 阈值超限   │
     │     ┌──────▼──────┐     │
     │     │  WARNING    │─────┘
     │     └──────┬──────┘
     │            │ 持续超限/临界值
     │     ┌──────▼──────┐
     │     │   DANGER    │
     │     └──────┬──────┘
     │            │ 恢复正常60s
     └────────────┘
```

---

## 6. 数据流图

```
传感器 → ADC采集 → 数据滤波 → 异常判断 → 模式选择 → 帧打包 → LoRa发送
                                    ↓
                              ┌─────┴─────┐
                              ↓           ↓
                         正常(定时)   异常(立即)
                              ↓           ↓
                        5-10min上传  1s/100ms上传
```

---

## 7. 文件命名规范

| 类型 | 命名规则 | 示例 |
|------|---------|------|
| 源文件 | module_name.c | led_control.c |
| 头文件 | module_name.h | led_control.h |
| 配置文件 | config.json | config.json |
| 脚本 | action.sh | deploy.sh |
| 文档 | purpose.md | README.md |

---

## 8. 编码规范

- C标准: C11
- 缩进: 4空格
- 命名风格: snake_case
- 注释: 关键逻辑必须注释
- 错误处理: 所有系统调用检查返回值
- 内存管理: 避免内存泄漏，及时释放资源

---

**文档版本**: v1.0  
**最后更新**: 2026-05-03
