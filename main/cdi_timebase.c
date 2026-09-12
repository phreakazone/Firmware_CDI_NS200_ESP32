#include "cdi_timebase.h"

#include "driver/gptimer.h"
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include <string.h>

#define CDI_TB_RESOLUTION_HZ 1000000u /* 1 tick = 1 us, cukup untuk 0.06 derajat @10000 rpm */
#define CDI_TB_NO_TARGET UINT64_MAX

typedef struct {
    volatile bool armed;
    volatile uint64_t target_tick;
    cdi_tb_slot_cb_t cb;
    void *ctx;
} cdi_tb_slot_state_t;

static gptimer_handle_t s_timer;
static cdi_tb_slot_state_t s_slots[CDI_TB_SLOT_COUNT];
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

/* Cari target pending terdekat lalu muat ke register alarm hardware.
 * Dipanggil selalu dari dalam critical section (ISR atau task). */
static void IRAM_ATTR reload_alarm_locked(void)
{
    uint64_t nearest = CDI_TB_NO_TARGET;
    for (int i = 0; i < CDI_TB_SLOT_COUNT; ++i) {
        if (s_slots[i].armed && s_slots[i].target_tick < nearest) {
            nearest = s_slots[i].target_tick;
        }
    }
    if (nearest == CDI_TB_NO_TARGET) {
        gptimer_set_alarm_action(s_timer, NULL); /* tidak ada yang pending: alarm off */
        return;
    }
    gptimer_alarm_config_t alarm = {
        .alarm_count = nearest,
        .reload_count = 0,
        .flags.auto_reload_on_alarm = false,
    };
    gptimer_set_alarm_action(s_timer, &alarm);
}

static bool IRAM_ATTR on_alarm(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_ctx)
{
    (void)timer; (void)user_ctx;
    uint64_t now = edata->count_value;
    portENTER_CRITICAL_ISR(&s_lock);
    for (int i = 0; i < CDI_TB_SLOT_COUNT; ++i) {
        cdi_tb_slot_state_t *s = &s_slots[i];
        if (!s->armed || s->target_tick > now) continue;
        uint64_t next = 0;
        bool reschedule = s->cb ? s->cb(s->ctx, s->target_tick, &next) : false;
        if (reschedule) {
            s->target_tick = next;
            s->armed = true;
        } else {
            s->armed = false;
        }
    }
    reload_alarm_locked();
    portEXIT_CRITICAL_ISR(&s_lock);
    return false; /* tidak butuh yield task ke penjadwal */
}

void cdi_timebase_init(void)
{
    memset(s_slots, 0, sizeof(s_slots));
    gptimer_config_t cfg = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = CDI_TB_RESOLUTION_HZ,
    };
    ESP_ERROR_CHECK(gptimer_new_timer(&cfg, &s_timer));
    gptimer_event_callbacks_t cbs = { .on_alarm = on_alarm };
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(s_timer, &cbs, NULL));
    ESP_ERROR_CHECK(gptimer_enable(s_timer));
    ESP_ERROR_CHECK(gptimer_start(s_timer));
}

uint64_t IRAM_ATTR cdi_timebase_now(void)
{
    uint64_t v = 0;
    gptimer_get_raw_count(s_timer, &v);
    return v;
}

void cdi_timebase_set_slot_handler(cdi_tb_slot_t slot, cdi_tb_slot_cb_t cb, void *ctx)
{
    if (slot >= CDI_TB_SLOT_COUNT) return;
    s_slots[slot].cb = cb;
    s_slots[slot].ctx = ctx;
}

void IRAM_ATTR cdi_timebase_arm(cdi_tb_slot_t slot, uint64_t target_tick)
{
    if (slot >= CDI_TB_SLOT_COUNT) return;
    portENTER_CRITICAL_SAFE(&s_lock);
    s_slots[slot].target_tick = target_tick;
    s_slots[slot].armed = true;
    reload_alarm_locked();
    portEXIT_CRITICAL_SAFE(&s_lock);
}

void IRAM_ATTR cdi_timebase_disarm(cdi_tb_slot_t slot)
{
    if (slot >= CDI_TB_SLOT_COUNT) return;
    portENTER_CRITICAL_SAFE(&s_lock);
    s_slots[slot].armed = false;
    reload_alarm_locked();
    portEXIT_CRITICAL_SAFE(&s_lock);
}
