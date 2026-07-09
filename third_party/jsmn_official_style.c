#define C99MTLC_STRING_IMPL
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "jsmn.h"

static const char *JSON_STRING =
    "{\"user\": \"johndoe\", \"admin\": false, \"uid\": 1000,\n  "
    "\"groups\": [\"users\", \"wheel\", \"audio\", \"video\"]}";

static int jsoneq(const char *json, jsmntok_t *tok, const char *s) {
  if (tok->type == JSMN_STRING && (int)strlen(s) == tok->end - tok->start &&
      strncmp(json + tok->start, s, tok->end - tok->start) == 0) {
    return 0;
  }
  return -1;
}

/* Minimal printf stand-in for the demo (host CRT printf is also fine if linked). */
int putchar(int c);
int printf(const char *fmt, ...) {
  /* Only used for demo strings; write format literally for non-% and skip specs. */
  const char *p = fmt;
  while (*p) {
    if (*p == '%' && p[1]) {
      p++;
      if (*p == '%') { putchar('%'); p++; continue; }
      while (*p && *p != 's' && *p != 'd' && *p != 'c' && *p != 'n') p++;
      if (*p) p++;
      continue;
    }
    putchar(*p);
    p++;
  }
  return 0;
}

int main() {
  int i;
  int r;
  jsmn_parser p;
  jsmntok_t t[128];
  jsmn_init(&p);
  r = jsmn_parse(&p, JSON_STRING, strlen(JSON_STRING), t,
                 sizeof(t) / sizeof(t[0]));
  if (r < 0) return 1;
  if (r < 1 || t[0].type != JSMN_OBJECT) return 2;
  for (i = 1; i < r; i++) {
    if (jsoneq(JSON_STRING, &t[i], "user") == 0) {
      i++;
    } else if (jsoneq(JSON_STRING, &t[i], "admin") == 0) {
      i++;
    } else if (jsoneq(JSON_STRING, &t[i], "uid") == 0) {
      i++;
    } else if (jsoneq(JSON_STRING, &t[i], "groups") == 0) {
      if (t[i + 1].type != JSMN_ARRAY) continue;
      if (t[i + 1].size != 4) return 3;
      i += t[i + 1].size + 1;
    }
  }
  return 0;
}
