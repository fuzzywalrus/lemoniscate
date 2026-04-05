# Makefile for mobius-c
# Targeting Mac OS X 10.4 Tiger on PowerPC with GCC 4.0
#
# On Tiger: CC = gcc-4.0
# On modern macOS for development/testing: CC = cc

CC ?= cc

# Auto-detect Tiger/PPC vs modern macOS
IS_TIGER := $(shell test -d /usr/local/lib -a ! -d /opt/homebrew && echo yes)

ifeq ($(IS_TIGER),yes)
  # Tiger/Leopard PPC: gcc-4.0, /usr/local for libs
  CC = gcc-4.0
  EXTRA_INCLUDES = -I/usr/local/include
  YAML_LDFLAGS = /usr/local/lib/libyaml.a
  TIGER_FLAGS = -mmacosx-version-min=10.4
else
  # Modern macOS for development/testing
  EXTRA_INCLUDES = -I/opt/homebrew/include
  YAML_LDFLAGS = /opt/homebrew/lib/libyaml.a
  TIGER_FLAGS =
endif

CFLAGS = -std=c99 -Wall -Wextra -pedantic -O2 \
         -I./include \
         $(EXTRA_INCLUDES) \
         $(TIGER_FLAGS) \
         -DTARGET_OS_MAC=1

# Obj-C flags (no -std=c99 - Obj-C uses its own standard)
OBJCFLAGS = -Wall -Wextra -O2 \
            -I./include \
            $(EXTRA_INCLUDES) \
            $(TIGER_FLAGS) \
            -DTARGET_OS_MAC=1

LDFLAGS = -framework CoreFoundation \
          -framework Foundation \
          -lpthread

# CoreServices needed for Bonjour (dns_sd.h)
LDFLAGS += -framework CoreServices

# OpenSSL for SHA-1 password hashing (available on Tiger 10.4)
LDFLAGS += -lcrypto

# Security framework for TLS via SecureTransport (available on Tiger 10.4)
LDFLAGS += -framework Security

# libyaml needed for Phase 4 (persistence)
# LDFLAGS += -lyaml

# --- Source files ---

# Platform-specific sources (macOS/PPC: kqueue, SecTransport, CommonCrypto, CF encoding)
PLATFORM_SRCS = \
	src/hotline/platform/event_kqueue.c \
	src/hotline/platform/tls_sectransport.c \
	src/hotline/platform/crypto_commoncrypto.c \
	src/hotline/platform/encoding_cf.c

PLATFORM_OBJS = $(PLATFORM_SRCS:.c=.o)

# Phase 1: C wire format library (+ HTTP client for Mnemosyne)
HOTLINE_C_SRCS = \
	src/hotline/http_client.c \
	src/hotline/field.c \
	src/hotline/transaction.c \
	src/hotline/handshake.c \
	src/hotline/user.c \
	src/hotline/time_conv.c \
	src/hotline/file_resume_data.c \
	src/hotline/config.c \
	src/hotline/logger.c \
	src/hotline/file_store.c \
	src/hotline/stats.c \
	src/hotline/client_manager.c \
	src/hotline/chat.c \
	src/hotline/client_conn.c \
	src/hotline/server.c \
	src/hotline/file_path.c \
	src/hotline/file_name_with_info.c \
	src/hotline/file_types.c \
	src/hotline/files.c \
	src/hotline/file_wrapper.c \
	src/hotline/flattened_file_object.c \
	src/hotline/transfer.c \
	src/hotline/file_transfer.c \
	src/hotline/bonjour.c \
	src/hotline/tracker.c \
	src/hotline/password.c \
	src/hotline/hope.c

HOTLINE_C_OBJS = $(HOTLINE_C_SRCS:.c=.o)

# Phase 2: Obj-C client library
HOTLINE_OBJC_SRCS = \
	src/hotline/client.m

HOTLINE_OBJC_OBJS = $(HOTLINE_OBJC_SRCS:.m=.o)

# All hotline library objects
HOTLINE_OBJS = $(HOTLINE_C_OBJS) $(PLATFORM_OBJS) $(HOTLINE_OBJC_OBJS)

# Phase 4: Server application (persistence layer)
MOBIUS_SRCS = \
	src/mobius/agreement.c \
	src/mobius/flat_news.c \
	src/mobius/config_loader.c \
	src/mobius/yaml_account_manager.c \
	src/mobius/ban_file.c \
	src/mobius/threaded_news_yaml.c \
	src/mobius/transaction_handlers.c \
	src/mobius/logger_impl.c \
	src/mobius/config_plist.c \
	src/mobius/json_builder.c \
	src/mobius/mnemosyne_sync.c \
	src/mobius/dir_threaded_news.c \
	src/mobius/jsonl_message_board.c

MOBIUS_OBJS = $(MOBIUS_SRCS:.c=.o)

# Test sources
TEST_C_SRCS = test/test_runner.c
TEST_C_OBJS = $(TEST_C_SRCS:.c=.o)

TEST_OBJC_SRCS = test/test_client.m
TEST_OBJC_OBJS = $(TEST_OBJC_SRCS:.m=.o)

# --- Targets ---

# GUI sources (Obj-C, AppKit)
GUI_OBJC_SRCS = \
	src/gui/main.m \
	src/gui/AppController.m \
	src/gui/ProcessManager.m \
	src/gui/TigerCompat.m

GUI_OBJC_OBJS = $(GUI_OBJC_SRCS:.m=.o)

# GUI needs AppKit + Foundation
GUI_LDFLAGS = -framework AppKit \
              -framework Foundation \
              -framework CoreFoundation

# App bundle paths
APP_BUNDLE = Lemoniscate.app
APP_CONTENTS = $(APP_BUNDLE)/Contents
APP_MACOS = $(APP_CONTENTS)/MacOS
APP_RESOURCES = $(APP_CONTENTS)/Resources
SERVER_COMPAT_BIN = mobius-hotline-server
APP_SKIP_ARCH_CHECK ?= 0

.PHONY: all clean test test-wire test-client gui app

all: libhotline.a lemoniscate $(SERVER_COMPAT_BIN)

# Static library (C wire format + Obj-C client)
libhotline.a: $(HOTLINE_OBJS)
	ar rcs $@ $^
	@echo "Built libhotline.a (C wire format + Obj-C client)"

# Server binary
lemoniscate: $(HOTLINE_C_OBJS) $(PLATFORM_OBJS) $(MOBIUS_OBJS) src/main.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(YAML_LDFLAGS)
	@echo "Built lemoniscate server"

# Compatibility alias used by MobiusAdmin launcher expectations
$(SERVER_COMPAT_BIN): lemoniscate
	cp lemoniscate $(SERVER_COMPAT_BIN)
	chmod +x $(SERVER_COMPAT_BIN)
	@echo "Built $(SERVER_COMPAT_BIN) compatibility binary"

# Phase 1 wire format tests (C only, no Foundation needed)
test-wire: $(TEST_C_OBJS) $(HOTLINE_C_OBJS) $(PLATFORM_OBJS)
	$(CC) $(CFLAGS) -o test_runner $^ -framework CoreFoundation -framework Security -lpthread -lcrypto
	./test_runner

# Phase 2 client tests (Obj-C, needs Foundation)
test-client: $(TEST_OBJC_OBJS) $(HOTLINE_OBJS)
	$(CC) $(OBJCFLAGS) -o test_client $^ $(LDFLAGS)
	./test_client

# Run all tests
test: test-wire test-client

# --- Pattern rules ---

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.m
	$(CC) $(OBJCFLAGS) -c -o $@ $<

# GUI binary (links against AppKit for the admin interface)
gui: $(GUI_OBJC_OBJS)
	$(CC) $(OBJCFLAGS) -o lemoniscate-gui $^ $(GUI_LDFLAGS)
	@echo "Built lemoniscate-gui"

# App bundle: Lemoniscate.app containing server binary + GUI
app: lemoniscate $(SERVER_COMPAT_BIN) gui
	@if [ "$(APP_SKIP_ARCH_CHECK)" != "1" ]; then \
		server_arch=`file lemoniscate | grep -Eo 'ppc64|ppc|x86_64|arm64|i386' | head -n1`; \
		gui_arch=`file lemoniscate-gui | grep -Eo 'ppc64|ppc|x86_64|arm64|i386' | head -n1`; \
		if [ -z "$$server_arch" ] || [ -z "$$gui_arch" ]; then \
			echo "ERROR: Could not detect binary architecture for app bundle."; \
			exit 1; \
		fi; \
		if [ "$$server_arch" != "$$gui_arch" ]; then \
			echo "ERROR: Architecture mismatch: lemoniscate ($$server_arch) vs lemoniscate-gui ($$gui_arch)."; \
			echo "Build both binaries on the same target architecture (e.g. PPC + gcc-4.0 on Tiger)."; \
			echo "Set APP_SKIP_ARCH_CHECK=1 to bypass this safety check."; \
			exit 1; \
		fi; \
	fi
	mkdir -p $(APP_MACOS) $(APP_RESOURCES)
	cp lemoniscate-gui $(APP_MACOS)/Lemoniscate
	cp lemoniscate $(APP_MACOS)/lemoniscate-server
	cp $(SERVER_COMPAT_BIN) $(APP_MACOS)/$(SERVER_COMPAT_BIN)
	cp resources/Info.plist $(APP_CONTENTS)/Info.plist
	@test -f resources/lemoniscate.icns || (echo "ERROR: missing resources/lemoniscate.icns"; exit 1)
	cp resources/lemoniscate.icns $(APP_RESOURCES)/Lemoniscate.icns
	@test -f resources/lemoniscate-256.png && cp resources/lemoniscate-256.png $(APP_RESOURCES)/lemoniscate-256.png || true
	@test -f resources/default-banner.jpg && cp resources/default-banner.jpg $(APP_RESOURCES)/default-banner.jpg || true
	@echo "Built $(APP_BUNDLE)"
	@echo "  Server binary: $(APP_MACOS)/lemoniscate-server"
	@echo "  Compat binary: $(APP_MACOS)/$(SERVER_COMPAT_BIN)"
	@echo "  GUI binary:    $(APP_MACOS)/Lemoniscate"

clean:
	rm -f $(HOTLINE_OBJS) $(PLATFORM_OBJS) $(MOBIUS_OBJS) $(TEST_C_OBJS) $(TEST_OBJC_OBJS) \
	      $(GUI_OBJC_OBJS) \
	      libhotline.a lemoniscate $(SERVER_COMPAT_BIN) lemoniscate-gui test_runner test_client src/main.o
	rm -rf $(APP_BUNDLE)

# Auto-dependency generation
-include $(HOTLINE_C_OBJS:.o=.d)

%.d: %.c
	@$(CC) $(CFLAGS) -MM -MT $(@:.d=.o) $< > $@ 2>/dev/null || true
