PREFIX ?= $(HOME)/.local
BINDIR ?= $(PREFIX)/bin
LIBTAM_PREFIX ?= $(HOME)/.local
BUILD ?= build
VERSION := $(shell cat VERSION)

ifneq ($(wildcard $(LIBTAM_PREFIX)/include/tam.h),)
LIBTAM_INC ?= $(LIBTAM_PREFIX)/include
LIBTAM ?= $(LIBTAM_PREFIX)/lib/libtam.a
else
LIBTAM_INC ?= ../libtam/include
LIBTAM ?= ../libtam/build/libtam.a
endif

CPPFLAGS += -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 \
            -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 \
            -Iinclude -I$(LIBTAM_INC) \
            -DKNOX_VERSION='"$(VERSION)"'
CFLAGS ?= -O2 -g
override CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wvla

CORE_OBJECTS = $(addprefix $(BUILD)/,git.o graph.o explorer.o keymap.o syntax.o viewer.o pane.o workbench.o source.o layout.o render.o render_md.o render_html.o render_mermaid.o)
OBJECTS = $(BUILD)/main.o $(CORE_OBJECTS)

all: $(BUILD)/tam-knox

libtam-check:
	@printf '#include <tam.h>\n#if TAM_VERSION_MAJOR == 0 && TAM_VERSION_MINOR < 10\n#error libtam 0.10.0 or later required\n#endif\n#include <tam_pager.h>\n' | $(CC) $(CPPFLAGS) -x c -fsyntax-only - || { echo 'tam-knox requires libtam 0.10.0 or later' >&2; exit 1; }

$(OBJECTS): | libtam-check

$(BUILD)/tam-knox: $(OBJECTS) $(LIBTAM)
	$(CC) $(LDFLAGS) -o $@ $(OBJECTS) $(LIBTAM)

$(BUILD)/%.o: src/%.c VERSION
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c -o $@ $<

$(BUILD)/test-graph: tests/test-graph.c $(CORE_OBJECTS) $(LIBTAM)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $^

$(BUILD)/test-git: tests/test-git.c $(CORE_OBJECTS) $(LIBTAM)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $^

$(BUILD)/test-syntax-render: tests/test-syntax-render.c $(CORE_OBJECTS) $(LIBTAM)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $^

$(BUILD)/test-p3: tests/test-p3.c $(CORE_OBJECTS) $(LIBTAM)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $^

$(BUILD)/test-pty: tests/test-pty.c VERSION | libtam-check
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LIBTAM)

test: $(BUILD)/tam-knox $(BUILD)/test-graph $(BUILD)/test-git $(BUILD)/test-syntax-render $(BUILD)/test-p3 $(BUILD)/test-pty
	$(BUILD)/test-graph
	$(BUILD)/test-git
	$(BUILD)/test-syntax-render
	$(BUILD)/test-p3
	$(BUILD)/test-pty $(BUILD)/tam-knox
	sh tests/cli.sh $(BUILD)/tam-knox $(VERSION)

sanitize:
	$(MAKE) BUILD=build/sanitize \
	  CFLAGS='-O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie' \
	  LDFLAGS='-fsanitize=address,undefined -no-pie' test

install: $(BUILD)/tam-knox
	install -Dm755 $(BUILD)/tam-knox $(DESTDIR)$(BINDIR)/tam-knox

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/tam-knox

clean:
	rm -rf build

.PHONY: all test sanitize install uninstall clean libtam-check

-include $(OBJECTS:.o=.d)
