/* One missing semicolon. The parser used to report an error for every token
 * of the wreckage that followed; it must now report exactly one. */
int f(void) { return 1 }

int g(void) { return 2; }
int h(void) { return 3; }
