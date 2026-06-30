# Makefile for IEEE 9-Bus UKF State Estimation (C implementation)
# Requires: LAPACK + BLAS (liblapack, libblas)

CC      = gcc
CFLAGS  = -O2 -Wall -march=native
LDFLAGS = -lm

# On macOS with Homebrew, use:
# LDFLAGS = -L/opt/homebrew/opt/lapack/lib -llapack -lblas -lm
#
# On Ubuntu/Debian, install: sudo apt install liblapack-dev libblas-dev
# On Windows (MSYS2), install: pacman -S mingw-w64-x86_64-lapack

ALL = terminal_node controller

.PHONY: all clean run

all: $(ALL)

terminal_node: terminal_node.c ukf_core.h
	$(CC) $(CFLAGS) -o $@ terminal_node.c $(LDFLAGS)

controller: controller.c ukf_core.h
	$(CC) $(CFLAGS) -o $@ controller.c $(LDFLAGS)

# Run both (terminal first, then controller)
run: all
	@echo "=== Step 1: Terminal Node ==="
	./terminal_node
	@echo ""
	@echo "=== Step 2: Controller ==="
	./controller
	@echo ""
	@echo "=== Done. Output files: ==="
	@ls -lh system_params.bin measurements.txt true_states.csv ukf_est.csv ukf_rmse.csv ukf_compare.csv 2>/dev/null

clean:
	rm -f terminal_node controller terminal_node.exe controller.exe
	rm -f system_params.bin measurements.txt true_states.csv
	rm -f ukf_est.csv ukf_rmse.csv ukf_compare.csv
