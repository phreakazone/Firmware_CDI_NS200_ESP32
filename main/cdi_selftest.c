#include "cdi_selftest.h"
#include "cdi_timebase.h"
#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdbool.h>
#include <inttypes.h>

#define CDI_PIN_SELFTEST_LOOPBACK GPIO_NUM_5

static const char *TAG = "cdi_selftest";
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static volatile uint64_t s_last_due;
static volatile bool s_have_due;

static volatile uint32_t s_count;
static volatile int64_t s_sum_us;
static volatile int64_t s_min_us = INT64_MAX;
static volatile int64_t s_max_us = INT64_MIN;

void IRAM_ATTR cdi_selftest_mark_center_due(uint64_t due_tick)
{
    s_last_due = due_tick;
    s_have_due = true;
}

static void IRAM_ATTR loopback_isr(void *arg)
{
    (void)arg;
    if (!s_have_due) return;
    uint64_t now = cdi_timebase_now();
    int64_t err_us = (int64_t)now - (int64_t)s_last_due;
    s_have_due = false;

    portENTER_CRITICAL_ISR(&s_lock);
    s_count++;
    s_sum_us += err_us;
    if (err_us < s_min_us) s_min_us = err_us;
    if (err_us > s_max_us) s_max_us = err_us;
    portEXIT_CRITICAL_ISR(&s_lock);
}

static void selftest_report_task(void *arg)
{
    (void)arg;
    ESP_LOGW(TAG, "SELF-TEST AKTIF -- pastikan kabel jumper GPIO%d(gate)->GPIO%d(loopback) "
                   "HANYA terpasang saat uji meja, LEPAS sebelum dipasang ke motor.",
              (int)25, (int)CDI_PIN_SELFTEST_LOOPBACK);
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        uint32_t count; int64_t sum, mn, mx;
        portENTER_CRITICAL(&s_lock);
        count = s_count; sum = s_sum_us; mn = s_min_us; mx = s_max_us;
        s_count = 0; s_sum_us = 0; s_min_us = INT64_MAX; s_max_us = INT64_MIN;
        portEXIT_CRITICAL(&s_lock);

        if (count == 0) {
            /* FIX: Pesan log diperjelas agar tidak salah pin */
            ESP_LOGI(TAG, "Menunggu pulsa... Pastikan f-Generator GM328A masuk ke GPIO4 (Pickup) DAN jumper GPIO25->GPIO5 terpasang.");
            continue;
        }
        int64_t mean = sum / (int64_t)count;
        int64_t spread = mx - mn;
        ESP_LOGI(TAG, "n=%u  rata2=%" PRId64 "us  min=%" PRId64 "us  max=%" PRId64
                      "us  JITTER(spread)=%" PRId64 "us",
                 (unsigned)count, mean, mn, mx, spread);
    }
}

void cdi_selftest_init(void)
{
    esp_err_t err = gpio_install_isr_service(ESP_INTR_FLAG_IRAM | ESP_INTR_FLAG_LEVEL3);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(err);
    }

    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << CDI_PIN_SELFTEST_LOOPBACK,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_POSEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
    ESP_ERROR_CHECK(gpio_isr_handler_add(CDI_PIN_SELFTEST_LOOPBACK, loopback_isr, NULL));

    xTaskCreatePinnedToCore(selftest_report_task, "cdi_selftest", 3072, NULL, 2, NULL, 0);
}