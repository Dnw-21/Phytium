#!/bin/bash
# deploy_fc_lib.sh — 将 fc_lib 飞腾优化库部署到开发板
# 用法: ./deploy_fc_lib.sh [board_ip]
# 默认: 192.168.88.10

set -e

BOARD_IP="${1:-192.168.88.10}"
BOARD_USER="user"
BOARD_DIR="/home/user/ukf"
FC_LIB="/home/alientek/Phytium/fc_lib"

echo "=== 飞腾优化库部署脚本 ==="
echo "目标: ${BOARD_USER}@${BOARD_IP}:${BOARD_DIR}"
echo ""

# 1. 创建目标目录
echo "[1/6] 创建目标目录..."
ssh ${BOARD_USER}@${BOARD_IP} "mkdir -p ${BOARD_DIR}/include ${BOARD_DIR}/lib" || {
    echo "ERROR: 无法连接开发板，请确认网络和IP"
    exit 1
}

# 2. 部署 BLAS-FT 头文件
echo "[2/6] 部署 BLAS-FT 头文件..."
scp ${FC_LIB}/BLAS-FT_v1.5.0/include/cblas.h \
    ${FC_LIB}/BLAS-FT_v1.5.0/include/blas_config.h \
    ${FC_LIB}/BLAS-FT_v1.5.0/include/f77blas.h \
    ${BOARD_USER}@${BOARD_IP}:${BOARD_DIR}/include/

# 3. 部署 BLAS-FT 库
echo "[3/6] 部署 BLAS-FT 库..."
scp ${FC_LIB}/BLAS-FT_v1.5.0/lib/libblas_ft.so \
    ${FC_LIB}/BLAS-FT_v1.5.0/lib/libblas_ft.a \
    ${BOARD_USER}@${BOARD_IP}:${BOARD_DIR}/lib/

# 4. 部署 LAPACK-FT 头文件 + 库
echo "[4/6] 部署 LAPACK-FT..."
scp ${FC_LIB}/LAPACK-FT_v1.4.0/include/lapacke.h \
    ${FC_LIB}/LAPACK-FT_v1.4.0/include/lapacke_config.h \
    ${FC_LIB}/LAPACK-FT_v1.4.0/include/lapacke_utils.h \
    ${FC_LIB}/LAPACK-FT_v1.4.0/include/lapack.h \
    ${BOARD_USER}@${BOARD_IP}:${BOARD_DIR}/include/
scp ${FC_LIB}/LAPACK-FT_v1.4.0/lib/liblapack.so \
    ${BOARD_USER}@${BOARD_IP}:${BOARD_DIR}/lib/

# 5. 部署 VML-FT 头文件 + 库
echo "[5/6] 部署 VML-FT..."
scp ${FC_LIB}/VML-FT_v1.4.0/include/vml-ft.h \
    ${BOARD_USER}@${BOARD_IP}:${BOARD_DIR}/include/
scp ${FC_LIB}/VML-FT_v1.4.0/lib/libvml-ft.so \
    ${BOARD_USER}@${BOARD_IP}:${BOARD_DIR}/lib/

# 6. 创建编译辅助脚本
echo "[6/6] 创建编译辅助脚本..."
cat << 'SCRIPT' | ssh ${BOARD_USER}@${BOARD_IP} "cat > ${BOARD_DIR}/build_ft.sh && chmod +x ${BOARD_DIR}/build_ft.sh"
#!/bin/bash
# build_ft.sh — 在开发板上编译 FT 版本 UKF 控制器
# 用法: ./build_ft.sh <5bus|9bus|39bus>
set -e

NODE="$1"
UKF_DIR="/home/user/ukf"

case "$NODE" in
    5bus)
        SRC="/home/user/build5/controller_online_5bus_ft.c"
        HDR="${UKF_DIR}/ukf_core_5_ft.h"
        OUT="controller_online_5bus_ft"
        ;;
    9bus)
        SRC="/home/user/build9/controller_online_9bus_ft.c"
        HDR="${UKF_DIR}/ukf_core_9_ft.h"
        OUT="controller_online_9bus_ft"
        ;;
    39bus)
        SRC="/home/user/build39/controller_online_39bus_ft.c"
        HDR="${UKF_DIR}/ukf_core_39_ft.h"
        OUT="controller_online_39bus_ft"
        ;;
    *)
        echo "用法: $0 <5bus|9bus|39bus>"
        exit 1
        ;;
esac

gcc -D_GNU_SOURCE -O2 -std=c99 \
    -I${UKF_DIR}/include \
    -I${UKF_DIR} \
    -o ${OUT} \
    ${SRC} \
    -L${UKF_DIR}/lib -lblas_ft -llapack -lvml-ft \
    -lm

echo "编译完成: ${OUT}"
SCRIPT

echo ""
echo "=== 部署完成 ==="
echo ""
echo "开发板上的库文件:"
echo "  头文件: ${BOARD_DIR}/include/"
echo "  库文件: ${BOARD_DIR}/lib/"
echo ""
echo "编译FT版本:"
echo "  ssh ${BOARD_USER}@${BOARD_IP} 'cd ${BOARD_DIR} && ./build_ft.sh 39bus'"