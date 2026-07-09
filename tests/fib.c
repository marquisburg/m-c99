/* Fibonacci — returns fib(10) == 55 as exit code. */
int fib(int n) {
  if (n <= 1)
    return n;
  return fib(n - 1) + fib(n - 2);
}

int main(void) {
  return fib(10);
}
