#include "fixed-point.h"
#include <debug.h>



//extracts sign of argument
static int8_t fu_sign(int64_t x);

//divides two numbers multiplied by a constant, rounding the result
//if the divisor is multiplied by a constant then the divident will be
//multiplied as well
int64_t fu_rounding_division(int64_t x, int64_t y, bool b_divisor_multiplied)
{
  //multiplying x by MULTIPLICATION so that the constant is not lost
  if(b_divisor_multiplied == true)
  {
    //check for overflow
    ASSERT(fu_sign(x*MULTIPLICATION) == fu_sign(x) * fu_sign(MULTIPLICATION));
    x *= MULTIPLICATION;
  }
  //if the number is positive it adds half the divisor
  //else, it substracts half the divisor
  ASSERT(y != 0);

  int64_t i64_round;
  if(x > 0)
    i64_round = (x + y/2);
  else
    i64_round = (x - y/2);

  int64_t result = i64_round/y;
  //check that the result has correct sign
  //if the result is not 0, than x would be greater in absolute value than y
  //so i64_round should have the same sign as x
  ASSERT(fu_sign(i64_round) / fu_sign(y) == fu_sign(result) || result == 0);

  return result;
}

//adjusts size, removes constant with which is multiplied
int fu_adjust(int64_t x)
{
  ASSERT(fu_sign(x*ADJUST_SIZE) == fu_sign(x));//assumes ADJUST_SIZE is positive

  //have to store the call in a variable because a macro would repeat the
  //function call
  int64_t result = fu_rounding_division(x * ADJUST_SIZE, MULTIPLICATION, false);
  ASSERT(m_inside_int32_bounds(result));
  //the constant MULTIPLICATION is not multiplied with itself
  return (int)result;
}

int64_t fu_introduce(int n)
{
  return (int64_t)n * MULTIPLICATION;
}


//divides by a constant
int fu_extract(int64_t x)
{
  return fu_rounding_division(x, MULTIPLICATION, false);
}

//(a + (b/c)) = (a*c + b)/c
//where c is not multiplied by a constant
int64_t fu_share_division(int64_t x, int64_t y, int n)
{
  int64_t product = x * n;
  ASSERT(fu_sign(product) == fu_sign(x) * fu_sign(n));

  return fu_rounding_division(x * n + y, n, false);
}

static int8_t fu_sign(int64_t x)
{
  if(x > 0)
    return 1;
  else if(x < 0)
    return -1;
  return 0;
}

