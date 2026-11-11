# SmartShell Makefile
#
# Build configurations:
#   make                  - Release build (smartshell CLI)
#   make server           - Build smartshell-server web server
#   make debug            - Debug build (-g, -O0, -DDEBUG)
#   make asan             - AddressSanitizer + UBSan build
#   make clean            - Remove all build artifacts
#   make test             - Build and run unit tests
#   make test-asan        - Build and run unit tests under ASan + UBSan
#   make test-integration - Run integration tests
#   make test-server      - Run server tests
#
# NOTE: Run 'make clean' before switching between build configurations.

# -- Compiler and flags ----------------------------------------------------

CC       = gcc
CSTD     = -std=c11
WARNINGS = -Wall -Wextra -Werror -pedantic
INCLUDES = -Iinclude
DEFINES  = -D_POSIX_C_SOURCE=200809L

CFLAGS   = $(CSTD) $(WARNINGS) $(INCLUDES) $(DEFINES)
LDFLAGS  =

# -- Directories -----------------------------------------------------------

SRCDIR   = src
TESTDIR  = tests
BUILDDIR = build
BINDIR   = bin

# -- Source and object files -----------------------------------------------

CORE_SRCS = $(filter-out $(SRCDIR)/main.c, $(wildcard $(SRCDIR)/*.c))
CORE_OBJS = $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.o,$(CORE_SRCS))

MAIN_SRC  = $(SRCDIR)/main.c
MAIN_OBJ  = $(BUILDDIR)/main.o

TEST_SRCS = $(wildcard $(TESTDIR)/test_*.c)
TEST_BINS = $(patsubst $(TESTDIR)/%.c,$(BUILDDIR)/$(TESTDIR)/%,$(TEST_SRCS))

ALL_OBJS  = $(CORE_OBJS) $(MAIN_OBJ)
DEPS      = $(ALL_OBJS:.o=.d)

# -- Server source and object files ----------------------------------------

SRVDIR        = $(SRCDIR)/server
THIRDPARTYDIR = third_party

SERVER_SRCS = $(wildcard $(SRVDIR)/*.c)
SERVER_OBJS = $(patsubst $(SRVDIR)/%.c,$(BUILDDIR)/server/%.o,$(SERVER_SRCS))

# Third-party objects (compiled with relaxed warnings)
CIVETWEB_OBJ  = $(BUILDDIR)/third_party/civetweb.o
CJSON_OBJ     = $(BUILDDIR)/third_party/cJSON.o

SERVER_ALL_OBJS = $(SERVER_OBJS) $(CORE_OBJS) $(CIVETWEB_OBJ) $(CJSON_OBJ)
SERVER_DEPS     = $(SERVER_OBJS:.o=.d)

# Server needs pthreads (CivetWeb uses threads) and dl (dynamic loading)
SERVER_LDFLAGS  = $(LDFLAGS) -lpthread -ldl

# Server-specific includes add third-party headers
SERVER_CFLAGS   = $(CFLAGS) -D_DEFAULT_SOURCE -I$(THIRDPARTYDIR)/civetweb -I$(THIRDPARTYDIR)/cjson

# -- Target binaries -------------------------------------------------------

TARGET        = $(BINDIR)/smartshell
TARGET_SERVER = $(BINDIR)/smartshell-server

# -- Default target --------------------------------------------------------

.PHONY: all
all: $(TARGET)

# Link smartshell binary
$(TARGET): $(MAIN_OBJ) $(CORE_OBJS) | $(BINDIR)
	$(CC) $(LDFLAGS) -o $@ $^

# Compile source files
$(BUILDDIR)/%.o: $(SRCDIR)/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

# Compile and link test binaries
$(BUILDDIR)/$(TESTDIR)/test_server_auth: $(TESTDIR)/test_server_auth.c $(CORE_OBJS) $(BUILDDIR)/server/auth.o | $(BUILDDIR)/$(TESTDIR)
	$(CC) $(CFLAGS) -D_DEFAULT_SOURCE -I$(TESTDIR) -MMD -MP -o $@ $< $(CORE_OBJS) $(BUILDDIR)/server/auth.o $(LDFLAGS)

$(BUILDDIR)/$(TESTDIR)/%: $(TESTDIR)/%.c $(CORE_OBJS) | $(BUILDDIR)/$(TESTDIR)
	$(CC) $(CFLAGS) -I$(TESTDIR) -MMD -MP -o $@ $< $(CORE_OBJS) $(LDFLAGS)

# Create build directories
$(BUILDDIR):
	mkdir -p $@

$(BUILDDIR)/$(TESTDIR):
	mkdir -p $@

$(BINDIR):
	mkdir -p $@

$(BUILDDIR)/server:
	mkdir -p $@

$(BUILDDIR)/third_party:
	mkdir -p $@

# Include auto-generated dependency files
-include $(DEPS)
-include $(SERVER_DEPS)

# -- Server build ----------------------------------------------------------

.PHONY: server
server: $(TARGET_SERVER)

# Compile server source files (need third-party includes)
$(BUILDDIR)/server/%.o: $(SRVDIR)/%.c | $(BUILDDIR)/server
	$(CC) $(SERVER_CFLAGS) -MMD -MP -c -o $@ $<

# Compile third-party sources with relaxed warnings (no -Werror -pedantic)
$(CIVETWEB_OBJ): $(THIRDPARTYDIR)/civetweb/civetweb.c | $(BUILDDIR)/third_party
	$(CC) -std=c11 -Wall -Wextra -O2 $(filter -fsanitize=%,$(CFLAGS)) -I$(THIRDPARTYDIR)/civetweb -D_POSIX_C_SOURCE=200809L -DUSE_IPV6=0 -DNDEBUG -DNO_SSL -DNO_CGI -c -o $@ $<

$(CJSON_OBJ): $(THIRDPARTYDIR)/cjson/cJSON.c | $(BUILDDIR)/third_party
	$(CC) -std=c11 -Wall -Wextra -O2 $(filter -fsanitize=%,$(CFLAGS)) -I$(THIRDPARTYDIR)/cjson -c -o $@ $<

# Link smartshell-server binary
$(TARGET_SERVER): $(SERVER_ALL_OBJS) | $(BINDIR)
	$(CC) $(LDFLAGS) -o $@ $(SERVER_ALL_OBJS) $(SERVER_LDFLAGS)

# -- Debug build -----------------------------------------------------------

.PHONY: debug
debug: CFLAGS  += -g -O0 -DDEBUG
debug: $(TARGET)

# -- Sanitizer build -------------------------------------------------------

.PHONY: asan server-asan
asan: CFLAGS  += -fsanitize=address,undefined -g -O1 -fno-omit-frame-pointer
asan: LDFLAGS += -fsanitize=address,undefined
asan: $(TARGET)

server-asan: CFLAGS  += -fsanitize=address,undefined -g -O1 -fno-omit-frame-pointer
server-asan: LDFLAGS += -fsanitize=address,undefined
server-asan: $(TARGET_SERVER)

# -- Test targets ----------------------------------------------------------

.PHONY: test test-asan test-integration test-server test-server-asan
test: $(TEST_BINS)
	@echo "=== Running Unit Tests ==="
	@for t in $(TEST_BINS); do \
		echo "Executing $$t:"; \
		$$t || exit 1; \
	done
	@echo "All unit tests passed successfully."

test-asan: CFLAGS  += -fsanitize=address,undefined -g -O1 -fno-omit-frame-pointer
test-asan: LDFLAGS += -fsanitize=address,undefined
test-asan: clean test

test-integration: $(TARGET)
	@$(TESTDIR)/integration/run_tests.sh

test-server: $(TARGET_SERVER)
	@python3 $(TESTDIR)/test_server.py

test-server-asan: CFLAGS  += -fsanitize=address,undefined -g -O1 -fno-omit-frame-pointer
test-server-asan: LDFLAGS += -fsanitize=address,undefined
test-server-asan: clean test-server

# -- Clean -----------------------------------------------------------------

.PHONY: clean
clean:
	rm -rf $(BUILDDIR) $(BINDIR)
