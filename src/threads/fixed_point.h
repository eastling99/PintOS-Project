#define INT_MAX ((1 << 31) - 1)
#define INT_MIN (-(1 << 31))
#define Fp (1 << 14)

int int_to_fp (int n);
int fp_to_int (int x);
int round_fp_to_int (int x);
int fp_add (int x, int y);
int fp_sub (int x, int y);
int m_add (int x, int n);
int m_sub (int x, int n);
int fp_mult (int x, int y);
int m_mult (int x, int n);
int fp_div (int x, int y);
int m_div (int x, int n);

int int_to_fp (int n) 
{
  return n * Fp;
}

int fp_to_int (int x) 
{
  return x / Fp;
}

int round_fp_to_int (int x) 
{
  if (x >= 0) return (x + Fp / 2) / Fp;
  else return (x - Fp / 2) / Fp;
}

int fp_add (int x, int y) 
{
  return x + y;
}

int fp_sub (int x, int y) 
{
  return x - y;
}

int m_add (int x, int n) 
{
  return x + n * Fp;
}

int m_sub (int x, int n) 
{
  return x - n * Fp;
}

int fp_mult (int x, int y) 
{
  return ((int64_t) x) * y / Fp;
}

int m_mult (int x, int n) 
{
  return x * n;
}

int fp_div (int x, int y) 
{
  return ((int64_t) x) * Fp / y;
}

int m_div (int x, int n) 
{
  return x / n;
}
