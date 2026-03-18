# Makefile for mobius-c
# Targeting Mac OS X 10.4 Tiger on PowerPC with GCC 4.0
#
# On Tiger: CC = gcc-4.0
# On modern macOS for development/testing: CC = cc

CC ?= cc
CFLAGS = -std=c99 -Wall -Wextra -pedantic -O2 \
         -I./include \
         -I/opt/homebrew/include \
         -DTARGET_OS_MAC=1

YAML_LDFLAGS = -L/opt/homebrew/lib -lyaml

# Obj-C flags (no -std=c99 — Obj-C uses its own standard)
OBJCFLAGS = -Wall -Wextra -O2 \
            -I./include \
            -I/opt/homebrew/include \
            -DTARGET_OS_MAC=1

# Tiger-specific flags (uncomment on Tiger):
# CC = gcc-4.0
# CFLAGS += -mmacosx-version-min=10.4 -I/usr/local/include
# OBJCFLAGS += -mmacosx-version-min=10.4 -I/usr/local/include
# YAML_LDFLAGS = -L/usr/local/lib -lyaml

LDFLAGS = -framework CoreFoundation \
          -framework Foundation \
          -lpthread

# CoreServices needed for Bonjour (dns_sd.h)
LDFLAGS += -framework CoreServices

# libyaml needed for Phase 4 (persistence)
# LDFLAGS += -lyaml

# --- Source files ---

# Phase 1: C wire format library
HOTLINE_C_SRCS = \
	src/hotline/field.c \
	src/hotline/transaction.c \
	src/hotline/handshake.c \
	src/hotline/user.c \
	src/hotline/time_conv.c \
	src/hotline/encoding.c \
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
	src/hotline/tracker.c

HOTLINE_C_OBJS = $(HOTLINE_C_SRCS:.c=.o)

# Phase 2: Obj-C client library
HOTLINE_OBJC_SRCS = \
	src/hotline/client.m

HOTLINE_OBJC_OBJS = $(HOTLINE_OBJC_SRCS:.m=.o)

# All hotline library objects
HOTLINE_OBJS = $(HOTLINE_C_OBJS) $(HOTLINE_OBJC_OBJS)

# Phase 4: Server application (persistence layer)
MOBIUS_SRCS = \
	src/mobius/agreement.c \
	src/mobius/flat_news.c \
	src/mobius/config_loader.c \
	src/mobius/yaml_account_manager.c \
	src/mobius/ban_file.c \
	src/mobius/threaded_news_yaml.c \
	src/mobius/transaction_handlers.c \
	src/mobius/logger_impl.c

MOBIUS_OBJS = $(MOBIUS_SRCS:.c=.o)

# Test sources
TEST_C_SRCS = test/test_runner.c
TEST_C_OBJS = $(TEST_C_SRCS:.c=.o)

TEST_OBJC_SRCS = test/test_client.m
TEST_OBJC_OBJS = $(TEST_OBJC_SRCS:.m=.o)

# --- Targets ---

.PHONY: all clean test test-wire test-client

all: libhotline.a lemoniscate

# Static library (C wire format + Obj-C client)
libhotline.a: $(HOTLINE_OBJS)
	ar rcs $@ $^
	@echo "Built libhotline.a (C wire format + Obj-C client)"

# Server binary
lemoniscate: $(HOTLINE_C_OBJS) $(MOBIUS_OBJS) src/main.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(YAML_LDFLAGS)
	@echo "Built lemoniscate server"

# Phase 1 wire format tests (C only, no Foundation needed)
test-wire: $(TEST_C_OBJS) $(HOTLINE_C_OBJS)
	$(CC) $(CFLAGS) -o test_runner $^ -framework CoreFoundation -lpthread
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

clean:
	rm -f $(HOTLINE_OBJS) $(MOBIUS_OBJS) $(TEST_C_OBJS) $(TEST_OBJC_OBJS) \
	      libhotline.a lemoniscate test_runner test_client src/main.o

# Auto-dependency generation
-include $(HOTLINE_C_OBJS:.o=.d)

%.d: %.c
	@$(CC) $(CFLAGS) -MM -MT $(@:.d=.o) $< > $@ 2>/dev/null || true
