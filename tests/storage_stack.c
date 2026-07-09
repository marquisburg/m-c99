/* Fixed arrays/structs must work as addressable contiguous objects.
 * (Lowered via custom-size stack locals, not per-declare heap.) */
struct Point {
  int x;
  int y;
};

int main(void) {
  int a[4];
  struct Point p;
  int i;
  for (i = 0; i < 4; i = i + 1)
    a[i] = (i + 1) * 10;
  p.x = a[0];
  p.y = a[3];
  if (a[0] + a[1] + a[2] + a[3] != 100)
    return 1;
  if (p.x != 10 || p.y != 40)
    return 2;
  /* string literal content */
  {
    const char *s = "OK";
    if (s[0] != 'O' || s[1] != 'K' || s[2] != 0)
      return 3;
  }
  return 0;
}
