# SmartShell Makefile
#
# Build configurations:
#   make            - Release build
#   make debug      - Debug build (-g, -O0, -DDEBUG)
#   make asan       - AddressSanitizer + UBSan build
#   make clean      - Remove all build artifacts
#   make test       - Run unit tests
#   make test-asan  - Run unit tests under ASan + UBSan
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
BUILDDIR = build
BINDIR   = bin

# -- Source and object files -----------------------------------------------

SRCS = $(wildcard $(SRCDIR)/*.c)
OBJS = $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.o,$(SRCS))
DEPS = $(OBJS:.o=.d)

# -- Target binary ---------------------------------------------------------

TARGET = $(BINDIR)/smartshell

# -- Default target --------------------------------------------------------

.PHONY: all
all: $(TARGET)

# Link
$(TARGET): $(OBJS) | $(BINDIR)
	$(CC) $(LDFLAGS) -o $@ $^

# Compile
$(BUILDDIR)/%.o: $(SRCDIR)/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

# Create build directories
$(BUILDDIR):
	mkdir -p $@

$(BINDIR):
	mkdir -p $@

# Include auto-generated dependency files
-include $(DEPS)

# -- Debug build -----------------------------------------------------------

.PHONY: debug
debug: CFLAGS  += -g -O0 -DDEBUG
debug: $(TARGET)

# -- Sanitizer build -------------------------------------------------------

.PHONY: asan
asan: CFLAGS  += -fsanitize=address,undefined -g -O1 -fno-omit-frame-pointer
asan: LDFLAGS += -fsanitize=address,undefined
asan: $(TARGET)

# -- Test targets ----------------------------------------------------------

.PHONY: test test-asan test-integration test-valgrind
test:
	@echo "No unit tests defined yet (Phase 1)."

test-asan:
	@echo "No unit tests defined yet (Phase 1)."

test-integration:
	@echo "No integration tests defined yet (Phase 1)."

test-valgrind:
	@echo "No unit tests defined yet (Phase 1)."

# -- Clean -----------------------------------------------------------------

.PHONY: clean
clean:
	rm -rf $(BUILDDIR) $(BINDIR)
