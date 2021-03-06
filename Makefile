.PHONY: imv clean check install uninstall

PREFIX ?= /usr
BINPREFIX ?= $(PREFIX)/bin
MANPREFIX ?= $(PREFIX)/share/man
DATAPREFIX ?= $(PREFIX)/share

CFLAGS ?= -W -Wall -pedantic -Wmissing-prototypes
CFLAGS += -std=c99
CPPFLAGS += $(shell sdl2-config --cflags) -D_XOPEN_SOURCE=700
LIBS := $(shell sdl2-config --libs) -lfreeimage -lSDL2_ttf -lfontconfig -lpthread

BUILDDIR ?= build
TARGET := $(BUILDDIR)/imv

SOURCES := $(wildcard src/*.c)
OBJECTS := $(patsubst src/%.c,$(BUILDDIR)/%.o,$(SOURCES))
TESTS := $(patsubst test/%.c,$(BUILDDIR)/test_%,$(wildcard test/*.c))
TFLAGS ?= -g $(CFLAGS) $(CPPFLAGS) $(shell pkg-config --cflags cmocka)
TLIBS := $(LIBS) $(shell pkg-config --libs cmocka)

VERSION != git describe --dirty --always --tags 2> /dev/null || echo v2.1.3

CFLAGS += -DIMV_VERSION=\""$(VERSION)"\"

imv: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) -o $@ $^ $(LIBS) $(LDFLAGS)

debug: CFLAGS += -DDEBUG -g -pg
debug: $(TARGET)

$(OBJECTS): | $(BUILDDIR)

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(BUILDDIR)/%.o: src/%.c
	$(CC) -c $(CFLAGS) $(CPPFLAGS) -o $@ $<

$(BUILDDIR)/test_%: test/%.c src/%.c
	$(CC) -o $@ -Isrc $(TFLAGS) $^ $(LDFLAGS) $(TLIBS)

check: $(BUILDDIR) $(TESTS)
	for t in $(TESTS); do $$t; done

clean:
	$(RM) -Rf $(BUILDDIR)

install: $(TARGET)
	install -D -m 0755 $(TARGET) $(DESTDIR)$(BINPREFIX)/imv
	install -D -m 0644 doc/imv.1 $(DESTDIR)$(MANPREFIX)/man1/imv.1
	install -D -m 0644 files/imv.desktop $(DESTDIR)$(DATAPREFIX)/applications/imv.desktop

uninstall:
	$(RM) $(DESTDIR)$(BINPREFIX)/imv
	$(RM) $(DESTDIR)$(MANPREFIX)/man1/imv.1
	$(RM) $(DESTDIR)$(DATAPREFIX)/applications/imv.desktop
