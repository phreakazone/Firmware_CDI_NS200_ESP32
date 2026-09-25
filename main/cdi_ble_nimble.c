#include "cdi_engine_esp32.h"
#include "cdi_ble_nimble.h"
#include "cdi_r5_ble.h"

#include "esp_log.h"
#include <stdlib.h>
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nimble/nimble_npl.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "cdi_ble";
static volatile uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_telem_val_handle, s_resp_val_handle, s_ota_status_val_handle;

#define BLE_WORK_QUEUE_DEPTH 16u
#define COMMAND_WORKER_STACK 8192u
#define COMMAND_WORKER_PRIORITY 5u
#define TX_PRIORITY_QUEUE_DEPTH 8u
#define TX_PAYLOAD_MAX 256u
#define TX_HOST_BURST 8u

enum {
    WORK_KIND_COMMAND = 0,
    WORK_KIND_OTA_DATA = 1,
};

enum {
    TX_KIND_TELEMETRY = 0,
    TX_KIND_RESPONSE = 1,
    TX_KIND_OTA = 2,
};

typedef struct {
    uint16_t conn_handle;
    uint16_t length;
    uint8_t kind;
    uint8_t data[256];
} ble_work_job_t;

typedef struct {
    uint16_t conn_handle;
    uint16_t value_handle;
    uint16_t length;
    uint8_t kind;
    uint8_t data[TX_PAYLOAD_MAX];
} tx_job_t;

static QueueHandle_t s_work_queue;
static QueueHandle_t s_tx_priority_queue;
static QueueHandle_t s_tx_telemetry_queue;
static struct ble_npl_event s_tx_event;
static portMUX_TYPE s_tx_event_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_tx_event_ready;
static bool s_tx_event_pending;
static volatile bool s_command_busy;

static ble_uuid128_t uuid_from_str(const char *s);

static void tx_schedule_host_event(void)
{
    bool post = false;
    if (!s_tx_event_ready) return;

    portENTER_CRITICAL(&s_tx_event_lock);
    if (!s_tx_event_pending) {
        s_tx_event_pending = true;
        post = true;
    }
    portEXIT_CRITICAL(&s_tx_event_lock);

    if (post) {
        /*
         * The default NimBLE queue is consumed by nimble_port_run().  Keeping
         * ble_gatts_notify_custom() there prevents application tasks from
         * entering VHCI concurrently.
         */
        ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &s_tx_event);
    }
}

static void tx_send_in_host(const tx_job_t *job)
{
    if (job == NULL || job->length == 0u ||
        job->conn_handle == BLE_HS_CONN_HANDLE_NONE ||
        job->conn_handle != s_conn_handle) {
        return;
    }

    struct os_mbuf *om = ble_hs_mbuf_from_flat(job->data, job->length);
    if (om == NULL) {
        if (job->kind != TX_KIND_TELEMETRY) {
            ESP_LOGW(TAG, "TX mbuf habis kind=%u", (unsigned)job->kind);
        }
        return;
    }

    /* NimBLE consumes om on every return path. */
    int rc = ble_gatts_notify_custom(job->conn_handle, job->value_handle, om);
    if (rc != 0 && job->kind != TX_KIND_TELEMETRY) {
        ESP_LOGW(TAG, "Notify gagal kind=%u rc=%d", (unsigned)job->kind, rc);
    }
}

static void tx_host_event_cb(struct ble_npl_event *event)
{
    (void)event;
    tx_job_t job;
    unsigned sent = 0u;

    /* ACK/OTA always precede the latest telemetry snapshot. */
    while (sent < TX_HOST_BURST &&
           xQueueReceive(s_tx_priority_queue, &job, 0) == pdTRUE) {
        tx_send_in_host(&job);
        ++sent;
    }
    if (sent < TX_HOST_BURST &&
        xQueueReceive(s_tx_telemetry_queue, &job, 0) == pdTRUE) {
        tx_send_in_host(&job);
    }

    portENTER_CRITICAL(&s_tx_event_lock);
    s_tx_event_pending = false;
    portEXIT_CRITICAL(&s_tx_event_lock);

    /*
     * Covers a producer that queued data while this event was executing.
     * tx_schedule_host_event() is idempotent under s_tx_event_lock.
     */
    if (uxQueueMessagesWaiting(s_tx_priority_queue) != 0u ||
        uxQueueMessagesWaiting(s_tx_telemetry_queue) != 0u) {
        tx_schedule_host_event();
    }
}

static bool queue_notification(uint16_t conn_handle, uint16_t value_handle,
                               const uint8_t *data, uint16_t length,
                               uint8_t kind)
{
    if (data == NULL || length == 0u || length > TX_PAYLOAD_MAX ||
        conn_handle == BLE_HS_CONN_HANDLE_NONE ||
        conn_handle != s_conn_handle ||
        s_tx_priority_queue == NULL || s_tx_telemetry_queue == NULL ||
        !s_tx_event_ready) {
        return false;
    }

    tx_job_t job = {
        .conn_handle = conn_handle,
        .value_handle = value_handle,
        .length = length,
        .kind = kind,
    };
    memcpy(job.data, data, length);

    BaseType_t queued;
    if (kind == TX_KIND_TELEMETRY) {
        /* Queue length is one: old telemetry is replaced, never accumulated. */
        queued = xQueueOverwrite(s_tx_telemetry_queue, &job);
    } else {
        queued = xQueueSendToBack(s_tx_priority_queue, &job, 0);
    }
    if (queued != pdTRUE) return false;

    tx_schedule_host_event();
    return true;
}

static void notify_response(uint16_t conn_handle, const uint8_t *reply, size_t length)
{
    if (length == 0u) return;
    if (length > TX_PAYLOAD_MAX ||
        !queue_notification(conn_handle, s_resp_val_handle, reply,
                            (uint16_t)length, TX_KIND_RESPONSE)) {
        ESP_LOGW(TAG, "Response TX queue penuh/tidak siap");
    }
}

/*
 * SETUP/MAP commands may commit NVS. Flash commits must never run inside the
 * NimBLE GATT access callback: doing so can starve the host. The response is
 * also queued back to the NimBLE host task; no application task touches VHCI.
 */
static void command_worker_task(void *arg)
{
    (void)arg;
    ble_work_job_t job;
    uint8_t reply[256];
    for (;;) {
        if (xQueueReceive(s_work_queue, &job, portMAX_DELAY) != pdTRUE) continue;
        s_command_busy = true;
        if (job.kind == WORK_KIND_OTA_DATA) {
            /*
             * esp_ota_write() may stall on flash. Keep it off the NimBLE host
             * task and in the same FIFO as commands so BEGIN/DATA/END order is
             * preserved.
             */
            cdi_engine_handle_ota_data(job.data, job.length);
        } else {
            size_t n = cdi_engine_handle_command(job.data, job.length,
                                                 reply, sizeof(reply));
            notify_response(job.conn_handle, reply, n);
        }
        s_command_busy = false;
        UBaseType_t remaining = uxTaskGetStackHighWaterMark(NULL);
        if (remaining < 1024u) {
            ESP_LOGW(TAG, "Stack worker BLE menipis: %u byte", (unsigned)remaining);
        }
    }
}

static int command_write_cb(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)attr_handle; (void)arg;
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return 0;
    ble_work_job_t job = {
        .conn_handle = conn_handle,
        .length = OS_MBUF_PKTLEN(ctxt->om),
        .kind = WORK_KIND_COMMAND,
    };
    if (job.length == 0u || job.length > sizeof(job.data) ||
        ble_hs_mbuf_to_flat(ctxt->om, job.data, job.length, NULL) != 0) {
        ESP_LOGW(TAG, "Frame command kosong/terlalu panjang; diabaikan");
        return 0;
    }
    if (s_work_queue == NULL ||
        xQueueSend(s_work_queue, &job, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Command queue penuh; frame ditolak tanpa memutus BLE");
    }
    return 0;
}

static int ota_data_write_cb(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle; (void)attr_handle; (void)arg;
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return 0;
    ble_work_job_t job = {
        .conn_handle = conn_handle,
        .length = OS_MBUF_PKTLEN(ctxt->om),
        .kind = WORK_KIND_OTA_DATA,
    };
    if (job.length == 0u || job.length > sizeof(job.data) ||
        ble_hs_mbuf_to_flat(ctxt->om, job.data, job.length, NULL) != 0) {
        ESP_LOGW(TAG, "Frame OTA kosong/terlalu panjang; ditolak");
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    if (s_work_queue == NULL ||
        xQueueSend(s_work_queue, &job, 0) != pdTRUE) {
        ESP_LOGW(TAG, "BLE work queue penuh; frame OTA ditolak");
        return BLE_ATT_ERR_INSUFFICIENT_RES;
    }
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

static void reset_tx_queues(void)
{
    if (s_tx_priority_queue != NULL) xQueueReset(s_tx_priority_queue);
    if (s_tx_telemetry_queue != NULL) xQueueReset(s_tx_telemetry_queue);
}

static int gap_event_cb(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        reset_tx_queues();
        s_conn_handle = event->connect.status == 0 ?
            event->connect.conn_handle : BLE_HS_CONN_HANDLE_NONE;
        cdi_engine_set_ble_connected(s_conn_handle != BLE_HS_CONN_HANDLE_NONE);
        if (event->connect.status != 0) cdi_ble_start_advertising();
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGW(TAG, "BLE disconnect reason=0x%02x", event->disconnect.reason);
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        reset_tx_queues();
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

static void delete_ble_queues(void)
{
    if (s_work_queue != NULL) vQueueDelete(s_work_queue);
    if (s_tx_priority_queue != NULL) vQueueDelete(s_tx_priority_queue);
    if (s_tx_telemetry_queue != NULL) vQueueDelete(s_tx_telemetry_queue);
    s_work_queue = NULL;
    s_tx_priority_queue = NULL;
    s_tx_telemetry_queue = NULL;
}

void cdi_ble_init(void)
{
    s_work_queue = xQueueCreate(BLE_WORK_QUEUE_DEPTH, sizeof(ble_work_job_t));
    s_tx_priority_queue = xQueueCreate(TX_PRIORITY_QUEUE_DEPTH, sizeof(tx_job_t));
    s_tx_telemetry_queue = xQueueCreate(1u, sizeof(tx_job_t));
    if (s_work_queue == NULL || s_tx_priority_queue == NULL ||
        s_tx_telemetry_queue == NULL) {
        ESP_LOGE(TAG, "Gagal membuat queue BLE");
        delete_ble_queues();
        return;
    }
    if (xTaskCreate(command_worker_task, "cdi_ble_cmd", COMMAND_WORKER_STACK,
                    NULL, COMMAND_WORKER_PRIORITY, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Gagal membuat command worker BLE");
        delete_ble_queues();
        return;
    }

    nimble_port_init();
    ble_npl_event_init(&s_tx_event, tx_host_event_cb, NULL);
    s_tx_event_ready = true;
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
    uint16_t conn = s_conn_handle;
    if (conn != BLE_HS_CONN_HANDLE_NONE &&
        !queue_notification(conn, s_ota_status_val_handle, data, len,
                            TX_KIND_OTA)) {
        ESP_LOGW(TAG, "OTA TX queue penuh/tidak siap");
    }
}

void cdi_ble_notify_telemetry(const uint8_t *data, uint16_t len)
{
    /* ACK command gets priority; telemetry resumes with the newest snapshot. */
    if (s_command_busy) return;
    uint16_t conn = s_conn_handle;
    if (conn != BLE_HS_CONN_HANDLE_NONE) {
        (void)queue_notification(conn, s_telem_val_handle, data, len,
                                 TX_KIND_TELEMETRY);
    }
}
