# Makefile for UKF Controller (C version)
# ============================================================
# Build:   make
# Run:     make run
# Clean:   make clean
# ============================================================

CC      = gcc
CFLAGS  = -Wall -O2 -std=c99 -march=native
LDFLAGS = -lm

INCDIR  = include
SRCDIR  = src
DATADIR = data
OUTDIR  = output

INCS    = -I$(INCDIR)

SRCS = $(SRCDIR)/main.c $(SRCDIR)/ukf_estimation.c \
       $(SRCDIR)/dynamic_system.c $(SRCDIR)/matrix_ops.c

OBJS = $(SRCS:.c=.o)

TARGET = ukf_controller

.PHONY: all clean run prepare plot

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) $(INCS) -c $< -o $@

# Generate system_params.txt from system_params.mat
prepare:
	python scripts/generate_c_params.py
	@echo "Copying measurements.txt and true_states.csv to data/"
	cp ../measurements.txt $(DATADIR)/ 2>/dev/null || echo "  measurements.txt not found (ok)"
	cp ../true_states.csv $(DATADIR)/ 2>/dev/null || echo "  true_states.csv not found (ok)"

# Create output directory and run
run: $(TARGET)
	mkdir -p $(OUTDIR)
	./$(TARGET) $(DATADIR)

# Plot results from C output CSVs
plot:
	mkdir -p $(OUTDIR)
	python scripts/plot_results_c.py

# Full pipeline: prepare -> run -> plot
all_run: prepare run plot
	@echo "============================================"
	@echo "Full pipeline complete!"
	@echo "Output plots in $(OUTDIR)/"
	@echo "============================================"

clean:
	rm -f $(OBJS) $(TARGET)
	rm -f $(DATADIR)/system_params.txt
	rm -f $(DATADIR)/ukf_estimation_output.csv
	rm -f $(DATADIR)/ukf_comparison_output.csv
	rm -f $(OUTDIR)/*.png
