# SmartShell Makefile
#
# Build configurations:
#   make            - Release build
#   make debug      - Debug build (-g, -O0, -DDEBUG)
#   make asan       - AddressSanitizer + UBSan build
#   make clean      - Remove all build artifacts
#   make test       - Build and run unit tests
#   make test-asan  - Build and run unit tests under ASan + UBSan
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

# -- Target binary ---------------------------------------------------------

TARGET = $(BINDIR)/smartshell

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
$(BUILDDIR)/$(TESTDIR)/%: $(TESTDIR)/%.c $(CORE_OBJS) | $(BUILDDIR)/$(TESTDIR)
	$(CC) $(CFLAGS) -I$(TESTDIR) -MMD -MP -o $@ $< $(CORE_OBJS) $(LDFLAGS)

# Create build directories
$(BUILDDIR):
	mkdir -p $@

$(BUILDDIR)/$(TESTDIR):
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

.PHONY: test test-asan
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

# -- Clean -----------------------------------------------------------------

.PHONY: clean
clean:
	rm -rf $(BUILDDIR) $(BINDIR)
