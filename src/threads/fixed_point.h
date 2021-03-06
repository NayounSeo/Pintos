#define F (1 << 14)  // fixed point 1
#define int_MAX ((1 << 31) - 1)
#define int_MIN (-(1 << 31))

// x and y denote fixed_point numbers in 17.14 format 
// n is an int eger

int int_to_fp (int n);  /* integer를 fixed point로 전환 */
int fp_to_int_round (int x);  /* FP를 int로 전환 (반올림) */
int fp_to_int (int x);  /* FP를 int로 전환 (버림) */
int add_fp (int x,int y);  /* FP의 덧셈 */
int add_mixed (int x, int n);  /* FP와 int의 덧셈 */
int sub_fp (int x, int y);  /* FP의 뺄셈 (x - y) */
int sub_mixed (int x, int n);  /* FP와 int의 뺄셈(x - n) */
int mult_fp (int x, int y);  /* FP의 곱셈 */
int mult_mixed (int x, int n);  /* FP와 int의 곱셈 */
int div_fp (int x, int y);  /* FP의 나눗셈 (x / y) */
int div_mixed (int x, int n);  /* FP와 int의 나눗셈(x / n) */
int div_int_to_fp (int n, int m);

int
int_to_fp (int n)
{
  return n * F;
}

int fp_to_int_round (int x)
{
  if (x >= 0)
  {
    return (x + F / 2) / F;
  }
  return (x - F / 2) / F;
}

int
fp_to_int (int x)
{
  return x / F;
}

int
add_fp (int x, int y)
{
  return x + y;
}

int
add_mixed (int x, int n)
{
  return x + int_to_fp (n);
}

int
sub_fp (int x, int y)
{
  return x - y;
}

int
sub_mixed (int x, int n)
{
  return x - int_to_fp (n);
}

int
mult_fp (int x, int y)
{
  return ((int64_t)x) * y / F;  // TODO : ??
}

int
mult_mixed (int x, int n)
{
  return x * n;
}

int
div_fp (int x, int y)
{
  if (y == 0)
    return 0;
  return ((int64_t)x) * F / y;
}

int
div_mixed (int x, int n)
{
  if (n == 0)
    return 0;
  return x / n;
}

int
div_int_to_fp (int n, int m)
{
  if (m == 0)
    return 0;
  n = int_to_fp (n);
  return div_mixed (n, m);
}