#include "utils.h"

#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>

static int64_t pow10_int(int digits) {
  int64_t result = 1;
  while (digits-- > 0) {
    result *= 10;
  }
  return result;
}

static size_t append_char(char *buffer, size_t size, size_t pos, char c) {
  if (pos + 1 < size) {
    buffer[pos] = c;
  }
  return pos + 1;
}

static size_t append_str(char *buffer, size_t size, size_t pos, const char *src) {
  while (*src) {
    pos = append_char(buffer, size, pos, *src++);
  }
  return pos;
}

static size_t append_uint64(char *buffer, size_t size, size_t pos, uint64_t value) {
  char tmp[24];
  size_t len = 0;

  if (value == 0) {
    return append_char(buffer, size, pos, '0');
  }

  while (value > 0 && len < sizeof(tmp)) {
    tmp[len++] = (char)('0' + (value % 10));
    value /= 10;
  }

  while (len > 0) {
    pos = append_char(buffer, size, pos, tmp[--len]);
  }

  return pos;
}

double str_to_double(const char *str) {
  // Minimal replacement for strtod: supports optional sign and fractional part.
  if (!str) {
    return 0.0;
  }

  int sign = 1;
  if (*str == '-') {
    sign = -1;
    ++str;
  } else if (*str == '+') {
    ++str;
  }

  double value = 0.0;
  while (isdigit((unsigned char)*str)) {
    value = (value * 10.0) + (double)(*str - '0');
    ++str;
  }

  if (*str == '.') {
    ++str;
    double scale = 1.0;
    while (isdigit((unsigned char)*str)) {
      scale *= 10.0;
      value = value + (double)(*str - '0') / scale;
      ++str;
    }
  }

  return value * (double)sign;
}

char* double_to_str(double num, char *buffer, size_t size) {
  if (!buffer || size == 0) {
    return buffer;
  }

  int negative = num < 0.0;
  double abs_num = negative ? -num : num;
  int64_t scale = pow10_int(DOUBLE_MAX_FRACTION_DIGITS);
  double scaled = (abs_num * (double)scale) + 0.5;
  int64_t scaled_int = scaled > (double)INT64_MAX ? INT64_MAX : (int64_t)scaled;
  uint64_t int_part = (uint64_t)(scaled_int / scale);
  uint64_t frac_part = (uint64_t)(scaled_int % scale);

  size_t pos = 0;
  if (negative && (int_part != 0 || frac_part != 0)) {
    pos = append_char(buffer, size, pos, '-');
  }

  pos = append_uint64(buffer, size, pos, int_part);

  if (frac_part != 0) {
    pos = append_char(buffer, size, pos, '.');
    uint64_t pad_scale = scale / 10;
    while (pad_scale > frac_part && pad_scale > 0) {
      pos = append_char(buffer, size, pos, '0');
      pad_scale /= 10;
    }
    pos = append_uint64(buffer, size, pos, frac_part);
  }

  if (size > 0) {
    buffer[pos < size ? pos : (size - 1)] = '\0';
  }

  char *dot = strchr(buffer, '.');
  if (dot) {
    size_t len = strlen(buffer);
    while (len > 0 && buffer[len - 1] == '0') {
      buffer[--len] = '\0';
    }
    if (len > 0 && buffer[len - 1] == '.') {
      buffer[--len] = '\0';
    }
  }

  if (strcmp(buffer, "-0") == 0) {
    strncpy(buffer, "0", size);
    buffer[size - 1] = '\0';
  }

  return buffer;
}

double pow_int(double base, int exponent) {
  double result = 1.0;
  int exp = exponent < 0 ? -exponent : exponent;

  while (exp-- > 0) {
    result *= base;
  }

  return exponent < 0 ? 1.0 / result : result;
}