CROSS_COMPILE ?= /home/alientek/Phytium_syscode/GCC编译器/gcc-arm-10.2-2020.11-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-
CC     = $(CROSS_COMPILE)gcc
CFLAGS = -Wall -Wextra -O2 -std=c11
BUILD  = build
SRC    = test_modules

PROGS = test_panel test_rpmsg_link test_fault_inject test_command test_encrypt test_stress

all: build_dir $(PROGS)
	@echo ""
	@echo "===== 测试套件编译完成 ====="
	@echo "  文件: $(BUILD)/*"
	@echo "  部署: make deploy"
	@echo "  运行: make run-{panel|link|fault|cmd|encrypt|stress|all}"

build_dir:
	mkdir -p $(BUILD)

test_panel: $(SRC)/test_panel.c
	$(CC) $(CFLAGS) $< -o $(BUILD)/$@
	@echo "[BUILD] test_panel (交互式测试面板)"

test_rpmsg_link: $(SRC)/test_rpmsg_link.c
	$(CC) $(CFLAGS) $< -o $(BUILD)/$@
	@echo "[BUILD] test_rpmsg_link"

test_fault_inject: $(SRC)/test_fault_inject.c
	$(CC) $(CFLAGS) $< -o $(BUILD)/$@
	@echo "[BUILD] test_fault_inject"

test_command: $(SRC)/test_command.c
	$(CC) $(CFLAGS) $< -o $(BUILD)/$@
	@echo "[BUILD] test_command"

test_encrypt: $(SRC)/test_encrypt.c
	$(CC) $(CFLAGS) $< -o $(BUILD)/$@ -lm
	@echo "[BUILD] test_encrypt"

test_stress: $(SRC)/test_stress.c
	$(CC) $(CFLAGS) $< -o $(BUILD)/$@
	@echo "[BUILD] test_stress"

deploy: all
	@echo "[DEPLOY] 部署到 192.168.88.11..."
	sshpass -p 'user' ssh -o StrictHostKeyChecking=no user@192.168.88.11 'mkdir -p /home/user/demo/tests' 2>/dev/null || true
	sshpass -p 'user' scp -o StrictHostKeyChecking=no $(BUILD)/* user@192.168.88.11:/home/user/demo/tests/
	sshpass -p 'user' scp -o StrictHostKeyChecking=no test_runner.sh user@192.168.88.11:/home/user/demo/run_tests.sh
	sshpass -p 'user' ssh -o StrictHostKeyChecking=no user@192.168.88.11 'chmod +x /home/user/demo/run_tests.sh'
	@echo "[DEPLOY] 完成"

run-panel:
	sshpass -p 'user' ssh -t -o StrictHostKeyChecking=no user@192.168.88.11 '/home/user/demo/tests/test_panel'

run-all:
	sshpass -p 'user' ssh -t -o StrictHostKeyChecking=no user@192.168.88.11 '/home/user/demo/run_tests.sh --all'

run-link:
	sshpass -p 'user' ssh -t -o StrictHostKeyChecking=no user@192.168.88.11 '/home/user/demo/tests/test_rpmsg_link 20'

run-fault:
	sshpass -p 'user' ssh -t -o StrictHostKeyChecking=no user@192.168.88.11 'timeout 30 /home/user/demo/tests/test_fault_inject'

run-cmd:
	sshpass -p 'user' ssh -t -o StrictHostKeyChecking=no user@192.168.88.11 'timeout 15 /home/user/demo/tests/test_command'

run-encrypt:
	sshpass -p 'user' ssh -t -o StrictHostKeyChecking=no user@192.168.88.11 '/home/user/demo/tests/test_encrypt'

run-stress:
	sshpass -p 'user' ssh -t -o StrictHostKeyChecking=no user@192.168.88.11 'timeout 30 /home/user/demo/tests/test_stress 10'

clean:
	rm -rf $(BUILD)
	@echo "[CLEAN] 测试构建目录已清除"

.PHONY: all clean deploy build_dir run-all run-panel run-link run-fault run-cmd run-encrypt run-stress