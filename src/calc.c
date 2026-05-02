#include <pebble.h>
#include <limits.h>
#include "fixed.h"

#define MAX_LENGTH 14

//Layers
static Window *s_window;
static TextLayer *s_result_text_layer;
static Layer *s_buttons_layer;
static int8_t s_touch_down_button = -1;
static bool s_touch_subscribed = false;

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
  "0","^","C","CE",
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

static void enter(){
  char *num = operator_entered ? num2 : num1; //Create a pointer to the currnetly edited number
  const char *label = buttons[selected_button];
  bool is_decimal = (label[0] == '.' && label[1] == '\0');

  if (is_decimal && has_decimal_point(num)) {
    return;
  }

  if (!is_decimal && has_decimal_point(num) && count_fractional_digits(num) >= FIXED_SCALE_DIGITS) {
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

//Switch the number's sign.
static void switch_sign(){
  bool overflow = false; //Overflow flag
  char *str_num = operator_entered ? num2 : num1; //Get pointer to currently edited number string
  fixed num = str_to_fixed(str_num, &overflow); //Convert to number
  num = fixed_mult(num, int_to_fixed(-1), &overflow); //Multiply by -1
  if(!overflow){
    fixed_repr(num, str_num, MAX_LENGTH); //Convert back to string
    text_layer_set_text(s_result_text_layer, str_num); //Display number
  }
}

//Calculate result, display it and reset
static void calculate(){
  bool overflow = false; //Overflow flag
  //Convert operands to numbers
  fixed lhs = str_to_fixed(num1, &overflow);
  fixed rhs = str_to_fixed(num2, &overflow);
  fixed result = 0;
  //Calculate the result
  switch(operator){
    case 0:
      result = fixed_add(lhs, rhs, &overflow);
      break;
    case 1:
      result = fixed_subt(lhs, rhs, &overflow);
      break;
    case 2:
      result = fixed_mult(lhs, rhs, &overflow);
      break;
    case 3:
      if (rhs == 0) {
        text_layer_set_text(s_result_text_layer, "Divide by 0");
        *num1 = 0;
        *num2 = 0;
        operator_entered = false;
        return;
      }
      result = fixed_div(lhs, rhs);
      break;
    case 4:
      result = fixed_pow(lhs, fixed_to_int(rhs), &overflow); //Exponent must be an int
      break;
    default:
      result = 0;
  }

  APP_LOG(APP_LOG_LEVEL_DEBUG, "num1: %d num2: %d result: %d", lhs, rhs, result);

  //Reset operands, operator_entered and entering_decimal
  *num1 = 0;
  *num2 = 0;
  operator_entered = false;
  
  if(overflow){
    text_layer_set_text(s_result_text_layer, "Overflow Error"); //Display message on overflow
  }
  else{
    fixed_repr(result, result_text, MAX_LENGTH); //Convert result to string
    text_layer_set_text(s_result_text_layer, result_text); //Display result
    strcpy(num1, result_text); //Copy result into num1
    num1_is_ans = true;
  }
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
    case 6:// C
      clear(false);
      break;
    case 7:// CE
      clear(true);
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

  switch (event->type) {
    case TouchEvent_Touchdown:
      s_touch_down_button = hit;
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
      if (hit >= 0 && hit == s_touch_down_button) {
        //vibes_short_pulse();
        vibes_enqueue_custom_pattern(pat);
        press_selected_button();
      }
      s_touch_down_button = -1;
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
