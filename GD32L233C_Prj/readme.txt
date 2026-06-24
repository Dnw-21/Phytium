=======================================
GD32L233C_Prj 项目说明文档
=======================================

项目简介
--------
本项目是基于GD32L233C微控制器的嵌入式应用开发工程，使用Keil MDK作为开发环境，集成了FreeRTOS实时操作系统。

项目结构
--------

├── FreeRTOS/           # FreeRTOS实时操作系统相关文件
│   ├── config/         # FreeRTOS配置文件
│   ├── include/        # FreeRTOS头文件
│   ├── portable/       # 平台移植层（内存管理、CPU端口）
│   ├── tasks.c         # 任务管理核心
│   ├── queue.c         # 队列管理
│   ├── list.c          # 链表实现
│   ├── timers.c        # 软件定时器
│   ├── event_groups.c  # 事件组
│   ├── croutine.c      # 协程支持
│   └── stream_buffer.c # 流缓冲区

├── Firmware/           # GD32固件库
│   ├── GD32L23x_standard_peripheral/  # 标准外设驱动库
│   │   └── Source/     # 外设驱动源文件（ADC、UART、I2C、SPI等）
│   └── GD32L23x_usbd_library/         # USB设备库

├── app/                # 用户应用程序
│   ├── main.c          # 主程序入口
│   └── main.h          # 主程序头文件

├── gd32_sys/           # GD32系统级代码
│   ├── gd32l23x_it.c   # 中断服务程序
│   ├── gd32l23x_it.h   # 中断服务头文件
│   ├── gd32l23x_libopt.h # 库配置选项
│   ├── systick.c       # SysTick定时器配置
│   └── systick.h       # SysTick头文件

├── Utilities/          # 评估板工具函数
│   ├── gd32l233r_eval.c # 评估板外设驱动
│   └── gd32l233r_eval.h # 评估板头文件

├── RTE/                # RTE组件配置（Keil MDK自动生成）
│   └── _Gd32L233C_START/
│       └── RTE_Components.h # 组件配置头文件

├── Objects/            # 编译输出目录
│   ├── *.o             # 目标文件
│   ├── *.d             # 依赖文件
│   ├── *.axf           # 可执行文件
│   └── *.hex           # 烧录文件

└── Listings/           # 列表文件目录
    └── *.map           # 链接映射文件

关键配置文件
------------

1. FreeRTOS/config/FreeRTOSConfig.h
   - FreeRTOS内核裁剪配置
   - 任务优先级、栈大小、堆大小配置
   - 功能模块开关（互斥锁、信号量、定时器等）
   - 中断优先级配置

2. gd32_sys/gd32l23x_libopt.h
   - GD32外设库功能裁剪配置

3. GD32L233C_Prj.uvprojx
   - Keil MDK项目文件

开发环境
--------
- 开发工具：Keil MDK-ARM
- 目标芯片：GD32L233C (Cortex-M23)
- RTOS：FreeRTOS V202112.00

编译说明
--------
1. 使用Keil MDK打开GD32L233C_Prj.uvprojx
2. 选择目标配置（Gd32L233C_START）
3. 点击Build按钮编译项目
4. 编译成功后生成Objects/GD32L233C_Prj.hex可烧录文件

FreeRTOS功能配置
----------------
当前配置已启用：
- 抢占式调度 + 时间片轮转
- 互斥锁、递归互斥锁、计数信号量
- 任务通知机制
- 软件定时器
- Tickless低功耗模式
- 堆栈溢出检测

当前配置已禁用：
- MPU内存保护单元
- FPU浮点单元
- TrustZone安全扩展
- 协程(Co-routines)
- 队列集
- 静态内存分配

=======================================
End of File
=======================================