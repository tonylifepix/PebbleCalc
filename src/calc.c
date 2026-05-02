#include <pebble.h>
#include <ctype.h>
#include <limits.h>
#include <stdint.h>

#include "utils.h"

#define MAX_LENGTH 14

//Layers
static Window *s_window;
static TextLayer *s_result_text_layer;
static Layer *s_buttons_layer;
static int8_t s_touch_down_button = -1;
static bool s_touch_subscribed = false;
static GPoint s_swipe_start_point;
static bool s_swipe_start_in_text = false;

// Ultra short vibration pattern for button presses
static const uint32_t segments[] = { 50 };
VibePattern pat = {
  .durations = segments,
  .num_segments = ARRAY_LENGTH(segments),
};

typedef struct {
  GRect text_bounds;
  GPoint grid_origin;
  int16_t cell_w;
  int16_t cell_h;
  int16_t button_w;
  int16_t button_h;
  int16_t cell_pad;
} Layout;

static Layout s_layout;

//Button labels
static char *buttons[] = {
  "+","-","*","/",
  "0","^","CE","AC",
  "1","2","3","+-",
  "4","5","6",".",
  "7","8","9","="
};

static int8_t selected_button = 13; //Currently selected button, starts on '5'

static bool operator_entered = false; //Has the operator been entered yet
static bool num1_is_ans = false; //Is the previous result in num1? Used to allow further calculations on result.
static char num1[MAX_LENGTH] = ""; //First operand
static char num2[MAX_LENGTH] = ""; //Second operand
static char result_text[MAX_LENGTH] = ""; //Results text layer buffer string
static uint8_t operator = 0; //Operator, where 0 is +, 1 is -, 2 is *, 3 is /, 4 is ^

static void layout_update(GRect bounds) {
#if defined(PBL_PLATFORM_EMERY)
  s_layout.text_bounds = GRect(6, 6, bounds.size.w - 12, 32);
  s_layout.cell_pad = 2;
  s_layout.cell_w = (bounds.size.w - 4) / 4;
  int16_t grid_top = s_layout.text_bounds.origin.y + s_layout.text_bounds.size.h + 2;
  s_layout.cell_h = (bounds.size.h - grid_top - 2) / 5;
  s_layout.button_w = s_layout.cell_w - (s_layout.cell_pad * 2);
  s_layout.button_h = s_layout.cell_h - (s_layout.cell_pad * 2);
  s_layout.grid_origin = GPoint(2, grid_top);
#else
  s_layout.text_bounds = GRect(6, 6, 132, 32);
  s_layout.grid_origin = GPoint(2, 40);
  s_layout.cell_w = 36;
  s_layout.cell_h = 26;
  s_layout.button_w = 32;
  s_layout.button_h = 22;
  s_layout.cell_pad = 2;
#endif
}

//Update handler for the buttons layer
static void button_layer_update(Layer *layer, GContext *ctx) {
  //Set up colors
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_fill_color(ctx, GColorBlack);
  //Set font
  GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  //Nested loop. 5 rows of 4
  for(int y = 0; y < 5; y++){
    for(int x = 0; x < 4; x++){
      //Define button bounds
      GRect rect_bounds = GRect(
        s_layout.grid_origin.x + (x * s_layout.cell_w) + s_layout.cell_pad,
        s_layout.grid_origin.y + (y * s_layout.cell_h) + s_layout.cell_pad,
        s_layout.button_w,
        s_layout.button_h
      );
      if( (y*4 + x) == selected_button ){ //If currently rendered button is the selected button
        //Change the text color and draw a filled rectangle
        graphics_context_set_text_color(ctx, GColorWhite);
        #if defined(PBL_COLOR)
          graphics_context_set_fill_color(ctx, GColorBlue);
        #endif
        graphics_fill_rect(ctx, rect_bounds, 2, GCornersAll);
      }
      else { //For all other buttons
        //Use a black text color and draw an empty rectangle
        graphics_context_set_text_color(ctx, GColorBlack);
        #if defined(PBL_COLOR)
          graphics_context_set_fill_color(ctx, GColorCyan);
          graphics_fill_rect(ctx, rect_bounds, 2, GCornersAll);
        #elif defined(PBL_BW)
          graphics_draw_round_rect(ctx, rect_bounds, 2);
        #endif
      }
      //Define text bounds
      GRect text_bounds = GRect(
        rect_bounds.origin.x,
        rect_bounds.origin.y,
        rect_bounds.size.w,
        rect_bounds.size.h - 3
      );
      //Draw the button labels
      graphics_draw_text(ctx, buttons[y*4 + x], font, text_bounds, GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
    }
  }
}

//Up or Down button handler
static void up_down_handler(ClickRecognizerRef recognizer, void *context){
  //Move selected button down if down is pressed, and up if up is pressed
  selected_button += (click_recognizer_get_button_id(recognizer) == BUTTON_ID_DOWN) ? 1 : -1;
  //If selected button is outside button range, wrap around
  selected_button = selected_button < 0 ? 19 : selected_button > 19 ? 0 : selected_button;
  //Mark button layer dirty for redraw
  layer_mark_dirty(s_buttons_layer);
}

//Enteres the contents of the currently selected button into the currently edited number
static int count_fractional_digits(const char *num) {
  const char *dot = strchr(num, '.');
  return dot ? (int)strlen(dot + 1) : 0;
}

static bool has_decimal_point(const char *num) {
  return strchr(num, '.') != NULL;
}

static const char* operator_symbol(uint8_t op) {
  switch (op) {
    case 0:
      return "+";
    case 1:
      return "-";
    case 2:
      return "*";
    case 3:
      return "/";
    case 4:
      return "^";
    default:
      return "?";
  }
}

static bool parse_int64_str(const char *str, int64_t *out) {
  if (!str || !*str || !out) {
    return false;
  }

  bool negative = false;
  if (*str == '-') {
    negative = true;
    ++str;
  } else if (*str == '+') {
    ++str;
  }

  if (!*str) {
    return false;
  }

  uint64_t value = 0;
  uint64_t limit = negative ? ((uint64_t)INT64_MAX + 1u) : (uint64_t)INT64_MAX;

  while (*str) {
    if (!isdigit((unsigned char)*str)) {
      return false;
    }
    uint64_t digit = (uint64_t)(*str - '0');
    if (value > (limit - digit) / 10u) {
      return false;
    }
    value = (value * 10u) + digit;
    ++str;
  }

  if (negative) {
    if (value == limit) {
      *out = INT64_MIN;
    } else {
      *out = -(int64_t)value;
    }
  } else {
    *out = (int64_t)value;
  }

  return true;
}

static bool int64_add_safe(int64_t a, int64_t b, int64_t *out) {
  if ((b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b)) {
    return false;
  }
  *out = a + b;
  return true;
}

static bool int64_sub_safe(int64_t a, int64_t b, int64_t *out) {
  if ((b > 0 && a < INT64_MIN + b) || (b < 0 && a > INT64_MAX + b)) {
    return false;
  }
  *out = a - b;
  return true;
}

static bool int64_mul_safe(int64_t a, int64_t b, int64_t *out) {
  if (a == 0 || b == 0) {
    *out = 0;
    return true;
  }

  if (a > 0) {
    if (b > 0) {
      if (a > INT64_MAX / b) {
        return false;
      }
    } else {
      if (b < INT64_MIN / a) {
        return false;
      }
    }
  } else {
    if (b > 0) {
      if (a < INT64_MIN / b) {
        return false;
      }
    } else {
      if (a != 0 && b < INT64_MAX / a) {
        return false;
      }
    }
  }

  *out = a * b;
  return true;
}

static void int64_to_str(int64_t value, char *buffer, size_t size) {
  if (!buffer || size == 0) {
    return;
  }

  if (value == INT64_MIN) {
    const char *min_text = "-9223372036854775808";
    size_t i = 0;
    while (i + 1 < size && min_text[i]) {
      buffer[i] = min_text[i];
      ++i;
    }
    buffer[i < size ? i : (size - 1)] = '\0';
    return;
  }

  bool negative = value < 0;
  uint64_t abs_value = (uint64_t)(negative ? -value : value);
  char temp[24];
  size_t pos = 0;

  if (abs_value == 0) {
    temp[pos++] = '0';
  } else {
    while (abs_value > 0 && pos < sizeof(temp)) {
      temp[pos++] = (char)('0' + (abs_value % 10));
      abs_value /= 10;
    }
  }

  size_t out_pos = 0;
  if (negative && out_pos + 1 < size) {
    buffer[out_pos++] = '-';
  }

  while (pos > 0 && out_pos + 1 < size) {
    buffer[out_pos++] = temp[--pos];
  }

  buffer[out_pos < size ? out_pos : (size - 1)] = '\0';
}

static bool fits_display(const char *text) {
  return text && (strlen(text) < MAX_LENGTH);
}

static void enter(){
  char *num = operator_entered ? num2 : num1; //Create a pointer to the currnetly edited number
  const char *label = buttons[selected_button];
  bool is_decimal = (label[0] == '.' && label[1] == '\0');

  if (is_decimal && has_decimal_point(num)) {
    return;
  }

  if (!is_decimal && has_decimal_point(num) && count_fractional_digits(num) >= DOUBLE_MAX_FRACTION_DIGITS) {
    return;
  }

  if(strlen(num) < MAX_LENGTH-1){ //Make sure string is smaller than the max length (-1 to exclude null character)
    strcat(num, label); //Add needed character to the end of the string
    text_layer_set_text(s_result_text_layer, num); //Display num
  }
}

//Set the operator
static void enter_operator(uint8_t id){
  operator = id; //Set operator to operator id
  operator_entered = true;
  text_layer_set_text(s_result_text_layer, buttons[selected_button]); //Display operator
}

//Backspace. Clears whole number if full is true
static void clear(bool full){
  char *num = operator_entered ? num2 : num1; //Create a pointer to the currnetly edited number
  if(full)
    *num = 0;
  else if (strlen(num) > 0)
    num[strlen(num)-1] = 0;
  text_layer_set_text(s_result_text_layer, num);
}

//All clear. Clears all numbers and operator.
static void all_clear(){
  *num1 = 0;
  *num2 = 0;
  operator_entered = false;
  text_layer_set_text(s_result_text_layer, num1);
}

//Switch the number's sign.
static void switch_sign(){
  char *str_num = operator_entered ? num2 : num1; //Get pointer to currently edited number string
  double num = str_to_double(str_num); //Convert to number
  num = -num; //Multiply by -1
  double_to_str(num, str_num, MAX_LENGTH); //Convert back to string
  text_layer_set_text(s_result_text_layer, str_num); //Display number
}

//Calculate result, display it and reset
static void calculate(){
  //Convert operands to numbers
  double lhs = str_to_double(num1);
  double rhs = str_to_double(num2);
  double result = 0.0;
  int exponent = 0;
  int64_t lhs_int = 0;
  int64_t rhs_int = 0;
  int64_t int_result = 0;
  bool lhs_is_int = parse_int64_str(num1, &lhs_int);
  bool rhs_is_int = parse_int64_str(num2, &rhs_int);
  bool used_int = false;
  bool overflow = false;
  APP_LOG(APP_LOG_LEVEL_DEBUG, "calc: %s %s %s", num1, operator_symbol(operator), num2);
  //Calculate the result
  switch(operator){
    // Addition
    case 0:
      if (lhs_is_int && rhs_is_int && int64_add_safe(lhs_int, rhs_int, &int_result)) {
        result = (double)int_result;
        used_int = true;
      } else if (lhs_is_int && rhs_is_int) {
        overflow = true;
      } else {
        result = lhs + rhs;
      }
      break;
    // Subtraction
    case 1:
      if (lhs_is_int && rhs_is_int && int64_sub_safe(lhs_int, rhs_int, &int_result)) {
        result = (double)int_result;
        used_int = true;
      } else if (lhs_is_int && rhs_is_int) {
        overflow = true;
      } else {
        result = lhs - rhs;
      }
      break;
    // Multiplication
    case 2:
      if (lhs_is_int && rhs_is_int && int64_mul_safe(lhs_int, rhs_int, &int_result)) {
        result = (double)int_result;
        used_int = true;
      } else if (lhs_is_int && rhs_is_int) {
        overflow = true;
      } else {
        result = lhs * rhs;
      }
      break;
    // Division
    case 3:
      if ((rhs_is_int && rhs_int == 0) || (!rhs_is_int && rhs == 0.0)) {
        APP_LOG(APP_LOG_LEVEL_DEBUG, "calc result: divide by 0");
        text_layer_set_text(s_result_text_layer, "Divide by 0");
        *num1 = 0;
        *num2 = 0;
        operator_entered = false;
        return;
      }
      if (lhs_is_int && rhs_is_int && (lhs_int % rhs_int) == 0) {
        result = (double)(lhs_int / rhs_int);
        int_result = lhs_int / rhs_int;
        used_int = true;
      } else {
        result = lhs / rhs;
      }
      break;
    // Exponentiation
    case 4:
      exponent = (int)rhs; //Exponent must be an int
      result = pow_int(lhs, exponent);
      break;
    default:
      result = 0.0;
  }

  char lhs_text[32];
  char rhs_text[32];
  char result_value_text[32];
  if (lhs_is_int) {
    int64_to_str(lhs_int, lhs_text, sizeof(lhs_text));
  } else {
    double_to_str(lhs, lhs_text, sizeof(lhs_text));
  }
  if (rhs_is_int) {
    int64_to_str(rhs_int, rhs_text, sizeof(rhs_text));
  } else {
    double_to_str(rhs, rhs_text, sizeof(rhs_text));
  }
  if (overflow) {
    strncpy(result_value_text, "Overflow", sizeof(result_value_text));
    result_value_text[sizeof(result_value_text) - 1] = '\0';
  } else if (used_int) {
    int64_to_str(int_result, result_value_text, sizeof(result_value_text));
  } else {
    double_to_str(result, result_value_text, sizeof(result_value_text));
  }
  APP_LOG(APP_LOG_LEVEL_DEBUG, "values: %s %s %s", lhs_text, operator_symbol(operator), rhs_text);
  APP_LOG(APP_LOG_LEVEL_DEBUG, "result value: %s", result_value_text);

  //Reset operands, operator_entered and entering_decimal
  *num1 = 0;
  *num2 = 0;
  operator_entered = false;
  
  if (overflow) {
    strncpy(result_text, "Overflow", MAX_LENGTH);
    result_text[MAX_LENGTH - 1] = '\0';
  } else if (used_int) {
    int64_to_str(int_result, result_text, MAX_LENGTH);
  } else {
    double_to_str(result, result_text, MAX_LENGTH); //Convert result to string
  }

  if (!overflow && !fits_display(result_text)) {
    strncpy(result_text, "Overflow", MAX_LENGTH);
    result_text[MAX_LENGTH - 1] = '\0';
    overflow = true;
  }
  APP_LOG(APP_LOG_LEVEL_DEBUG, "calc result text: %s", result_text);
  text_layer_set_text(s_result_text_layer, result_text); //Display result
  strcpy(num1, result_text); //Copy result into num1
  num1_is_ans = true;
}

//Button press handler
static void press_selected_button(void){
  switch(selected_button){
    case 0: // +
      enter_operator(0);
      break;
    case 1: // -
      enter_operator(1);
      break;
    case 2: // *
      enter_operator(2);
      break;
    case 3: // /
      enter_operator(3);
      break;
    case 5:// ^
      enter_operator(4);
      break;
    case 6:// CE
      clear(true);
      break;
    case 7:// AC
      all_clear();
      break;
    case 11:// +-
      switch_sign();
      break;
    case 19:// =
      calculate();
      break;
    default:
      enter();
  }
}

//Button press handler
static void select_handler(ClickRecognizerRef recognizer, void *context){
  press_selected_button();
}

static bool point_in_rect(GPoint point, GRect rect) {
  return point.x >= rect.origin.x
    && point.y >= rect.origin.y
    && point.x < rect.origin.x + rect.size.w
    && point.y < rect.origin.y + rect.size.h;
}

static int8_t button_hit_test(GPoint point) {
  if (point.x < s_layout.grid_origin.x || point.y < s_layout.grid_origin.y) {
    return -1;
  }

  int16_t col = (point.x - s_layout.grid_origin.x) / s_layout.cell_w;
  int16_t row = (point.y - s_layout.grid_origin.y) / s_layout.cell_h;

  if (col < 0 || col > 3 || row < 0 || row > 4) {
    return -1;
  }

  int16_t btn_x = s_layout.grid_origin.x + (col * s_layout.cell_w) + s_layout.cell_pad;
  int16_t btn_y = s_layout.grid_origin.y + (row * s_layout.cell_h) + s_layout.cell_pad;

  if (point.x >= btn_x + s_layout.button_w || point.y >= btn_y + s_layout.button_h) {
    return -1;
  }

  return (int8_t)(row * 4 + col);
}

static void touch_handler(const TouchEvent *event, void *context) {
  int8_t hit = button_hit_test(GPoint(event->x, event->y));
  GPoint point = GPoint(event->x, event->y);

  switch (event->type) {
    case TouchEvent_Touchdown:
      s_touch_down_button = hit;
      s_swipe_start_point = point;
      s_swipe_start_in_text = point_in_rect(point, s_layout.text_bounds);
      if (hit >= 0 && hit != selected_button) {
        selected_button = hit;
        layer_mark_dirty(s_buttons_layer);
      }
      break;
    case TouchEvent_PositionUpdate:
      if (hit >= 0 && hit != selected_button) {
        selected_button = hit;
        layer_mark_dirty(s_buttons_layer);
      }
      break;
    case TouchEvent_Liftoff:
      if (s_swipe_start_in_text) {
        int16_t dx = point.x - s_swipe_start_point.x;
        int16_t dy = point.y - s_swipe_start_point.y;
        if (abs(dx) >= 30 && abs(dy) <= 20) {
          clear(false);
          vibes_enqueue_custom_pattern(pat);
          s_touch_down_button = -1;
          s_swipe_start_in_text = false;
          break;
        }
      }
      if (hit >= 0 && hit == s_touch_down_button) {
        //vibes_short_pulse();
        vibes_enqueue_custom_pattern(pat);
        press_selected_button();
      }
      s_touch_down_button = -1;
      s_swipe_start_in_text = false;
      break;
  }
}

static void click_config_provider(void *context) {
  //Register click handlers
  window_single_repeating_click_subscribe(BUTTON_ID_UP, 100, up_down_handler);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 100, up_down_handler);
  window_single_click_subscribe(BUTTON_ID_SELECT, select_handler);
}

static void init(void) {
	// Create a window and get information about the window
	s_window = window_create();
  Layer *window_layer = window_get_root_layer(s_window);
  GRect bounds = layer_get_bounds(window_layer);
  layout_update(bounds);
  //Register click config provider
  window_set_click_config_provider(s_window, click_config_provider);
	
  // Result text layer setup
	s_result_text_layer = text_layer_create(s_layout.text_bounds);
	text_layer_set_text(s_result_text_layer, "");
	text_layer_set_font(s_result_text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
	text_layer_set_text_alignment(s_result_text_layer, GTextAlignmentRight);
	layer_add_child(window_get_root_layer(s_window), text_layer_get_layer(s_result_text_layer));
  
  //Button layer setup
  s_buttons_layer = layer_create(bounds);
  layer_set_update_proc(s_buttons_layer, button_layer_update);
  layer_add_child(window_get_root_layer(s_window), s_buttons_layer);

  if (touch_service_is_enabled()) {
    touch_service_subscribe(touch_handler, NULL);
    s_touch_subscribed = true;
  }

	// Push the window, setting the window animation to 'true'
	window_stack_push(s_window, true);
}

static void deinit(void) {
	if (s_touch_subscribed) {
    touch_service_unsubscribe();
    s_touch_subscribed = false;
  }
	// Destroy the text layer
	text_layer_destroy(s_result_text_layer);
  layer_destroy(s_buttons_layer);
	
	// Destroy the window
	window_destroy(s_window);
}

int main(void) {
	init();
	app_event_loop();
	deinit();
}
