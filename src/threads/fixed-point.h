//header which defines arithmetic operations on fixed point variables
//simulating floating-point arithmetic
//to avoid overflow, the functions will handle 64-bit signed integers
#include<stdint.h>
#include<stdbool.h>

#define m_inside_int32_bounds(x) ( -2147483648 <= x && x <= 2147483647 )

//use const keyword instead of macros to facilitate debugging
static const int32_t NUMBER_OF_FRACTIONAL_BITS = 14;
static const int32_t MULTIPLICATION = 1<<14;//(1<<NUMBER_OF_FRACTIONAL_BITS);
static const int32_t ADJUST_SIZE = 100;

//rounds to nearest integer
//checks if the divisor is also multiplied by a constant
int64_t fu_rounding_division(int64_t x, int64_t y, bool b_divisor_multiplied);

//eliminates the constant with which the result is multiplied  and adjust its
//value
int fu_adjust(int64_t x);

//multiplies by a constant
int64_t fu_introduce(int n);

//divides by a constant
int fu_extract(int64_t x);

//(a + (b/c)) = (a*c + b)/c
int64_t fu_share_division(int64_t x, int64_t y, int n);
