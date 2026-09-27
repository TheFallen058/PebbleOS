/* SPDX-FileCopyrightText: 2026 Aliaksandr Karnilovich */
/* SPDX-License-Identifier: Apache-2.0 */

#include "apps/system/health/data_private.h"
#include "apps/system/health/weight_detail_card.h"
#include "apps/system/health/weight_entry_ui.h"
#include "apps/system/health/weight_entry_window.h"
#include "apps/system/health/weight_summary_card.h"

#include "test_health_app_includes.h"

static GContext s_ctx;
static FrameBuffer s_fb;

GContext *graphics_context_get_current_context(void) {
  return &s_ctx;
}

void health_weight_entry_window_push(uint16_t initial_weight_dag,
                                     WeightEntrySavedCallback saved_callback, void *context) {
}

bool clock_is_24h_style(void) {
  return true;
}

void test_health_weight_card__initialize(void) {
  shell_prefs_set_units_distance(UnitsDistance_Miles);
  rtc_set_time(1704557975);

  framebuffer_init(&s_fb, &(GSize){DISP_COLS, DISP_ROWS});
  framebuffer_clear(&s_fb);
  graphics_context_init(&s_ctx, &s_fb, GContextInitializationMode_App);
  s_app_state_get_graphics_context = &s_ctx;

  fake_spi_flash_init(0, 0x1000000);
  pfs_init(false);
  pfs_format(true);
  load_resource_fixture_in_flash(RESOURCES_FIXTURE_PATH, SYSTEM_RESOURCES_FIXTURE_NAME, false);
  resource_init();

  ContentIndicatorsBuffer *buffer = content_indicator_get_current_buffer();
  content_indicator_init_buffer(buffer);
}

void test_health_weight_card__cleanup(void) {
}

void test_health_weight_card__dragging_upper_value_down_increases_weight(void) {
  cl_assert_equal_i(health_weight_entry_value_from_drag(773, 18), 774);
}

void test_health_weight_card__dragging_lower_value_up_decreases_weight(void) {
  cl_assert_equal_i(health_weight_entry_value_from_drag(773, -18), 772);
}

static void prv_render_summary(HealthData *health_data) {
  Window window;
  window_init(&window, WINDOW_NAME("Weight Summary"));
  Layer *window_layer = window_get_root_layer(&window);
  Layer *card_layer = health_weight_summary_card_create(health_data);
  layer_set_frame(card_layer, &window_layer->bounds);
  layer_add_child(window_layer, card_layer);
  window_set_background_color(&window, health_weight_summary_card_get_bg_color(card_layer));
  window_set_on_screen(&window, true, true);
  window_render(&window, &s_ctx);
  health_weight_summary_card_destroy(card_layer);
  window_deinit(&window);
}

static void prv_render_detail(HealthData *health_data) {
  Window *window = health_weight_detail_card_create(health_data);
  window_set_on_screen(window, true, true);
  window_render(window, &s_ctx);
  window_set_on_screen(window, false, false);
  health_weight_detail_card_destroy(window);
}

void test_health_weight_card__profile_only(void) {
  prv_render_summary(&(HealthData){.profile_weight_dag = 7730});
  cl_check(gbitmap_pbi_eq(&s_ctx.dest_bitmap, TEST_PBI_FILE));
}

void test_health_weight_card__latest_entry(void) {
  HealthData health_data = {
    .profile_weight_dag = 7730,
    .weight_samples = {{.utc_sec = 1704557975, .weight_dag = 7710}},
    .weight_sample_count = 1,
  };
  prv_render_summary(&health_data);
  cl_check(gbitmap_pbi_eq(&s_ctx.dest_bitmap, TEST_PBI_FILE));
}

void test_health_weight_card__entry_metric(void) {
  health_weight_entry_ui_draw(&s_ctx, &s_ctx.dest_bitmap.bounds, 773, 300, 2000, "ADD WEIGHT",
                              "kg", FONT_KEY_BITHAM_34_MEDIUM_NUMBERS);
  cl_check(gbitmap_pbi_eq(&s_ctx.dest_bitmap, TEST_PBI_FILE));
}

void test_health_weight_card__entry_imperial(void) {
  health_weight_entry_ui_draw(&s_ctx, &s_ctx.dest_bitmap.bounds, 1704, 661, 4409, "ADD WEIGHT",
                              "lb", FONT_KEY_BITHAM_34_MEDIUM_NUMBERS);
  cl_check(gbitmap_pbi_eq(&s_ctx.dest_bitmap, TEST_PBI_FILE));
}

void test_health_weight_card__detail_empty(void) {
  prv_render_detail(&(HealthData){.profile_weight_dag = 7730});
  cl_check(gbitmap_pbi_eq(&s_ctx.dest_bitmap, TEST_PBI_FILE));
}

void test_health_weight_card__history(void) {
  HealthData health_data = {
    .profile_weight_dag = 7730,
    .weight_samples = {
      {.utc_sec = 1704557975, .weight_dag = 7700},
      {.utc_sec = 1704385175, .weight_dag = 7685},
      {.utc_sec = 1704212375, .weight_dag = 7690},
      {.utc_sec = 1703953175, .weight_dag = 7680},
      {.utc_sec = 1703693975, .weight_dag = 7720},
      {.utc_sec = 1702829975, .weight_dag = 7710},
      {.utc_sec = 1702052375, .weight_dag = 7740},
    },
    .weight_sample_count = 7,
  };
  prv_render_detail(&health_data);
  cl_check(gbitmap_pbi_eq(&s_ctx.dest_bitmap, TEST_PBI_FILE));
}
