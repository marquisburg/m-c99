int main(void) {
  _Complex double z = 3.0 + 4.0 * I;
  double r = __real__(z);
  double im = __imag__(z);
  /* |z|^2 = 9+16 = 25 */
  double mag2 = r * r + im * im;
  if (mag2 < 24.5 || mag2 > 25.5)
    return 1;
  _Complex double w = z + z;
  double wr = __real__(w);
  if (wr < 5.5 || wr > 6.5)
    return 2;
  return 0;
}
