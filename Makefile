CC ?= cc
CPPFLAGS += $(shell pkg-config --cflags libsystemd)
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow
LDLIBS += $(shell pkg-config --libs libsystemd)

.PHONY: all test test-widget clean
all: build/nuphy-backlight

build:
	mkdir -p $@

build/nuphy-backlight: src/nuphy-backlight.c src/protocol.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ src/nuphy-backlight.c $(LDFLAGS) $(LDLIBS)

build/test-protocol: tests/test-protocol.c src/protocol.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/test-protocol.c $(LDFLAGS)

build/test-transport: tests/test-transport.c src/nuphy-backlight.c src/protocol.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/test-transport.c $(LDFLAGS) $(LDLIBS)

test: build/test-protocol build/test-transport build/nuphy-backlight
	./build/test-protocol
	./build/test-transport
	./build/nuphy-backlight --help

build/mock-control: tests/mock-control.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/mock-control.c $(LDFLAGS) $(LDLIBS)

test-widget: build/mock-control
	dbus-run-session ./tests/test-widget.sh

clean:
	rm -f build/nuphy-backlight build/test-protocol build/test-transport build/mock-control
