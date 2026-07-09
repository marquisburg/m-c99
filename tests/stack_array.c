int main(void) {
  int a[5];
  int i, s;
  for (i = 0; i < 5; i = i + 1) a[i] = i + 1;
  s = 0;
  for (i = 0; i < 5; i = i + 1) s = s + a[i];
  return s; /* 15 */
}
