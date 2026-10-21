CC ?= cc
CPPFLAGS ?=
CFLAGS ?= -O2 -std=gnu11 -Wall -Wextra -Wpedantic -Wformat=2 -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wundef
LDFLAGS ?=
LDLIBS ?= -lX11 -lm -lcurl
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin

TARGET := amd-gpu-diagnostic
SRC := src/amd-gpu-diagnostic.c src/gpu_specs.c

.PHONY: all clean check install uninstall

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS) $(LDLIBS)

check:
	$(CC) $(CPPFLAGS) $(CFLAGS) -Werror -o .check-$(TARGET) $(SRC) $(LDFLAGS) $(LDLIBS)
	./.check-$(TARGET) --version | grep -qx '0.6.0'
	./.check-$(TARGET) --help >/dev/null
	rm -f .check-$(TARGET)

install: $(TARGET)
	install -Dm755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)

clean:
	rm -f $(TARGET) .check-$(TARGET)
