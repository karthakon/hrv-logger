#include <pebble.h>

// Minimal HRV transport harness for validating PebbleOS PR #1670.
// No filtering, no HRV math, no sleep staging. Just: does correctly
// scaled PPI arrive on the HRV event while subscribed, and does the
// sensor go back to HR-only when not. Build against the #1670 SDK tree.

static Window *s_window;
static TextLayer *s_text;
static char s_buf[240];

static bool s_sampling = false;
static uint16_t s_last_ppi = 0;
static uint16_t s_min_ppi = 0;
static uint16_t s_max_ppi = 0;
static uint16_t s_last_hr = 0;
static uint32_t s_hrv_events = 0;
static uint32_t s_hr_events = 0;
static uint32_t s_spo2_events = 0;
static uint8_t s_last_spo2 = 0;
static uint8_t s_last_spo2_q = 0;
static time_t s_start = 0;

// Mirror of hrv-monitor's gate (hrv_math.c) for comparable live stats.
#define LG_BUF_MAX 400
#define LG_STALE_SEC 10
static uint16_t s_ppi_buf[LG_BUF_MAX];
static uint16_t s_beats = 0;
static uint32_t s_rej = 0, s_rej_range = 0, s_rej_jump = 0;
static uint16_t s_gate_last = 0;
static uint32_t s_gate_last_time = 0;

static void prv_gate_reset(void) {
  s_beats = 0;
  s_rej = s_rej_range = s_rej_jump = 0;
  s_gate_last = 0;
  s_gate_last_time = 0;
}

static void prv_gate_add(uint16_t ppi_ms, uint32_t now) {
  if (ppi_ms < 300 || ppi_ms > 2000) { s_rej++; s_rej_range++; return; }
  bool fresh = (s_gate_last_time > 0) && ((now - s_gate_last_time) <= LG_STALE_SEC);
  if (s_gate_last > 0 && fresh) {
    uint32_t diff = (ppi_ms > s_gate_last) ? (ppi_ms - s_gate_last)
                                           : (s_gate_last - ppi_ms);
    if (diff * 5 > s_gate_last) { s_rej++; s_rej_jump++; return; }
  }
  if (s_beats < LG_BUF_MAX) {
    s_ppi_buf[s_beats++] = ppi_ms;
  } else {
    memmove(s_ppi_buf, s_ppi_buf + 1, (LG_BUF_MAX - 1) * sizeof(uint16_t));
    s_ppi_buf[LG_BUF_MAX - 1] = ppi_ms;
  }
  s_gate_last = ppi_ms;
  s_gate_last_time = now;
}

static uint32_t prv_isqrt(uint32_t n) {
  uint32_t x = n, y = (x + 1) / 2;
  if (n < 2) return n;
  while (y < x) { x = y; y = (x + n / x) / 2; }
  return x;
}

static uint16_t prv_rmssd(void) {
  if (s_beats < 2) return 0;
  uint64_t sumsq = 0;
  for (uint16_t i = 1; i < s_beats; i++) {
    int32_t d = (int32_t)s_ppi_buf[i] - (int32_t)s_ppi_buf[i - 1];
    sumsq += (uint64_t)(d * d);
  }
  return (uint16_t)prv_isqrt((uint32_t)(sumsq / (s_beats - 1)));
}

static void prv_health_handler(HealthEventType event, void *context) {
  if (event == HealthEventHeartRateUpdate) {
    s_hr_events++;
    HealthValue hr = health_service_peek_current_value(HealthMetricHeartRateRawBPM);
    if (hr > 0) s_last_hr = (uint16_t)hr;
  } else if ((int)event == 5) {  // HealthEventHRVUpdate
    s_hrv_events++;
    uint16_t ppi = (uint16_t)health_service_peek_hrv_ppi_ms();
    if (ppi > 0) {
      s_last_ppi = ppi;
      if (s_min_ppi == 0 || ppi < s_min_ppi) s_min_ppi = ppi;
      if (ppi > s_max_ppi) s_max_ppi = ppi;
      if (s_sampling) prv_gate_add(ppi, (uint32_t)time(NULL));
      APP_LOG(APP_LOG_LEVEL_INFO, "HRV %lu ppi=%u ms",
              (unsigned long)s_hrv_events, ppi);
    }
  } else if ((int)event == 6) {  // HealthEventSpO2Update
    s_spo2_events++;
    s_last_spo2 = health_service_peek_spo2_percent();
    s_last_spo2_q = health_service_peek_spo2_quality();
  }
}

static void prv_set_sampling(bool on) {
  if (on == s_sampling) return;
  s_sampling = on;
  if (on) {
    // HRV first (creates the subscription), then SpO2 (ORs its feature bit in).
    health_service_set_hrv_sample_period(1);
    health_service_set_spo2_sample_period(1);
  } else {
    // SpO2 off first: hrv off fully unsubscribes and would drop SpO2 with it.
    health_service_set_spo2_sample_period(0);
    health_service_set_hrv_sample_period(0);
  }
}

static void prv_tick(struct tm *tick_time, TimeUnits units) {
  uint32_t dur = s_start ? (uint32_t)(time(NULL) - s_start) : 0;
  snprintf(s_buf, sizeof(s_buf),
    "HRV LOGGER %s\nDur %lu:%02lu\n"
    "Beats %u  Rej %lu\nR/J %lu/%lu\n"
    "RMSSD %u\nPPI %u ms\n"
    "SpO2 %u%% q%u e%lu\n"
    "ev H%lu V%lu HR%u",
    s_sampling ? "ON" : "OFF",
    (unsigned long)(dur / 60), (unsigned long)(dur % 60),
    s_beats, (unsigned long)s_rej,
    (unsigned long)s_rej_range, (unsigned long)s_rej_jump,
    prv_rmssd(), s_last_ppi,
    s_last_spo2, s_last_spo2_q, (unsigned long)s_spo2_events,
    (unsigned long)s_hr_events, (unsigned long)s_hrv_events, s_last_hr);
  text_layer_set_text(s_text, s_buf);
}

static void prv_select_long(ClickRecognizerRef ref, void *ctx) {
  if (s_sampling) {
    prv_set_sampling(false);
  } else {
    s_start = time(NULL);
    s_hrv_events = 0;
    s_hr_events = 0;
    s_min_ppi = 0;
    s_max_ppi = 0;
    prv_gate_reset();
    prv_set_sampling(true);
  }
}

static void prv_click_config(void *ctx) {
  window_long_click_subscribe(BUTTON_ID_SELECT, 1500, prv_select_long, NULL);
}

static void prv_window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect b = layer_get_bounds(root);
  s_text = text_layer_create(GRect(4, 2, b.size.w - 8, b.size.h - 4));
  text_layer_set_font(s_text, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  text_layer_set_text(s_text, "HRV LOGGER\n\nHold Select\nto start");
  layer_add_child(root, text_layer_get_layer(s_text));
}

static void prv_window_unload(Window *window) {
  text_layer_destroy(s_text);
}

static void prv_init(void) {
  s_window = window_create();
  window_set_click_config_provider(s_window, prv_click_config);
  window_set_window_handlers(s_window, (WindowHandlers){
    .load = prv_window_load,
    .unload = prv_window_unload,
  });
  window_stack_push(s_window, true);
  health_service_events_subscribe(prv_health_handler, NULL);
  tick_timer_service_subscribe(SECOND_UNIT, prv_tick);
}

static void prv_deinit(void) {
  prv_set_sampling(false);
  tick_timer_service_unsubscribe();
  health_service_events_unsubscribe();
  window_destroy(s_window);
}

int main(void) {
  prv_init();
  app_event_loop();
  prv_deinit();
}
