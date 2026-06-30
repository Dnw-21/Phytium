# Makefile — UKF Controller (C, online streaming mode)
# ============================================================
# make          — full pipeline (prepare → build → run → plot)
# make build    — compile only
# make run      — run estimation + plot
# make clean    — remove built files
# ============================================================

CC      = gcc
CFLAGS  = -Wall -O2 -std=c99
LDFLAGS = -lm
TARGET  = controller_online

.PHONY: all prepare build run plot clean

all: prepare build run

prepare:
	@echo "=== Converting Python system_params.mat → system_params.bin ==="
	python convert_params.py

build: $(TARGET)

$(TARGET): controller_online.c ukf_core.h
	$(CC) $(CFLAGS) -o $@ controller_online.c $(LDFLAGS)
	@echo "Build OK: $(TARGET)"

run: $(TARGET)
	@echo "=== Running UKF (streaming mode) ==="
	./$(TARGET) measurements.txt | python plot_online.py

plot:
	./$(TARGET) measurements.txt > output/estimates.csv
	python plot_online.py < output/estimates.csv

clean:
	rm -f $(TARGET) $(TARGET).exe *.o
	rm -rf output
