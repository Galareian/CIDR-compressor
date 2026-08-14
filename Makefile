CC      ?= gcc
CFLAGS  ?= -O2 -std=c11 -Wall -Wextra -Wpedantic
LDFLAGS ?=
LDLIBS  ?=

TARGET := bin/cidr-compressor
SOURCES := $(wildcard src/*.c)
OBJECTS := $(patsubst %.c,build/%.o,$(SOURCES))

# Cross-compilers must be installed separately for the ARM targets.
ARCHES := x86_64 arm64 armv7
CC_x86_64 := gcc
CC_arm64 := aarch64-linux-gnu-gcc
CC_armv7 := arm-linux-gnueabihf-gcc
PORTABLE_TARGETS := $(ARCHES:%=dist/%/cidr-compressor)

.PHONY: all build clean run test stress-test help

all: $(TARGET)

build: $(PORTABLE_TARGETS)

$(TARGET): $(OBJECTS)
	@mkdir -p $(@D)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

build/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

-include $(OBJECTS:.o=.d)

define PORTABLE_template
dist/$(1)/cidr-compressor: $(SOURCES)
	@mkdir -p $$(@D)
	$$(CC_$(1)) $$(CFLAGS) $$(LDFLAGS) -o $$@ $$^ $$(LDLIBS)
endef

$(foreach arch,$(ARCHES),$(eval $(call PORTABLE_template,$(arch))))

run: $(TARGET)
	$(TARGET) $(ARGS)

test: $(TARGET)
	./tests/test.sh $(TARGET)

stress-test: $(TARGET)
	./tests/stress.sh $(TARGET)

clean:
	rm -rf bin build dist

help:
	@printf '%s\n' \
		'Available targets:' \
		'  all    Build the CIDR compressor (default)' \
		'  build  Build x86_64, ARM64, and ARMv7 binaries under dist/' \
		'  run    Run it; pass input with ARGS=path/to/input.txt' \
		'  test   Run the test suite' \
		'  stress-test  Run million-entry IPv4, IPv6, and mixed tests' \
		'  clean  Remove build artifacts' \
		'  help   Show this help message'
