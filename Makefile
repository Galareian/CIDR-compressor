CC      ?= gcc
CFLAGS  ?= -O2 -std=c11 -Wall -Wextra -Wpedantic
LDFLAGS ?=
LDLIBS  ?=

TARGET := bin/cidr-compressor
SOURCES := $(wildcard src/*.c)
OBJECTS := $(patsubst %.c,build/%.o,$(SOURCES))

.PHONY: all clean run help

all: $(TARGET)

$(TARGET): $(OBJECTS)
	@mkdir -p $(@D)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

build/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

-include $(OBJECTS:.o=.d)

run: $(TARGET)
	$(TARGET) $(ARGS)

clean:
	rm -rf bin build

help:
	@printf '%s\n' \
		'Available targets:' \
		'  all    Build the CIDR compressor (default)' \
		'  run    Run it; pass input with ARGS=path/to/input.txt' \
		'  clean  Remove build artifacts' \
		'  help   Show this help message'
