/*
 * utils.c — Safe wrappers and common utilities for SmartShell.
 */

#include "utils.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void *xmalloc(size_t size)
{
    void *ptr = malloc(size);
    if (ptr == NULL && size != 0) {
        die("xmalloc: out of memory (requested %zu bytes)", size);
    }
    return ptr;
}

void *xcalloc(size_t nmemb, size_t size)
{
    void *ptr = calloc(nmemb, size);
    if (ptr == NULL && nmemb != 0 && size != 0) {
        die("xcalloc: out of memory (requested %zu * %zu bytes)", nmemb, size);
    }
    return ptr;
}

void *xrealloc(void *ptr, size_t size)
{
    void *new_ptr = realloc(ptr, size);
    if (new_ptr == NULL && size != 0) {
        die("xrealloc: out of memory (requested %zu bytes)", size);
    }
    return new_ptr;
}

char *xstrdup(const char *s)
{
    if (s == NULL) {
        die("xstrdup: NULL pointer passed");
    }
    char *dup = strdup(s);
    if (dup == NULL) {
        die("xstrdup: out of memory");
    }
    return dup;
}

int xclose(int fd)
{
    if (close(fd) == -1) {
        int saved_errno = errno;
        fprintf(stderr, "smartshell: warning: close(%d) failed: %s\n",
                fd, strerror(saved_errno));
        errno = saved_errno;
        return -1;
    }
    return 0;
}

void die(const char *fmt, ...)
{
    va_list ap;

    fprintf(stderr, "smartshell: fatal: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");

    exit(EXIT_FAILURE);
}
