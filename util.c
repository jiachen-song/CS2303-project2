#include "util.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static void die_oom(void) {
  fprintf(stderr, "Fatal: out of memory\n");
  exit(1);
}

void *xmalloc(size_t size) {
  void *p = malloc(size);
  if (!p)
    die_oom();
  return p;
}

void *xrealloc(void *ptr, size_t size) {
  void *p = realloc(ptr, size);
  if (!p)
    die_oom();
  return p;
}

char *xstrdup(const char *s) {
  char *p = strdup(s);
  if (!p)
    die_oom();
  return p;
}

char *xasprintf(const char *fmt, ...) {
  va_list ap, ap2;
  va_start(ap, fmt);
  va_copy(ap2, ap);

  int len = vsnprintf(NULL, 0, fmt, ap);
  va_end(ap);
  if (len < 0) {
    va_end(ap2);
    fprintf(stderr, "Fatal: vsnprintf failed\n");
    exit(1);
  }

  char *buf = xmalloc((size_t)len + 1);
  vsnprintf(buf, (size_t)len + 1, fmt, ap2);
  va_end(ap2);
  return buf;
}

int ensure_dir_recursive(const char *path) {
  if (!path || !*path)
    return -1;

  /* Work on a mutable copy because we NUL-terminate at each separator. */
  char tmp[4096];
  size_t n = strlen(path);
  if (n >= sizeof(tmp))
    return -1;
  memcpy(tmp, path, n + 1);
  /* Strip a trailing slash so we don't mkdir("") on the final segment. */
  while (n > 1 && tmp[n - 1] == '/')
    tmp[--n] = '\0';

  for (char *p = tmp + 1; *p; p++) {
    if (*p == '/') {
      *p = '\0';
      if (mkdir(tmp, 0755) != 0 && errno != EEXIST)
        return -1;
      *p = '/';
    }
  }
  if (mkdir(tmp, 0755) != 0 && errno != EEXIST)
    return -1;
  return 0;
}
