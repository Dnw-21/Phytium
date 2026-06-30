# Makefile for UKF eBPF Monitor
#
# 前置条件:
#   1. 安装 clang + llvm + libbpf-dev:
#      sudo apt install clang llvm libbpf-dev
#   2. 生成 vmlinux.h (从目标内核):
#      bpftool btf dump file /sys/kernel/btf/vmlinux format c > vmlinux.h
#
# 编译:
#   make                        # 本地 x86_64 编译
#   make ARCH=arm64             # ARM64 交叉编译
#
# 运行:
#   sudo ./ukf_monitor [interval_sec]

# ---- 目标架构 ----
ARCH ?= $(shell uname -m)

# ---- 工具链 ----
CC       = gcc
CLANG    = clang
LLVM_STRIP = llvm-strip

ifeq ($(ARCH), arm64)
    CLANG_TARGET = -target aarch64-linux-gnu
    CC_TARGET    = aarch64-linux-gnu-gcc
else ifeq ($(ARCH), aarch64)
    CLANG_TARGET = -target aarch64-linux-gnu
    CC_TARGET    = aarch64-linux-gnu-gcc
else
    CLANG_TARGET = -target bpf
    CC_TARGET    = gcc
endif

# ---- 编译选项 ----
BPF_CFLAGS  = $(CLANG_TARGET) -g -O2 -Wall \
              -I/usr/include/$(shell uname -m)-linux-gnu \
              -D__TARGET_ARCH_$(ARCH)

USER_CFLAGS = -O2 -Wall
USER_LIBS   = -lbpf -lelf -lz

# ---- 目标 ----
.PHONY: all clean vmlinux deploy

all: ukf_monitor.bpf.o ukf_monitor

# BPF 字节码 (内核侧)
ukf_monitor.bpf.o: ukf_monitor.bpf.c ukf_monitor.h vmlinux.h
	$(CLANG) $(BPF_CFLAGS) -c $< -o $@
	$(LLVM_STRIP) -g $@  # strip debug info to reduce size

# 用户态加载器
ukf_monitor: ukf_monitor.c ukf_monitor.h
	$(CC) $(USER_CFLAGS) -o $@ $< $(USER_LIBS)

# 生成 vmlinux.h (从当前运行内核)
vmlinux:
	bpftool btf dump file /sys/kernel/btf/vmlinux format c > vmlinux.h
	@echo "vmlinux.h generated from current kernel"

# 交叉编译 ARM64 版本
arm64:
	$(MAKE) ARCH=arm64

# 清理
clean:
	rm -f ukf_monitor.bpf.o ukf_monitor vmlinux.h

# 部署到开发板 (需要开发板在线)
deploy:
	scp ukf_monitor.bpf.o ukf_monitor user@192.168.88.10:/home/user/
	@echo "Deployed to board. Run: sudo ./ukf_monitor"