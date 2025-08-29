# Makefile for Pamir AI E-Ink Examples
# Copyright (C) 2025 Pamir AI

# Allow CC to be overridden for cross-compilation
CC ?= gcc
CFLAGS = -Wall -O2 -I..
LDFLAGS = -lm

# For standalone build in /opt/distiller-eink-tests
ifdef STANDALONE
CFLAGS = -Wall -O2 -I.
endif

# List of C examples
C_EXAMPLES = eink_demo eink_clock eink_monitor

# Default target
all: $(C_EXAMPLES)

# Individual targets
eink_demo: eink_demo.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

eink_clock: eink_clock.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

eink_monitor: eink_monitor.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

# Clean target
clean:
	rm -f $(C_EXAMPLES)

# Install examples to /usr/local/bin
install: $(C_EXAMPLES)
	install -d /usr/local/bin
	install -m 755 $(C_EXAMPLES) /usr/local/bin/

# Uninstall examples
uninstall:
	for prog in $(C_EXAMPLES); do \
		rm -f /usr/local/bin/$$prog; \
	done

.PHONY: all clean install uninstall