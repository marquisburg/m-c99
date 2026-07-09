#define C99MTLC_STRING_IMPL
#include <stddef.h>
#include <string.h>

#define JSMN_STATIC
#include "jsmn/jsmn.h"

int putchar(int c);
static void puts_s(const char *s) {
  while (*s) { putchar(*s); s = s + 1; }
  putchar(10);
}

static const char *JSON =
    "{\"user\": \"johndoe\", \"admin\": false, \"uid\": 1000, "
    "\"groups\": [\"users\", \"wheel\", \"audio\", \"video\"]}";

static int jsoneq(const char *json, jsmntok_t *tok, const char *s) {
  size_t n;
  if (tok->type != JSMN_STRING) return -1;
  n = strlen(s);
  if ((int)n != tok->end - tok->start) return -1;
  if (strncmp(json + tok->start, s, n) != 0) return -1;
  return 0;
}

int main(void) {
  jsmn_parser p;
  jsmntok_t t[64];
  int r, i, saw_user = 0, saw_uid = 0, groups = 0;
  jsmn_init(&p);
  r = jsmn_parse(&p, JSON, strlen(JSON), t, 64);
  if (r < 0) { puts_s("parse fail"); return 1; }
  if (r < 1 || t[0].type != JSMN_OBJECT) { puts_s("not object"); return 2; }
  for (i = 1; i < r; i = i + 1) {
    if (jsoneq(JSON, &t[i], "user") == 0) { saw_user = 1; i = i + 1; }
    else if (jsoneq(JSON, &t[i], "uid") == 0) { saw_uid = 1; i = i + 1; }
    else if (jsoneq(JSON, &t[i], "groups") == 0) {
      if (t[i + 1].type == JSMN_ARRAY) groups = t[i + 1].size;
      i = i + t[i + 1].size + 1;
    }
  }
  if (!saw_user || !saw_uid || groups != 4) { puts_s("shape fail"); return 3; }
  puts_s("jsmn ok");
  return 0;
}
