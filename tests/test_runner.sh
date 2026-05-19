#!/bin/bash
# ═══════════════════════════════════════════════════════════════
#  Phytium Pi PE2204 OpenAMP 自动化测试套件
#  总控脚本 - 通过SSH远程运行测试并生成报告
# ═══════════════════════════════════════════════════════════════

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
REPORT_DIR="${PROJECT_DIR}/docs"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
REPORT_FILE="${REPORT_DIR}/test_report_${TIMESTAMP}.md"

BOARD_IP="192.168.88.11"
BOARD_USER="user"
BOARD_PASS="user"
TEST_DIR="/home/user/demo/tests"
SSH_CMD="sshpass -p '${BOARD_PASS}' ssh -o StrictHostKeyChecking=no ${BOARD_USER}@${BOARD_IP}"

PASS_COUNT=0
FAIL_COUNT=0
declare -a TC_IDS TC_NAMES TC_RESULTS TC_DETAILS TC_OUTPUTS

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

log_info()  { echo -e "${BLUE}[INFO]${NC}  $*"; }
log_pass()  { echo -e "${GREEN}[PASS]${NC}  $*"; }
log_fail()  { echo -e "${RED}[FAIL]${NC}  $*"; }
log_title() {
    echo -e "\n${CYAN}═══════════════════════════════════════════════${NC}"
    echo -e "${CYAN}  $*${NC}"
    echo -e "${CYAN}═══════════════════════════════════════════════${NC}"
}

record() {
    local id="$1" name="$2" result="$3" detail="$4" output="$5"
    TC_IDS+=("$id")
    TC_NAMES+=("$name")
    TC_RESULTS+=("$result")
    TC_DETAILS+=("$detail")
    TC_OUTPUTS+=("$output")
    if [ "$result" = "PASS" ]; then PASS_COUNT=$((PASS_COUNT + 1))
    else FAIL_COUNT=$((FAIL_COUNT + 1)); fi
}

# ═══════════════════════════════════════════════════════════════
# 环境检查
# ═══════════════════════════════════════════════════════════════
check_env() {
    log_info "检查开发板连接..."
    if ! ping -c 1 -W 2 ${BOARD_IP} > /dev/null 2>&1; then
        log_fail "开发板 ${BOARD_IP} 不可达"
        exit 1
    fi
    log_pass "开发板可达"

    log_info "检查 remoteproc 状态..."
    local state=$(${SSH_CMD} "cat /sys/class/remoteproc/remoteproc0/state 2>/dev/null" 2>/dev/null || echo "unknown")
    log_info "remoteproc0 state: ${state}"

    if [ "$state" != "running" ]; then
        log_info "尝试启动 remoteproc0..."
        ${SSH_CMD} "echo ${BOARD_PASS} | sudo -S sh -c 'echo start > /sys/class/remoteproc/remoteproc0/state; sleep 2'" 2>/dev/null
    fi

    log_info "确保测试程序存在..."
    ${SSH_CMD} "ls ${TEST_DIR}/test_rpmsg_link > /dev/null 2>&1" 2>/dev/null || {
        log_fail "测试程序未部署，请先运行 make deploy"
        exit 1
    }
    log_pass "环境检查完成"
}

# ═══════════════════════════════════════════════════════════════
# 清理函数 — 在每次测试前释放 /dev/rpmsg0
# ═══════════════════════════════════════════════════════════════
cleanup_rpmsg() {
    sleep 2
    ${SSH_CMD} "echo ${BOARD_PASS} | sudo -S fuser -k /dev/rpmsg0 2>/dev/null; sleep 2; echo ${BOARD_PASS} | sudo -S fuser -k /dev/rpmsg0 2>/dev/null; sleep 1" 2>/dev/null
    sleep 1
}

# ═══════════════════════════════════════════════════════════════
# 运行单个测试（需要 RPMsg）
# ═══════════════════════════════════════════════════════════════
run_test_rpmsg() {
    local id="$1" name="$2" bin="$3" timeout="$4" extra="$5"
    log_title "${id}: ${name}"
    cleanup_rpmsg
    local out
    out=$(${SSH_CMD} "timeout ${timeout} ${TEST_DIR}/${bin} ${extra}" 2>&1) || true
    echo "$out"
    local detail=""
    if echo "$out" | grep -q "RESULT: PASS"; then
        local pass_line=$(echo "$out" | grep "RESULT: PASS")
        detail="${pass_line#*RESULT: PASS (}"
        detail="${detail%)}"
        [ -z "$detail" ] && detail="OK"
        record "$id" "$name" "PASS" "$detail" "$out"
        log_pass "${id} OK"
    else
        record "$id" "$name" "FAIL" "RPMsg test failed" "$out"
        log_fail "${id} FAILED"
    fi
}

# ═══════════════════════════════════════════════════════════════
# 运行本地测试（无需 RPMsg）
# ═══════════════════════════════════════════════════════════════
run_test_local() {
    local id="$1" name="$2" bin="$3"
    log_title "${id}: ${name}"
    local out
    out=$(${SSH_CMD} "timeout 10 ${TEST_DIR}/${bin}" 2>&1) || true
    echo "$out"
    if echo "$out" | grep -q "RESULT: PASS"; then
        local pass_line=$(echo "$out" | grep "RESULT: PASS")
        local detail="${pass_line#*RESULT: PASS (}"
        detail="${detail%)}"
        [ -z "$detail" ] && detail="OK"
        record "$id" "$name" "PASS" "$detail" "$out"
        log_pass "${id} OK"
    else
        record "$id" "$name" "FAIL" "Local test failed" "$out"
        log_fail "${id} FAILED"
    fi
}

# ═══════════════════════════════════════════════════════════════
# ═══════════════════════════════════════════════════════════════
#  测试用例
# ═══════════════════════════════════════════════════════════════
# ═══════════════════════════════════════════════════════════════

test_tc01() {
    run_test_rpmsg "TC01" "RPMsg Link PING Test" "test_rpmsg_link" 20 "5"
}

test_tc02() {
    run_test_rpmsg "TC02" "Fault Injection Test" "test_fault_inject" 60 ""
}

test_tc03() {
    run_test_rpmsg "TC03" "Command Transmission Test" "test_command" 20 ""
}

test_tc04() {
    run_test_local "TC04" "Chaos Encrypt/Decrypt Test" "test_encrypt"
}

test_tc05() {
    run_test_rpmsg "TC05" "Stress Test" "test_stress" 15 "5"
}

# ═══════════════════════════════════════════════════════════════
# 测试报告生成
# ═══════════════════════════════════════════════════════════════
generate_report() {
    mkdir -p "${REPORT_DIR}"

    local total=$((PASS_COUNT + FAIL_COUNT))
    local pass_rate=0
    [ $total -gt 0 ] && pass_rate=$((PASS_COUNT * 100 / total))

    local verdict icon
    if [ $FAIL_COUNT -eq 0 ]; then
        verdict="PASS — 所有测试通过" icon="✅"
    elif [ $PASS_COUNT -ge $((total * 70 / 100)) ]; then
        verdict="CONDITIONAL PASS — 部分测试失败" icon="⚠️"
    else
        verdict="FAIL — 多项测试失败" icon="❌"
    fi

    local env_info
    env_info=$(${SSH_CMD} "uname -a; cat /sys/class/remoteproc/remoteproc0/firmware 2>/dev/null; cat /sys/class/remoteproc/remoteproc0/state 2>/dev/null" 2>/dev/null || echo "N/A")

    cat > "${REPORT_FILE}" << EOF
# Phytium Pi PE2204 OpenAMP 测试报告

| 项目 | 内容 |
|------|------|
| 生成时间 | $(date '+%Y-%m-%d %H:%M:%S') |
| 测试主机 | $(hostname) |
| 目标设备 | ${BOARD_IP} |
| 测试环境 | ${env_info} |

---

## 测试概要

| 指标 | 数值 |
|------|------|
| 测试总数 | ${total} |
| 通过 | ${PASS_COUNT} |
| 失败 | ${FAIL_COUNT} |
| 通过率 | ${pass_rate}% |
| **整体评估** | **${icon} ${verdict}** |

---

## 测试明细

| ID | 测试项 | 结果 | 详情 |
|----|--------|------|------|
EOF

    for i in "${!TC_IDS[@]}"; do
        local sym="❌"
        [ "${TC_RESULTS[$i]}" = "PASS" ] && sym="✅"
        printf "| %s | %s | %s %s | %s |\n" \
            "${TC_IDS[$i]}" "${TC_NAMES[$i]}" "${sym}" "${TC_RESULTS[$i]}" "${TC_DETAILS[$i]}" \
            >> "${REPORT_FILE}"
    done

    cat >> "${REPORT_FILE}" << EOF

---

## 测试输出详情

EOF

    for i in "${!TC_IDS[@]}"; do
        cat >> "${REPORT_FILE}" << EOF
### ${TC_IDS[$i]}: ${TC_NAMES[$i]} (${TC_RESULTS[$i]})

\`\`\`
${TC_OUTPUTS[$i]}
\`\`\`

EOF
    done

    cat >> "${REPORT_FILE}" << EOF
---

## 测试环境信息

\`\`\`
${env_info}
\`\`\`

---

## 测试标准

| 标准 | 说明 |
|------|------|
| 链路连通性 | PING 成功率 = 100%，RTT < 100ms |
| 故障注入 | 所有节点 × 所有故障类型 × 所有严重等级全部通过 |
| 命令传输 | 至少收到 1 条 FreeRTOS → Linux 的命令 |
| 混沌加密 | encrypt→decrypt 往返一致，多种数据长度均通过 |
| 压力测试 | ACK 率 ≥ 70%，速率 > 10 faults/s |

---

*报告由 test_runner.sh 自动生成*
EOF

    echo ""
    echo "═══════════════════════════════════════════════"
    echo "  报告已生成: ${REPORT_FILE}"
    echo "  通过: ${PASS_COUNT}/${total}  |  失败: ${FAIL_COUNT}/${total}"
    echo "  整体评估: ${icon} ${verdict}"
    echo "═══════════════════════════════════════════════"
}

# ═══════════════════════════════════════════════════════════════
# Main
# ═══════════════════════════════════════════════════════════════
main() {
    echo "╔══════════════════════════════════════════════╗"
    echo "║  Phytium Pi PE2204 OpenAMP 自动化测试套件   ║"
    echo "║  $(date '+%Y-%m-%d %H:%M:%S')                    ║"
    echo "╚══════════════════════════════════════════════╝"
    echo ""

    check_env

    local tests="tc01 tc02 tc03 tc04 tc05"
    if [ $# -gt 0 ]; then
        tests="$*"
    fi

    for t in $tests; do
        case "$t" in
            tc01|tc1|1) test_tc01 ;;
            tc02|tc2|2) test_tc02 ;;
            tc03|tc3|3) test_tc03 ;;
            tc04|tc4|4) test_tc04 ;;
            tc05|tc5|5) test_tc05 ;;
            all) test_tc01; test_tc02; test_tc03; test_tc04; test_tc05 ;;
            *) log_info "未知测试: $t (可用: tc01-tc05, all)" ;;
        esac
    done

    echo ""
    generate_report
}

main "$@"