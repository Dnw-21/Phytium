# ============================================================
# Makefile — Linux-side ARM64 二进制编译 (for Phytium PE2204)
# ============================================================
# 用法:
#   make all           - 编译所有 ARM64 二进制 (含在线 UKF 三节点)
#   make deploy HOST=x  - 编译并部署到开发板
#   make clean          - 清理
# ============================================================

CROSS ?= aarch64-linux-gnu-
CC   := $(CROSS)gcc

CFLAGS := -O2 -Wall -static
LDFLAGS := -lm

# FT 优化库路径 (可被环境变量覆盖, 默认本地路径)
FC_LIB_DIR ?= /home/alientek/Phytium/fc_lib
FT_CFLAGS  := -I$(FC_LIB_DIR)/BLAS-FT_v1.5.0/include \
              -I$(FC_LIB_DIR)/LAPACK-FT_v1.4.0/include
FT_LDFLAGS := -L$(FC_LIB_DIR)/BLAS-FT_v1.5.0/lib \
              -L$(FC_LIB_DIR)/LAPACK-FT_v1.4.0/lib \
              -lblas_ft -llapack -lm

# 在线 UKF Pipeline (三节点专用)
# 体积对比:
#   5bus:  ~20KB   (ns=4,  nm=14)
#   39bus: ~120KB  (ns=20, nm=98)
#   9bus:  ~30KB   (ns=6,  nm=24)

PIPELINE_SRC := ukf_pipeline_online.c
PIPELINE_HDR := ukf_online_core.h

# 仿真控制节点 (RPMsg 通信)
SIM_SRC := start_sim_nodes.c

# SHM 重置工具
RSHM_SRC := reset_shm.c

.PHONY: all clean deploy deploy-scripts

all: ukf_pipeline_5bus ukf_pipeline_39bus ukf_pipeline_9bus \
     ukf_pipeline_5bus_ft ukf_pipeline_39bus_ft ukf_pipeline_9bus_ft \
     start_sim_nodes_arm64 reset_shm_arm64

# ─── 在线 UKF Pipeline (三节点专用) ───
ukf_pipeline_5bus: $(PIPELINE_SRC) $(PIPELINE_HDR) sim_params_5bus.h
	$(CC) $(CFLAGS) -DUKF_NODE_5BUS -o $@ $(PIPELINE_SRC) $(LDFLAGS)
	@echo "  [OK] $@ ($(shell du -h $@ | cut -f1))"
	@file $@ | grep -o 'ARM aarch64.*'

ukf_pipeline_39bus: $(PIPELINE_SRC) $(PIPELINE_HDR) sim_params_39bus.h
	$(CC) $(CFLAGS) -DUKF_NODE_39BUS -o $@ $(PIPELINE_SRC) $(LDFLAGS)
	@echo "  [OK] $@ ($(shell du -h $@ | cut -f1))"
	@file $@ | grep -o 'ARM aarch64.*'

ukf_pipeline_9bus: $(PIPELINE_SRC) $(PIPELINE_HDR) sim_params_9bus.h
	$(CC) $(CFLAGS) -DUKF_NODE_9BUS -o $@ $(PIPELINE_SRC) $(LDFLAGS)
	@echo "  [OK] $@ ($(shell du -h $@ | cut -f1))"
	@file $@ | grep -o 'ARM aarch64.*'

# ─── FT 优化版本 (动态链接 BLAS-FT / LAPACK-FT) ───
ukf_pipeline_5bus_ft: $(PIPELINE_SRC) $(PIPELINE_HDR) sim_params_5bus.h
	$(CC) -O2 -Wall -DUKF_NODE_5BUS -DUKF_USE_FT $(FT_CFLAGS) -o $@ $(PIPELINE_SRC) $(FT_LDFLAGS)
	@echo "  [OK] $@ ($(shell du -h $@ | cut -f1))"
	@file $@ | grep -o 'ARM aarch64.*'

ukf_pipeline_39bus_ft: $(PIPELINE_SRC) $(PIPELINE_HDR) sim_params_39bus.h
	$(CC) -O2 -Wall -DUKF_NODE_39BUS -DUKF_USE_FT $(FT_CFLAGS) -o $@ $(PIPELINE_SRC) $(FT_LDFLAGS)
	@echo "  [OK] $@ ($(shell du -h $@ | cut -f1))"
	@file $@ | grep -o 'ARM aarch64.*'

ukf_pipeline_9bus_ft: $(PIPELINE_SRC) $(PIPELINE_HDR) sim_params_9bus.h
	$(CC) -O2 -Wall -DUKF_NODE_9BUS -DUKF_USE_FT $(FT_CFLAGS) -o $@ $(PIPELINE_SRC) $(FT_LDFLAGS)
	@echo "  [OK] $@ ($(shell du -h $@ | cut -f1))"
	@file $@ | grep -o 'ARM aarch64.*'

# ─── RPMsg 仿真控制 ───
start_sim_nodes_arm64: $(SIM_SRC)
	$(CC) $(CFLAGS) -o $@ $(SIM_SRC)
	@echo "  [OK] $@"
	@file $@ | grep -o 'ARM aarch64.*'

# ─── SHM 重置工具 ───
reset_shm_arm64: $(RSHM_SRC)
	$(CC) $(CFLAGS) -o $@ $(RSHM_SRC)
	@echo "  [OK] $@"
	@file $@ | grep -o 'ARM aarch64.*'

# ─── 部署 ───
HOST ?= user@192.168.88.10
DEPLOY_DIR ?= /home/user/Phytium_UKF_Distributed_State_Estimation/linux
SCRIPTS := ../scripts/multi_node_combo_test.sh \
           ../scripts/run_combo_suite_safe.sh \
           ../scripts/reload_firmware.sh

deploy: all
	@echo "Deploying to $(HOST):$(DEPLOY_DIR)..."
	ssh $(HOST) "mkdir -p $(DEPLOY_DIR)/bin $(DEPLOY_DIR)/scripts"
	scp ukf_pipeline_5bus $(HOST):$(DEPLOY_DIR)/bin/
	scp ukf_pipeline_39bus $(HOST):$(DEPLOY_DIR)/bin/
	scp ukf_pipeline_9bus $(HOST):$(DEPLOY_DIR)/bin/
	scp ukf_pipeline_5bus_ft $(HOST):$(DEPLOY_DIR)/bin/
	scp ukf_pipeline_39bus_ft $(HOST):$(DEPLOY_DIR)/bin/
	scp ukf_pipeline_9bus_ft $(HOST):$(DEPLOY_DIR)/bin/
	scp start_sim_nodes_arm64 $(HOST):$(DEPLOY_DIR)/bin/start_sim_nodes
	scp reset_shm_arm64 $(HOST):$(DEPLOY_DIR)/bin/reset_shm_arm64
	scp $(SCRIPTS) $(HOST):$(DEPLOY_DIR)/scripts/
	ssh $(HOST) "cd $(DEPLOY_DIR) && chmod +x bin/ukf_pipeline_5bus bin/ukf_pipeline_39bus \
	             bin/ukf_pipeline_9bus bin/ukf_pipeline_5bus_ft bin/ukf_pipeline_39bus_ft \
	             bin/ukf_pipeline_9bus_ft bin/start_sim_nodes bin/reset_shm_arm64 \
	             scripts/*.sh"
	@echo "Deploy complete. Run: cd $(DEPLOY_DIR) && sudo scripts/multi_node_combo_test.sh 0 0 0 10"

deploy-scripts:
	scp $(SCRIPTS) $(HOST):$(DEPLOY_DIR)/scripts/
	ssh $(HOST) "cd $(DEPLOY_DIR) && chmod +x scripts/*.sh"

clean:
	rm -f ukf_pipeline_5bus ukf_pipeline_39bus ukf_pipeline_9bus \
	      ukf_pipeline_5bus_ft ukf_pipeline_39bus_ft ukf_pipeline_9bus_ft \
	      start_sim_nodes_arm64 reset_shm_arm64 \
	      ukf_pipeline.arm64 ukf_pipeline
