.PHONY: all clean master slave deploy help

# 交叉编译器路径
CROSS_COMPILE ?= /home/alientek/Phytium_syscode/GCC编译器/gcc-arm-10.2-2020.11-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-

# 编译选项
CC = $(CROSS_COMPILE)gcc
CFLAGS_MASTER = -Wall -Wextra -O2 -std=c11
CFLAGS_SLAVE = -Wall -Wextra -O2 -std=c11

# 输出目录
BUILD_DIR = build
MASTER_DIR = linux-master
SLAVE_DIR = remote-core

# 目标文件
TARGET_MASTER = $(BUILD_DIR)/rpmsg_master
TARGET_MASTER_RECV = $(BUILD_DIR)/master_receiver
TARGET_LORA_CTRL = $(BUILD_DIR)/lora_ctrl
TARGET_SLAVE = $(BUILD_DIR)/rpmsg_slave.elf

# 源文件
MASTER_SRCS = $(MASTER_DIR)/rpmsg_master.c
MASTER_RECV_SRCS = $(MASTER_DIR)/master_receiver.c
LORA_CTRL_SRCS = $(MASTER_DIR)/lora_ctrl.c
SLAVE_SRCS = $(SLAVE_DIR)/rpmsg_slave.c

# 目标文件
MASTER_OBJS = $(BUILD_DIR)/rpmsg_master.o
MASTER_RECV_OBJS = $(BUILD_DIR)/master_receiver.o
LORA_CTRL_OBJS = $(BUILD_DIR)/lora_ctrl.o
SLAVE_OBJS = $(BUILD_DIR)/rpmsg_slave.o

all: master master-recv lora-ctrl slave

master: $(BUILD_DIR) $(TARGET_MASTER)
	@echo ""
	@echo "✅ Linux Echo程序编译完成: $(TARGET_MASTER)"
	@echo ""

master-recv: $(BUILD_DIR) $(TARGET_MASTER_RECV)
	@echo ""
	@echo "✅ Master Receiver编译完成: $(TARGET_MASTER_RECV)"
	@echo ""

lora-ctrl: $(BUILD_DIR) $(TARGET_LORA_CTRL)
	@echo ""
	@echo "✅ LoRa Control编译完成: $(TARGET_LORA_CTRL)"
	@echo ""

slave: $(BUILD_DIR) $(TARGET_SLAVE)
	@echo ""
	@echo "✅ 从核固件编译完成: $(TARGET_SLAVE)"
	@echo ""

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(TARGET_MASTER): $(MASTER_OBJS)
	$(CC) $(CFLAGS_MASTER) $^ -o $@
	@echo "[BUILD] 链接主控程序"

$(MASTER_OBJS): $(MASTER_SRCS)
	$(CC) $(CFLAGS_MASTER) -c $< -o $@
	@echo "[BUILD] 编译主控程序源码"

$(TARGET_MASTER_RECV): $(MASTER_RECV_OBJS)
	$(CC) $(CFLAGS_MASTER) $^ -o $@
	@echo "[BUILD] 链接Master Receiver"

$(MASTER_RECV_OBJS): $(MASTER_RECV_SRCS)
	$(CC) $(CFLAGS_MASTER) -c $< -o $@
	@echo "[BUILD] 编译Master Receiver源码"

$(TARGET_LORA_CTRL): $(LORA_CTRL_OBJS)
	$(CC) $(CFLAGS_MASTER) $^ -o $@
	@echo "[BUILD] 链接LoRa Control"

$(LORA_CTRL_OBJS): $(LORA_CTRL_SRCS)
	$(CC) $(CFLAGS_MASTER) -c $< -o $@
	@echo "[BUILD] 编译LoRa Control源码"

$(TARGET_SLAVE): $(SLAVE_OBJS)
	$(CC) $(CFLAGS_SLAVE) $^ -o $@
	@echo "[BUILD] 链接从核固件"

$(SLAVE_OBJS): $(SLAVE_SRCS)
	$(CC) $(CFLAGS_SLAVE) -c $< -o $@
	@echo "[BUILD] 编译从核固件源码"

clean:
	rm -rf $(BUILD_DIR)
	@echo "[CLEAN] 已清理构建目录"

deploy: master
	@echo ""
	@echo "🚀 开始部署到开发板..."
	@echo ""
	sshpass -p 'user' scp $(TARGET_MASTER) user@192.168.88.10:~/openamp-demo/
	@echo "[DEPLOY] ✅ 主控程序已部署"
	@echo ""
	@echo "=========================================="
	@echo "  部署完成！请在开发板上执行以下步骤："
	@echo "=========================================="
	@echo ""
	@echo "1. 启动远程处理器："
	@echo "   echo start > /sys/class/remoteproc/remoteproc0/state"
	@echo ""
	@echo "2. 绑定通道驱动："
	@echo "   echo rpmsg_chrdev > \\"
	@echo "     /sys/bus/rpmsg/devices/virtio0.rpmsg-openamp-demo-channel.-1.0/driver_override"
	@echo ""
	@echo "3. 运行demo："
	@echo "   cd ~/openamp-demo && ./rpmsg_master"
	@echo ""
	@echo "4. 停止测试："
	@echo "   Ctrl+C 或等待自动结束"
	@echo ""
	@echo "5. 停止远程处理器："
	@echo "   echo stop > /sys/class/remoteproc/remoteproc0/state"
	@echo ""

test-local: master
	@echo ""
	@echo "⚠️  本地测试模式（需要OpenAMP环境）"
	@echo ""
	./$(TARGET_MASTER) --count 10 --interval 500

help:
	@echo ""
	@echo "╔══════════════════════════════════════════╗"
	@echo "║     OpenAMP Demo 构建系统 - 飞腾派       ║"
	@echo "╚══════════════════════════════════════════╝"
	@echo ""
	@echo "用法: make [目标]"
	@echo ""
	@echo "目标:"
	@echo "  master      - 编译Linux Echo程序"
	@echo "  master-recv - 编译Master Receiver (主控数据接收)"
	@echo "  slave       - 编译从核固件（裸机程序）"
	@echo "  all         - 编译所有目标"
	@echo "  clean       - 清理构建文件"
	@echo "  deploy      - 编译并部署到开发板"
	@echo "  test-local  - 本地测试（需要环境）"
	@echo "  help        - 显示此帮助信息"
	@echo ""
	@echo "变量:"
	@echo "  CROSS_COMPILE  - 交叉编译器前缀路径"
	@echo ""
	@echo "示例:"
	@echo "  make                  # 只编译主控程序"
	@echo "  make all             # 编译主控+从核"
	@echo "  make deploy          # 编译并部署到开发板"
	@echo "  make clean && make   # 清理后重新编译"
	@echo ""
