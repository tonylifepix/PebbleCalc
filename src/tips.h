#ifndef TIPS_H
#define TIPS_H

#include <pebble.h>

typedef void (*TipsTouchCallback)(void);

void tips_show(double base_value, TipsTouchCallback disable_touch, TipsTouchCallback enable_touch);
void tips_deinit(void);

#endif
