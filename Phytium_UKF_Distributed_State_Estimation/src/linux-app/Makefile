CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c11
LDFLAGS =

SRCDIR = .
BUILDDIR = build
TARGET = iot-main

SRCS = $(wildcard $(SRCDIR)/*.c)
OBJS = $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.o,$(SRCS))

.PHONY: all clean run test

all: $(BUILDDIR) $(BUILDDIR)/$(TARGET)

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(BUILDDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR)/$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) $^ -o $@
	@echo "[SUCCESS] Build complete: $(BUILDDIR)/$(TARGET)"

clean:
	rm -rf $(BUILDDIR)
	@echo "[INFO] Cleaned build directory"

run: $(BUILDDIR)/$(TARGET)
	./$(BUILDDIR)/$(TARGET)

test: $(BUILDDIR)/$(TARGET)
	./$(BUILDDIR)/$(TARGET)
