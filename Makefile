CC = gcc
CFLAGS = -Wall -Wextra -g -O2
TARGET = iot-main
SRCDIR = src/linux-app
COMMONDIR = src/common
BUILDDIR = build

SOURCES = $(wildcard $(SRCDIR)/*.c) $(wildcard $(COMMONDIR)/*.c)
OBJECTS = $(patsubst %.c, $(BUILDDIR)/%.o, $(notdir $(SOURCES)))
INCLUDES = -I$(SRCDIR) -I$(COMMONDIR)

.PHONY: all clean run

all: $(BUILDDIR)/$(TARGET)

$(BUILDDIR)/$(TARGET): $(OBJECTS)
	@mkdir -p $(BUILDDIR)
	$(CC) $(CFLAGS) $(OBJECTS) -o $@ -lm -lpthread -lrt
	@echo "✓ Build successful: $@"

$(BUILDDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(BUILDDIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(BUILDDIR)/%.o: $(COMMONDIR)/*.c
	@mkdir -p $(BUILDDIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o @

clean:
	rm -rf $(BUILDDIR)/*
	@echo "✓ Cleaned"

run: $(BUILDDIR)/$(TARGET)
	./$(BUILDDIR)/$(TARGET) --once

test: $(BUILDDIR)/$(TARGET)
	./$(BUILDDIR)/$(TARGET) --monitor --count 5 --interval 2

debug: $(BUILDDIR)/$(TARGET)
	gdb ./$(BUILDDIR)/$(TARGET)
