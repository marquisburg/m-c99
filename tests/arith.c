int main(void) {
  int a = 40;
  int b = 2;
  int c = a + b;
  if (c != 42)
    return 1;
  if (c * 2 != 84)
    return 2;
  if ((c > 40) && (b < 3))
    return 0;
  return 3;
}
