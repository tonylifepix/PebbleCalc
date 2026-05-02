#ifndef _h_UTILS_
#define _h_UTILS_

/* Do not include pebble.h when compiling the unittests. */
#ifndef __cplusplus
#   include <pebble.h>             /* for bool */
#endif
#include <stdlib.h>

double str_to_double(const char *str);
char* double_to_str(double num, char *buffer, size_t size);
double pow_int(double base, int exponent);

#define DOUBLE_MAX_FRACTION_DIGITS 6


#endif