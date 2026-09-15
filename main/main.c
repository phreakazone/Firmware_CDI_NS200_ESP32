#include "cdi_board_esp32.h"
#include "cdi_timebase.h"
#include "cdi_engine_esp32.h"
#include "cdi_ble_nimble.h"
#include "cdi_selftest.h" /* uji jitter tanpa osiloskop -- lihat INSTRUKSI.md */

#include "esp_log.h"
#include "esp_system.h"
#include "esp_pm.h"
#include "esp_task_wdt.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include <inttypes.h>

static const char *TAG = "cdi_main";

#define ENGINE_TASK_CORE   1        /* jauhkan dari core 0 (WiFi/BT protocol stack) */
#define ENGINE_TASK_PRIO   (configMAX_PRIORITIES - 2)
#define UART_CONSOLE_NUM   UART_NUM_0
#define UART_BUF_SIZE      512

/* ------------------------------------------------------------------ */
/*  Diagnostik reset/brownout: dicatat ke NVS supaya bisa dibaca lewat  */
/*  perintah protokol (mis. tambahkan handler "GET,RESETS" di           */
/*  cdi_r5_protocol.c bila mau) atau lewat log serial saat servis.       */
/* ------------------------------------------------------------------ */
static void log_and_count_reset_reason(void)
{
    esp_reset_reason_t r = esp_reset_reason();
    const char *name = "LAINNYA";
    switch (r) {
        case ESP_RST_POWERON:  name = "POWER-ON"; break;
        case ESP_RST_BROWNOUT: name = "BROWNOUT"; break;
        case ESP_RST_PANIC:    name = "PANIC/CRASH"; break;
        case ESP_RST_INT_WDT:  name = "INTERRUPT WATCHDOG"; break;
        case ESP_RST_TASK_WDT: name = "TASK WATCHDOG"; break;
        case ESP_RST_WDT:      name = "OTHER WATCHDOG"; break;
        case ESP_RST_SW:       name = "SOFTWARE RESET"; break;
        default: break;
    }
    ESP_LOGW(TAG, "Sebab reset terakhir: %s (%d)", name, (int)r);
    if (r == ESP_RST_BROWNOUT) {
        nvs_handle_t h;
        if (nvs_open("cdi_diag", NVS_READWRITE, &h) == ESP_OK) {
            uint32_t count = 0;
            nvs_get_u32(h, "bod_count", &count);
            nvs_set_u32(h, "bod_count", ++count);
            nvs_commit(h);
            nvs_close(h);
            ESP_LOGW(TAG, "Brownout resets tercatat sejak awal: %" PRIu32, count);
            ESP_LOGW(TAG, "-> Periksa BOM catu daya (bulk cap input, regulator, "
                          "TVS) sebelum menurunkan level BOD lebih jauh. Lihat "
                          "README.md bagian 'Brownout'.");
        }
    }
}

static void uart_console_task(void *arg)
{
    (void)arg;
    uint8_t rx[UART_BUF_SIZE];
    for (;;) {
        int n = uart_read_bytes(UART_CONSOLE_NUM, rx, sizeof(rx) - 1, pdMS_TO_TICKS(50));
        if (n <= 0) continue;
        rx[n] = 0;
        uint8_t reply[256];
        size_t rn = cdi_engine_handle_command(rx, (size_t)n, reply, sizeof(reply));
        if (rn) uart_write_bytes(UART_CONSOLE_NUM, (const char *)reply, rn);
    }
}

/* Task real-time 1 kHz. Prioritas tinggi + pin ke core 1 supaya tidak
 * digeser oleh scheduler saat WiFi/BT sibuk di core 0. Ini menangani
 * bagian NON-time-critical (ADC, charger, telemetry) -- penembakan busi
 * itu sendiri terjadi di ISR (cdi_engine_esp32.c), bukan di task ini,
 * sehingga jitter task ini (kalaupun ada beberapa ratus us saat NVS
 * commit) TIDAK memengaruhi derajat pengapian. */
static void engine_1khz_task(void *arg)
{
    (void)arg;
    esp_task_wdt_add(NULL);
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        cdi_engine_tick_1ms();
        esp_task_wdt_reset();
        vTaskDelayUntil(&last, pdMS_TO_TICKS(1));
    }
}

void app_main(void)
{
    log_and_count_reset_reason();

    /* Matikan dynamic frequency scaling / light-sleep otomatis: kita butuh
     * clock CPU & APB yang KONSTAN supaya konversi tick<->waktu di
     * cdi_timebase tidak melenceng dan supaya tidak ada jeda light-sleep
     * yang menambah jitter. Wajib CONFIG_PM_ENABLE=y dulu di sdkconfig
     * agar esp_pm_configure tersedia -- lihat sdkconfig.defaults. */
#if CONFIG_PM_ENABLE
    esp_pm_config_t pm_cfg = {
        .max_freq_mhz = 240,
        .min_freq_mhz = 240,
        .light_sleep_enable = false,
    };
    esp_pm_configure(&pm_cfg);
#endif

    cdi_board_gpio_init();
    cdi_board_adc_init();
    cdi_board_charger_pwm_init();
    cdi_timebase_init();
    cdi_engine_init();
    cdi_selftest_init();   /* HAPUS/comment baris ini untuk build produksi final di motor */
    cdi_ble_init();

    uart_driver_install(UART_CONSOLE_NUM, UART_BUF_SIZE * 2, 0, 0, NULL, 0);

    xTaskCreatePinnedToCore(engine_1khz_task, "cdi_1khz", 4096, NULL,
                             ENGINE_TASK_PRIO, NULL, ENGINE_TASK_CORE);
    xTaskCreatePinnedToCore(uart_console_task, "cdi_uart", 4096, NULL,
                             tskIDLE_PRIORITY + 1, NULL, 0);

    ESP_LOGI(TAG, "NS200 CDI ESP32 port siap. INI FIRMWARE HASIL PORTING -- "
                  "WAJIB uji bangku (simulator pickup) sebelum dipasang di motor.");
}