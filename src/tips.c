#include "tips.h"
#include "utils.h"

#include <string.h>

static Window *s_tips_window;
static Layer *s_tips_controls_layer;
static TextLayer *s_tips_title_layer;
static TextLayer *s_tips_tip_label_layer;
static TextLayer *s_tips_tip_value_layer;
static TextLayer *s_tips_split_label_layer;
static TextLayer *s_tips_split_value_layer;
static TextLayer *s_tips_base_value_layer;
static TextLayer *s_tips_total_label_layer;
static TextLayer *s_tips_total_value_layer;
static TextLayer *s_tips_each_label_layer;
static TextLayer *s_tips_each_value_layer;

static GRect s_tip_box;
static GRect s_split_box;

static int s_tip_percent = 15;
static int s_split_count = 1;
static bool s_edit_tip = true;
static double s_base_value = 0.0;

static TipsTouchCallback s_enable_touch;

static char s_tip_percent_value_text[8];
static char s_split_value_text[8];
static char s_base_value_text[24];
static char s_tip_value_text[24];
static char s_total_value_text[24];
static char s_each_value_text[24];

static void append_suffix(char *buffer, size_t size, const char *suffix) {
  size_t len = strlen(buffer);
  if (len >= size - 1) {
    return;
  }

  while (*suffix && (len + 1 < size)) {
    buffer[len++] = *suffix++;
  }
  buffer[len] = '\0';
}

// Format a double as money, ensuring two decimal places and removing unnecessary zeros.
// This function only uses in Tips calculation, so it's here for now. 
// Maybe should be moved to utils.c if needed elsewhere.
static void format_money(double value, char *buffer, size_t size) {
  if (!buffer || size == 0) {
    return;
  }

  char temp[24];
  double_to_str(value, temp, sizeof(temp));

  strncpy(buffer, temp, size);
  buffer[size - 1] = '\0';

  char *dot = strchr(buffer, '.');
  if (!dot) {
    append_suffix(buffer, size, ".00");
    return;
  }

  size_t frac_len = strlen(dot + 1);
  if (frac_len == 0) {
    append_suffix(buffer, size, "00");
  } else if (frac_len == 1) {
    append_suffix(buffer, size, "0");
  }
}

static void update_control_colors(void) {
  if (!s_tips_tip_value_layer || !s_tips_tip_label_layer || !s_tips_split_value_layer
      || !s_tips_split_label_layer) {
    return;
  }
  GColor active_text = GColorWhite;
  GColor inactive_text = GColorBlack;

  text_layer_set_text_color(s_tips_tip_value_layer, s_edit_tip ? active_text : inactive_text);
  text_layer_set_text_color(s_tips_tip_label_layer, s_edit_tip ? active_text : inactive_text);
  text_layer_set_text_color(s_tips_split_value_layer, s_edit_tip ? inactive_text : active_text);
  text_layer_set_text_color(s_tips_split_label_layer, s_edit_tip ? inactive_text : active_text);
}

static void update_tips_text(void) {
    // Return early if layers aren't created yet
  if (!s_tips_tip_label_layer || !s_tips_tip_value_layer || !s_tips_split_value_layer
      || !s_tips_base_value_layer || !s_tips_total_value_layer || !s_tips_each_value_layer
      || !s_tips_controls_layer) {
    return;
  }
  if (s_split_count < 1) {
    s_split_count = 1;
  }
  if (s_tip_percent < 0) {
    s_tip_percent = 0;
  } else if (s_tip_percent > 100) {
    s_tip_percent = 100;
  }

  double tip_amount = s_base_value * ((double)s_tip_percent / 100.0);
  double total = s_base_value + tip_amount;
  double per_person = total / (double)s_split_count;

  snprintf(s_tip_percent_value_text, sizeof(s_tip_percent_value_text), "Tip %d%%", s_tip_percent);
  snprintf(s_split_value_text, sizeof(s_split_value_text), "x%d", s_split_count);

  format_money(tip_amount, s_tip_value_text, sizeof(s_tip_value_text));
  format_money(s_base_value, s_base_value_text, sizeof(s_base_value_text));
  format_money(total, s_total_value_text, sizeof(s_total_value_text));
  format_money(per_person, s_each_value_text, sizeof(s_each_value_text));

  text_layer_set_text(s_tips_tip_label_layer, s_tip_percent_value_text);
  text_layer_set_text(s_tips_tip_value_layer, s_tip_value_text);
  text_layer_set_text(s_tips_split_value_layer, s_split_value_text);
  text_layer_set_text(s_tips_base_value_layer, s_base_value_text);
  text_layer_set_text(s_tips_total_value_layer, s_total_value_text);
  text_layer_set_text(s_tips_each_value_layer, s_each_value_text);

  update_control_colors();
  layer_mark_dirty(s_tips_controls_layer);
}

static void tips_change_value(int delta) {
  if (s_edit_tip) {
    s_tip_percent += delta;
  } else {
    s_split_count += delta;
  }
  update_tips_text();
}

static void tips_up_handler(ClickRecognizerRef recognizer, void *context) {
  tips_change_value(1);
}

static void tips_down_handler(ClickRecognizerRef recognizer, void *context) {
  tips_change_value(-1);
}

static void tips_select_handler(ClickRecognizerRef recognizer, void *context) {
  s_edit_tip = !s_edit_tip;
  update_tips_text();
}

static void tips_back_handler(ClickRecognizerRef recognizer, void *context) {
  if (s_tips_window) {
    window_stack_remove(s_tips_window, true);
  }

  if (s_enable_touch) {
    s_enable_touch();
  }
}

static void tips_click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_UP, tips_up_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, tips_down_handler);
  window_single_click_subscribe(BUTTON_ID_SELECT, tips_select_handler);
  window_single_click_subscribe(BUTTON_ID_BACK, tips_back_handler);
}

static void tips_controls_update(Layer *layer, GContext *ctx) {
  graphics_context_set_stroke_color(ctx, GColorBlack);

  GColor active_fill = GColorBlack;
#if defined(PBL_COLOR)
  active_fill = GColorBlue;
#endif

  if (s_edit_tip) {
    graphics_context_set_fill_color(ctx, active_fill);
    graphics_fill_rect(ctx, s_tip_box, 4, GCornersAll);
    graphics_draw_round_rect(ctx, s_tip_box, 4);
    graphics_draw_round_rect(ctx, s_split_box, 4);
  } else {
    graphics_context_set_fill_color(ctx, active_fill);
    graphics_fill_rect(ctx, s_split_box, 4, GCornersAll);
    graphics_draw_round_rect(ctx, s_tip_box, 4);
    graphics_draw_round_rect(ctx, s_split_box, 4);
  }
}

static TextLayer *create_label_layer(GRect frame, GTextAlignment alignment, GFont font) {
  TextLayer *layer = text_layer_create(frame);
  text_layer_set_text_color(layer, GColorBlack);
  text_layer_set_background_color(layer, GColorWhite);
  text_layer_set_text_alignment(layer, alignment);
  text_layer_set_font(layer, font);
  return layer;
}

static void tips_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  window_set_background_color(window, GColorWhite);

  int16_t margin = 6;
  int16_t controls_top = 60;
  int16_t controls_height = 44;
  int16_t box_width = bounds.size.w - (margin * 2);

  s_tips_title_layer = create_label_layer(GRect(margin, margin, bounds.size.w - (margin * 2), 16),
                                     GTextAlignmentCenter, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text(s_tips_title_layer, "TIPS");
  layer_add_child(window_layer, text_layer_get_layer(s_tips_title_layer));

  // Base Value text layer setup
  s_tips_base_value_layer = create_label_layer(GRect(margin * 2, margin + 16, bounds.size.w - (margin * 4), 32), GTextAlignmentRight, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  layer_add_child(window_layer, text_layer_get_layer(s_tips_base_value_layer));

  s_tip_box = GRect(margin, controls_top, box_width, controls_height);
  s_split_box = GRect(margin, controls_top + controls_height + margin, box_width, controls_height);

  s_tips_controls_layer = layer_create(GRect(0, 0, bounds.size.w, controls_top + controls_height * 2 + margin * 2));
  layer_set_update_proc(s_tips_controls_layer, tips_controls_update);
  layer_add_child(window_layer, s_tips_controls_layer);

  s_tips_tip_label_layer = create_label_layer(GRect(s_tip_box.origin.x + margin, s_tip_box.origin.y + 4,
                                                   s_tip_box.size.w - margin, s_tip_box.size.h),
                                              GTextAlignmentLeft, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text(s_tips_tip_label_layer, "TIP:15%");
  text_layer_set_background_color(s_tips_tip_label_layer, GColorClear);
  layer_add_child(window_layer, text_layer_get_layer(s_tips_tip_label_layer));

  s_tips_tip_value_layer = create_label_layer(GRect(s_tip_box.origin.x + margin, s_tip_box.origin.y + margin,
                       s_tip_box.size.w - margin * 2, s_tip_box.size.h),
                                              GTextAlignmentRight,
                                              fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  text_layer_set_background_color(s_tips_tip_value_layer, GColorClear);
  layer_add_child(window_layer, text_layer_get_layer(s_tips_tip_value_layer));

  s_tips_split_label_layer = create_label_layer(GRect(s_split_box.origin.x + margin, s_split_box.origin.y + 4,
                                                     s_split_box.size.w - margin, s_split_box.size.h),
                                                GTextAlignmentLeft, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text(s_tips_split_label_layer, "SPLIT");
  text_layer_set_background_color(s_tips_split_label_layer, GColorClear);
  layer_add_child(window_layer, text_layer_get_layer(s_tips_split_label_layer));

  s_tips_split_value_layer = create_label_layer(GRect(s_split_box.origin.x + margin, s_split_box.origin.y + margin,
                         s_split_box.size.w - margin * 2, s_split_box.size.h),
                                                GTextAlignmentRight,
                                                fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  text_layer_set_background_color(s_tips_split_value_layer, GColorClear);
  layer_add_child(window_layer, text_layer_get_layer(s_tips_split_value_layer));

  int16_t results_top = controls_top + (controls_height + margin) * 2;
  int16_t row_height = 30;
  int16_t label_width = 56;
  int16_t value_width = bounds.size.w - (margin * 4) - label_width;

  s_tips_total_label_layer = create_label_layer(GRect(margin * 2, results_top + margin, label_width, row_height),
                                                GTextAlignmentLeft, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text(s_tips_total_label_layer, "TOTAL");
  layer_add_child(window_layer, text_layer_get_layer(s_tips_total_label_layer));

  s_tips_total_value_layer = create_label_layer(GRect(margin * 2 + label_width, results_top + margin,
                                                     value_width, row_height),
                                                GTextAlignmentRight, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  layer_add_child(window_layer, text_layer_get_layer(s_tips_total_value_layer));

  s_tips_each_label_layer = create_label_layer(GRect(margin * 2, results_top + row_height + margin,
                                                    label_width, row_height),
                                               GTextAlignmentLeft, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text(s_tips_each_label_layer, "EACH");
  layer_add_child(window_layer, text_layer_get_layer(s_tips_each_label_layer));

  s_tips_each_value_layer = create_label_layer(GRect(margin * 2 + label_width, results_top + row_height +margin,
                                                    value_width, row_height),
                                               GTextAlignmentRight, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  layer_add_child(window_layer, text_layer_get_layer(s_tips_each_value_layer));

  update_tips_text();
}

static void tips_window_unload(Window *window) {
  text_layer_destroy(s_tips_title_layer);
  text_layer_destroy(s_tips_tip_label_layer);
  text_layer_destroy(s_tips_tip_value_layer);
  text_layer_destroy(s_tips_split_label_layer);
  text_layer_destroy(s_tips_split_value_layer);
  text_layer_destroy(s_tips_base_value_layer);
  text_layer_destroy(s_tips_total_label_layer);
  text_layer_destroy(s_tips_total_value_layer);
  text_layer_destroy(s_tips_each_label_layer);
  text_layer_destroy(s_tips_each_value_layer);
  layer_destroy(s_tips_controls_layer);

  s_tips_title_layer = NULL;
  s_tips_tip_label_layer = NULL;
  s_tips_tip_value_layer = NULL;
  s_tips_split_label_layer = NULL;
  s_tips_split_value_layer = NULL;
  s_tips_base_value_layer = NULL;
  s_tips_total_label_layer = NULL;
  s_tips_total_value_layer = NULL;
  s_tips_each_label_layer = NULL;
  s_tips_each_value_layer = NULL;
  s_tips_controls_layer = NULL;
}

void tips_show(double base_value, TipsTouchCallback disable_touch, TipsTouchCallback enable_touch) {
  if (!s_tips_window) {
    s_tips_window = window_create();
    window_set_window_handlers(s_tips_window, (WindowHandlers){
      .load = tips_window_load,
      .unload = tips_window_unload,
    });
    window_set_click_config_provider(s_tips_window, tips_click_config_provider);
  }

  s_enable_touch = enable_touch;
  if (disable_touch) {
    disable_touch();
  }

  s_base_value = base_value;
  window_stack_push(s_tips_window, true);
}

void tips_deinit(void) {
  if (s_tips_window) {
    window_destroy(s_tips_window);
    s_tips_window = NULL;
  }
}
