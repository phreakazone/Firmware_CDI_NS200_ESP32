#include "cdi_engine_esp32.h"
#include "cdi_ble_nimble.h"
#include "cdi_r5_ble.h"

#include "esp_log.h"
#include <stdlib.h>
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "cdi_ble";
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_telem_val_handle, s_resp_val_handle, s_ota_status_val_handle;

#define COMMAND_QUEUE_DEPTH 8u
#define COMMAND_WORKER_STACK 4096u
#define COMMAND_WORKER_PRIORITY 5u

typedef struct {
    uint16_t conn_handle;
    uint16_t length;
    uint8_t data[256];
} command_job_t;

static QueueHandle_t s_command_queue;

static ble_uuid128_t uuid_from_str(const char *s);

static void notify_response(uint16_t conn_handle, const uint8_t *reply, size_t length)
{
    if (length == 0u || conn_handle == BLE_HS_CONN_HANDLE_NONE ||
        conn_handle != s_conn_handle) return;
    struct os_mbuf *om = ble_hs_mbuf_from_flat(reply, (uint16_t)length);
    if (om != NULL) {
        (void)ble_gatts_notify_custom(conn_handle, s_resp_val_handle, om);
    }
}

/*
 * SETUP/MAP commands may commit NVS. Flash commits must never run inside the
 * NimBLE GATT access callback: doing so can starve the host and make Android
 * report a disconnect/reconnect exactly while confirming installation.
 */
static void command_worker_task(void *arg)
{
    (void)arg;
    command_job_t job;
    uint8_t reply[256];
    for (;;) {
        if (xQueueReceive(s_command_queue, &job, portMAX_DELAY) != pdTRUE) continue;
        size_t n = cdi_engine_handle_command(job.data, job.length, reply, sizeof(reply));
        notify_response(job.conn_handle, reply, n);
    }
}

static int command_write_cb(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle; (void)attr_handle; (void)arg;
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return 0;
    command_job_t job = {
        .conn_handle = conn_handle,
        .length = OS_MBUF_PKTLEN(ctxt->om),
    };
    if (job.length == 0u || job.length > sizeof(job.data) ||
        ble_hs_mbuf_to_flat(ctxt->om, job.data, job.length, NULL) != 0) {
        ESP_LOGW(TAG, "Frame command kosong/terlalu panjang; diabaikan");
        return 0;
    }
    if (s_command_queue == NULL ||
        xQueueSend(s_command_queue, &job, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Command queue penuh; frame ditolak tanpa memutus BLE");
    }
    return 0;
}

static int ota_data_write_cb(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return 0;
    uint8_t buf[256];
    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    if (len > sizeof(buf)) len = sizeof(buf);
    ble_hs_mbuf_to_flat(ctxt->om, buf, len, NULL);

    cdi_engine_handle_ota_data(buf, len);
    return 0;
}

static int passive_read_cb(uint16_t conn_handle, uint16_t attr_handle,
                            struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle; (void)attr_handle; (void)ctxt; (void)arg;
    return 0; 
}

static ble_uuid128_t g_uuid_svc, g_uuid_telem, g_uuid_cmd, g_uuid_resp,
                      g_uuid_ota_data, g_uuid_ota_status;

static struct ble_gatt_chr_def g_chrs[6];
static struct ble_gatt_svc_def g_svcs[2];

static void gatt_svr_init(void)
{
    g_uuid_svc = uuid_from_str(CDI_R5_BLE_SERVICE_UUID);
    g_uuid_telem = uuid_from_str(CDI_R5_BLE_TELEM_UUID);
    g_uuid_cmd = uuid_from_str(CDI_R5_BLE_COMMAND_UUID);
    g_uuid_resp = uuid_from_str(CDI_R5_BLE_RESPONSE_UUID);
    g_uuid_ota_data = uuid_from_str(CDI_R8_BLE_OTA_DATA_UUID);
    g_uuid_ota_status = uuid_from_str(CDI_R8_BLE_OTA_STATUS_UUID);

    g_chrs[0] = (struct ble_gatt_chr_def){
        .uuid = &g_uuid_telem.u, .access_cb = passive_read_cb,
        .val_handle = &s_telem_val_handle, .flags = BLE_GATT_CHR_F_NOTIFY,
    };
    g_chrs[1] = (struct ble_gatt_chr_def){
        .uuid = &g_uuid_cmd.u, .access_cb = command_write_cb,
        .flags = BLE_GATT_CHR_F_WRITE,
    };
    g_chrs[2] = (struct ble_gatt_chr_def){
        .uuid = &g_uuid_resp.u, .access_cb = passive_read_cb,
        .val_handle = &s_resp_val_handle, .flags = BLE_GATT_CHR_F_NOTIFY,
    };
    g_chrs[3] = (struct ble_gatt_chr_def){
        .uuid = &g_uuid_ota_status.u, .access_cb = passive_read_cb,
        .val_handle = &s_ota_status_val_handle, .flags = BLE_GATT_CHR_F_NOTIFY,
    };
    g_chrs[4] = (struct ble_gatt_chr_def){
        .uuid = &g_uuid_ota_data.u, .access_cb = ota_data_write_cb,
        .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
    };
    g_chrs[5] = (struct ble_gatt_chr_def){0};
    
    g_svcs[0] = (struct ble_gatt_svc_def){
        .type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &g_uuid_svc.u, .characteristics = g_chrs,
    };
    g_svcs[1] = (struct ble_gatt_svc_def){ .type = 0 };

    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_gatts_count_cfg(g_svcs);
    ble_gatts_add_svcs(g_svcs);
}

static int gap_event_cb(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        s_conn_handle = event->connect.status == 0 ? event->connect.conn_handle : BLE_HS_CONN_HANDLE_NONE;
        cdi_engine_set_ble_connected(s_conn_handle != BLE_HS_CONN_HANDLE_NONE);
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        cdi_engine_set_ble_connected(false);
        cdi_ble_start_advertising();
        return 0;
    default:
        return 0;
    }
}

void cdi_ble_start_advertising(void)
{
    struct ble_gap_adv_params adv = { .conn_mode = BLE_GAP_CONN_MODE_UND, .disc_mode = BLE_GAP_DISC_MODE_GEN };
    struct ble_hs_adv_fields fields = {0};
    
    fields.name = (const uint8_t *)"NS200-CDI";
    fields.name_len = strlen("NS200-CDI");
    fields.name_is_complete = 1;
    ble_gap_adv_set_fields(&fields);

    struct ble_hs_adv_fields scan_rsp = {0};
    scan_rsp.uuids128 = &g_uuid_svc;
    scan_rsp.num_uuids128 = 1;
    scan_rsp.uuids128_is_complete = 1;
    ble_gap_adv_rsp_set_fields(&scan_rsp);

    ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER, &adv, gap_event_cb, NULL);
}

static void on_sync_cb(void)
{
    ESP_LOGI(TAG, "NimBLE sync, advertising as NS200-CDI");
    cdi_ble_start_advertising();
}

static void nimble_host_task(void *param)
{
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void cdi_ble_init(void)
{
    s_command_queue = xQueueCreate(COMMAND_QUEUE_DEPTH, sizeof(command_job_t));
    if (s_command_queue == NULL) {
        ESP_LOGE(TAG, "Gagal membuat command queue BLE");
        return;
    }
    if (xTaskCreate(command_worker_task, "cdi_ble_cmd", COMMAND_WORKER_STACK,
                    NULL, COMMAND_WORKER_PRIORITY, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Gagal membuat command worker BLE");
        vQueueDelete(s_command_queue);
        s_command_queue = NULL;
        return;
    }
    nimble_port_init();
    gatt_svr_init();
    ble_hs_cfg.sync_cb = on_sync_cb;
    nimble_port_freertos_init(nimble_host_task);
}

static ble_uuid128_t uuid_from_str(const char *s)
{
    ble_uuid128_t u = { .u.type = BLE_UUID_TYPE_128 };
    uint8_t bytes[16]; int bi = 15;
    for (const char *p = s; *p && bi >= 0; ) {
        if (*p == '-') { ++p; continue; }
        char hex[3] = { p[0], p[1], 0 };
        bytes[bi--] = (uint8_t)strtol(hex, NULL, 16);
        p += 2;
    }
    memcpy(u.value, bytes, sizeof(bytes));
    return u;
}

void cdi_ble_notify_ota_status(const uint8_t *data, uint16_t len)
{
    if (s_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
        if (om != NULL) { /* FIX: Cegah Memory Leak */
            ble_gatts_notify_custom(s_conn_handle, s_ota_status_val_handle, om);
        }
    }
}

/* FIX: Fungsi pengiriman telemetri yang sebelumnya hilang */
void cdi_ble_notify_telemetry(const uint8_t *data, uint16_t len)
{
    if (s_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
        if (om == NULL) return; /* FIX: Abaikan jika RAM penuh */
        ble_gatts_notify_custom(s_conn_handle, s_telem_val_handle, om);
    }
}