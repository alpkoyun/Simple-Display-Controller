CC ?= gcc
CFLAGS ?= -O2 -g
CPPFLAGS ?= -I/usr/include/drm
LDFLAGS ?=

WARN_CFLAGS := -Wall -Wextra -Wshadow -Wformat=2 -Wno-missing-field-initializers

.PHONY: all clean

all: kms_overlay_test

kms_overlay_test: kms_overlay_test.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN_CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f kms_overlay_test
