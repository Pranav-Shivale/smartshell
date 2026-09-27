/*
 * utils.h — Safe wrappers and common utilities for SmartShell.
 *
 * All x-prefixed allocation functions terminate the process on failure.
 * This avoids the need for NULL checks at every call site.
 */

#ifndef SMARTSHELL_UTILS_H
#define SMARTSHELL_UTILS_H

#include <stddef.h>

/*
 * Safe memory allocation wrappers.
 * Each function prints an error message to stderr and calls exit(EXIT_FAILURE)
 * if the underlying allocation fails.
 */
void *xmalloc(size_t size);
void *xcalloc(size_t nmemb, size_t size);
void *xrealloc(void *ptr, size_t size);
char *xstrdup(const char *s);

/*
 * Safe file descriptor close.
 * Prints a warning to stderr if close() fails.
 * Returns 0 on success, -1 on failure (with errno preserved).
 */
int xclose(int fd);

/*
 * Print a formatted error message to stderr and terminate with EXIT_FAILURE.
 * The message is prefixed with "smartshell: fatal: ".
 */
__attribute__((format(printf, 1, 2), noreturn))
void die(const char *fmt, ...);

#endif /* SMARTSHELL_UTILS_H */
