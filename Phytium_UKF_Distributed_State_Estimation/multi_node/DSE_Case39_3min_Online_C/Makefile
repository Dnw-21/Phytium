CC      = gcc
CFLAGS  = -Wall -O3 -std=c99
LDFLAGS = -lm
TARGET  = controller_online_39bus

.PHONY: all build clean

all: build

build: $(TARGET)

$(TARGET): controller_online_39bus.c ukf_core_5.h
	$(CC) $(CFLAGS) -o $@ controller_online_39bus.c $(LDFLAGS)
	@echo "Build OK: $(TARGET)"

clean:
	rm -f $(TARGET) *.o
