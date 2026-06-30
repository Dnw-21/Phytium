#!/bin/bash

BOARD_IP="192.168.88.11"
BOARD_USER="user"
BOARD_PASS="user"
REMOTE_DIR="/home/user/iot-monitoring-system"
LOCAL_DIR="$(cd "$(dirname "$0")/.." && pwd)"

build_project() {
    echo "=== Building project ==="
    mkdir -p build && cd build
    cmake .. && make -j$(nproc)
    if [ $? -eq 0 ]; then
        echo "✓ Build successful"
        cd ..
    else
        echo "✗ Build failed"
        exit 1
    fi
}

deploy_to_board() {
    echo "=== Deploying to board (${BOARD_IP}) ==="
    
    ssh ${BOARD_USER}@${BOARD_IP} "mkdir -p ${REMOTE_DIR}/build ${REMOTE_DIR}/src/linux-app ${REMOTE_DIR}/config"
    
    rsync -avz --progress \
        src/ \
        ${BOARD_USER}@${BOARD_IP}:${REMOTE_DIR}/src/
    
    rsync -avz --progress \
        Makefile \
        config/config.json \
        ${BOARD_USER}@${BOARD_IP}:${REMOTE_DIR}/
    
    if [ $? -eq 0 ]; then
        echo "✓ Deployment successful (source only, no docs)"
    else
        echo "✗ Deployment failed"
        exit 1
    fi
}

run_on_board() {
    local cmd=$1
    echo "=== Running on board: ${cmd} ==="
    ssh -t ${BOARD_USER}@${BOARD_IP} "cd ${REMOTE_DIR} && ${cmd}"
}

debug_on_board() {
    echo "=== Debugging on board ==="
    ssh -t ${BOARD_USER}@${BOARD_IP} "cd ${REMOTE_DIR} && gdb ./build/iot-main"
}

show_help() {
    echo "Usage: $0 <command>"
    echo ""
    echo "Commands:"
    echo "  build          Build the project locally (x86_64)"
    echo "  deploy         Deploy source code to FeitengPi"
    echo "  compile        Compile ON the FeitengPi board"
    echo "  run <args>     Run program on board"
    echo "  test           Quick LED blink test on board"
    echo "  debug          Start GDB debugging session on board"
    echo "  full           Deploy + Compile + Test LED demo"
    echo "  shell          Open SSH shell to board"
    echo "  info           Show board info and compiler version"
    echo ""
    echo "Board IP: ${BOARD_IP}"
    echo "Remote dir: ${REMOTE_DIR}"
}

case "$1" in
    build)
        build_project
        ;;
    deploy)
        deploy_to_board
        ;;
    compile)
        run_on_board "make clean && make"
        ;;
    run)
        shift
        run_on_board "./build/iot-main $*"
        ;;
    test)
        deploy_to_board
        run_on_board "make && timeout 8 ./build/iot-main sysled --blink --interval 500 --count 5"
        ;;
    debug)
        debug_on_board
        ;;
    full)
        deploy_to_board
        echo "Compiling on board..."
        run_on_board "make"
        echo "Running LED demo..."
        run_on_board "timeout 8 ./build/iot-main sysled --blink --interval 500 --count 5"
        ;;
    shell)
        ssh ${BOARD_USER}@${BOARD_IP}
        ;;
    info)
        ssh ${BOARD_USER}@${BOARD_IP} 'echo "=== Board Info ===" && uname -a && echo "" && echo "=== Compiler ===" && gcc --version | head -2 && echo "" && echo "=== LED Device ===" && ls /sys/class/leds/ && echo "" && echo "=== Project Files ===" && ls -la ~/iot-monitoring-system/'
        ;;
    *)
        show_help
        ;;
esac
