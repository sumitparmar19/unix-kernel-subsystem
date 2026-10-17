#ifndef THREADS_FIXED_POINT_H
#define THREADS_FIXED_POINT_H

// fixed point math stuff for mlfqs
// using 17.14 format

#define F (1 << 14)

// convert n to fixed point
#define CONVERT_TO_FP(n) ((n) * F)

// convert x to integer (round toward zero)
#define CONVERT_TO_INT_ZERO(x) ((x) / F)

// convert x to integer (round to nearest)
#define CONVERT_TO_INT_NEAR(x) ((x) >= 0 ? ((x) + F / 2) / F : ((x) - F / 2) / F)

// add x and y
#define ADD_FP(x,y) ((x) + (y))

// subtract y from x
#define SUB_FP(x,y) ((x) - (y))

// add x and n
#define ADD_FP_INT(x,n) ((x) + (n) * F)

// subtract n from x
#define SUB_FP_INT(x,n) ((x) - (n) * F)

// multiply x by y
#define MULT_FP(x,y) ((int)(((int64_t)(x)) * (y) / F))

// multiply x by n
#define MULT_FP_INT(x,n) ((x) * (n))

// divide x by y
#define DIV_FP(x,y) ((int)((((int64_t)(x)) * F) / (y)))

// divide x by n
#define DIV_FP_INT(x,n) ((x) / (n))

#endif

