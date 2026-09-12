#define _POSIX_C_SOURCE 200809L
#include "cdi_r5_protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint16_t cdi_r5_crc16(const void *data, size_t length)
{
    const uint8_t *p = (const uint8_t *)data;
    uint16_t crc = 0xFFFFu;
    size_t i;
    for (i = 0u; i < length; ++i) {
        unsigned bit;
        crc ^= (uint16_t)p[i] << 8u;
        for (bit = 0u; bit < 8u; ++bit)
            crc = (uint16_t)((crc << 1u) ^ ((crc & 0x8000u) ? 0x1021u : 0u));
    }
    return crc;
}

void cdi_r5_protocol_init(cdi_r5_protocol_t *p, cdi_r5_store_image_t *store)
{
    if (p == NULL || store == NULL) return;
    memset(p, 0, sizeof(*p));
    p->store = store;
    p->working = store->slots[store->active_slot];
    p->setup_trigger_cdeg = store->setup.trigger_angle_cdeg;
}

void cdi_r5_protocol_set_persist(cdi_r5_protocol_t *p,
                                 cdi_r5_persist_fn persist,
                                 void *context)
{
    if (p == NULL) return;
    p->persist = persist;
    p->persist_context = context;
}

void cdi_r8_protocol_attach_oem_learner(cdi_r5_protocol_t *p,
                                        cdi_r8_oem_learner_t *learner)
{
    if (p == NULL) return;
    p->oem_learner = learner;
}

void cdi_r8_protocol_attach_ota(cdi_r5_protocol_t *p,cdi_r8_ota_t *ota)
{
    if(p==NULL)return;
    p->ota=ota;
}

static size_t make_frame(unsigned long seq, const char *body,
                         char *out, size_t size)
{
    char payload[256];
    int n = snprintf(payload, sizeof(payload), "%lu,%s", seq, body);
    int total;
    uint16_t crc;
    if (n < 0 || (size_t)n >= sizeof(payload)) return 0u;
    crc = cdi_r5_crc16(payload, (size_t)n);
    total = snprintf(out, size, "@%s*%04X\n", payload, crc);
    return total > 0 && (size_t)total < size ? (size_t)total : 0u;
}

static size_t error_frame(unsigned long seq, const char *name,
                          char *out, size_t size)
{
    char body[96];
    int n = snprintf(body, sizeof(body), "ERR,%s", name);
    return n > 0 && (size_t)n < sizeof(body) ? make_frame(seq, body, out, size) : 0u;
}

static bool parse_uint(const char *s, unsigned long max, unsigned long *value)
{
    char *end;
    unsigned long v;
    if (s == NULL || *s == '\0' || *s == '-') return false;
    v = strtoul(s, &end, 10);
    if (*end != '\0' || v > max) return false;
    *value = v;
    return true;
}

static bool setup_can_write(const cdi_r5_protocol_t *p)
{ return p->rpm == 0u && !p->hv_enabled && p->hv_center < 30u && p->hv_side < 30u; }

static bool persist_store(cdi_r5_protocol_t *p)
{
    uint32_t previous_crc = p->store->crc32;
    cdi_r5_store_seal(p->store);
    if (p->persist == NULL || p->persist(p->store, p->persist_context)) return true;
    p->store->crc32 = previous_crc;
    return false;
}

static size_t handle_mode(cdi_r5_protocol_t *p, unsigned long seq, char **save,
                          char *out, size_t out_size)
{
    const char *mode = strtok_r(NULL, ",", save);
    cdi_r7_setup_t old;
    if (mode == NULL || !setup_can_write(p))
        return error_frame(seq, "STOP_ENGINE_WAIT_HV_LT30", out, out_size);
    old = p->store->setup;
    p->strobe_active = false;
    if (!strcmp(mode, "OEM_LEARN")) {
        p->store->setup.operating_mode = CDI_R8_OP_OEM_LEARN;
        p->store->setup.diy_oem_unplug_confirmed = 0u;
        p->store->setup.center_enabled = 0u;
        p->store->setup.side_enabled = 0u;
    } else if (!strcmp(mode, "MANUAL")) {
        p->store->setup.operating_mode = CDI_R8_OP_MANUAL_SETUP;
        p->store->setup.diy_oem_unplug_confirmed = 0u;
        p->store->setup.center_enabled = 0u;
        p->store->setup.side_enabled = 0u;
    } else if (!strcmp(mode, "DIY")) {
        const char *confirm = strtok_r(NULL, ",", save);
        if (confirm == NULL || strcmp(confirm, "OEM_UNPLUGGED"))
            return error_frame(seq, "CONFIRM_OEM_UNPLUGGED", out, out_size);
        if (p->store->setup.stage < CDI_R7_STAGE_TDC_SAVED) {
            p->store->setup = old;
            return error_frame(seq, "CALIBRATION_REQUIRED", out, out_size);
        }
        p->store->setup.operating_mode = CDI_R8_OP_DIY;
        p->store->setup.diy_oem_unplug_confirmed = 1u;
        p->store->setup.stage = p->store->setup.first_start_proven ?
            CDI_R7_STAGE_READY : CDI_R7_STAGE_FIRST_START;
        p->store->setup.center_enabled = 1u;
        p->store->setup.side_enabled = p->store->setup.first_start_proven &&
            p->store->oem_profile.valid &&
            p->store->oem_profile.side_samples >= 10u;
    } else {
        return error_frame(seq, "MODE", out, out_size);
    }
    if (!persist_store(p)) {
        p->store->setup = old;
        return error_frame(seq, "FLASH", out, out_size);
    }
    return make_frame(seq, "ACK,MODE", out, out_size);
}

static size_t handle_learn(cdi_r5_protocol_t *p, unsigned long seq, char **save,
                           char *out, size_t out_size)
{
    const char *op = strtok_r(NULL, ",", save);
    cdi_r7_setup_t old_setup;
    cdi_r5_map_t old_map;
    cdi_r8_oem_profile_t old_profile;
    bool side;
    if (p->oem_learner == NULL || op == NULL)
        return error_frame(seq, "OEM_LEARN_UNAVAILABLE", out, out_size);
    if (!strcmp(op, "START")) {
        if (!setup_can_write(p) ||
            p->store->setup.operating_mode != CDI_R8_OP_OEM_LEARN)
            return error_frame(seq, "SELECT_OEM_LEARN_ENGINE_STOPPED", out, out_size);
        cdi_r8_oem_learn_start(p->oem_learner);
        return make_frame(seq, "ACK,LEARN_STARTED_PASSIVE", out, out_size);
    }
    if (!strcmp(op, "ABORT")) {
        cdi_r8_oem_learn_abort(p->oem_learner);
        return make_frame(seq, "ACK,LEARN_ABORTED", out, out_size);
    }
    if (!strcmp(op, "STOP")) {
        if (!setup_can_write(p))
            return error_frame(seq, "STOP_ENGINE_WAIT_HV_LT30", out, out_size);
        side = p->oem_learner->profile.side_samples >= 10u;
        old_setup = p->store->setup;
        old_map = p->store->slots[1];
        old_profile = p->store->oem_profile;
        if (!cdi_r8_oem_learn_finish(p->oem_learner,
                                     &p->store->slots[1], side))
            return error_frame(seq, "LEARN_NEEDS_20_VALID_PULSES", out, out_size);
        p->store->oem_profile = p->oem_learner->profile;
        p->store->active_slot = 1u;
        p->working = p->store->slots[1];
        p->store->setup.stage = CDI_R7_STAGE_TDC_SAVED;
        p->store->setup.side_offset_cdeg = side ?
            p->store->oem_profile.side_offset_cdeg : 0;
        p->store->setup.center_enabled = 0u;
        p->store->setup.side_enabled = 0u;
        p->store->setup.diy_oem_unplug_confirmed = 0u;
        if (!persist_store(p)) {
            p->store->setup = old_setup;
            p->store->slots[1] = old_map;
            p->store->oem_profile = old_profile;
            p->working = old_map;
            return error_frame(seq, "FLASH", out, out_size);
        }
        return make_frame(seq, side ? "ACK,LEARN_SAVED_CENTER_SIDE" :
                          "ACK,LEARN_SAVED_CENTER", out, out_size);
    }
    return error_frame(seq, "LEARN", out, out_size);
}

static size_t handle_setup(cdi_r5_protocol_t *p, unsigned long seq, char **save,
                           char *out, size_t out_size)
{
    const char *op = strtok_r(NULL, ",", save);
    unsigned long a;
    cdi_r7_setup_t old;
    if (!op) return error_frame(seq, "SETUP_COMMAND", out, out_size);
    if (!strcmp(op,"PICKUP")) {
        const char *x=strtok_r(NULL,",",save);
        if (!x || strcmp(x,"CONFIRM")) return error_frame(seq,"PICKUP_COMMAND",out,out_size);
        if (!setup_can_write(p)) return error_frame(seq,"STOP_ENGINE_WAIT_HV_LT30",out,out_size);
        if (p->pickup_quality<10u) return error_frame(seq,"PICKUP_NOT_STABLE",out,out_size);
        old=p->store->setup; if (p->store->setup.stage<CDI_R7_STAGE_PICKUP_OK) p->store->setup.stage=CDI_R7_STAGE_PICKUP_OK;
        if (!persist_store(p)){p->store->setup=old; return error_frame(seq,"FLASH",out,out_size);}
        return make_frame(seq,"ACK,PICKUP_OK",out,out_size);
    }
    if (!strcmp(op,"EDGE")) {
        const char *x=strtok_r(NULL,",",save);
        if (!setup_can_write(p)) return error_frame(seq,"STOP_ENGINE_WAIT_HV_LT30",out,out_size);
        if (!x || (strcmp(x,"FALLING")&&strcmp(x,"RISING"))) return error_frame(seq,"EDGE",out,out_size);
        old=p->store->setup; p->store->setup.pickup_edge=!strcmp(x,"RISING")?CDI_R7_EDGE_RISING:CDI_R7_EDGE_FALLING; p->pickup_quality=0;
        p->store->setup.stage=CDI_R7_STAGE_NEW; p->store->setup.center_enabled=0; p->store->setup.side_enabled=0; p->strobe_active=false;
        if (!persist_store(p)){p->store->setup=old; return error_frame(seq,"FLASH",out,out_size);}
        return make_frame(seq,"ACK,EDGE_REQUIRES_PICKUP_TDC",out,out_size);
    }
    if (!strcmp(op,"PPR") && parse_uint(strtok_r(NULL,",",save),4u,&a) && a>=1u) {
        if (!setup_can_write(p)) return error_frame(seq,"STOP_ENGINE_WAIT_HV_LT30",out,out_size);
        old=p->store->setup; p->store->setup.pulses_per_revolution=(uint16_t)a;
        if (p->store->setup.stage>CDI_R7_STAGE_PICKUP_OK) p->store->setup.stage=CDI_R7_STAGE_PICKUP_OK;
        p->store->setup.center_enabled=0; p->store->setup.side_enabled=0; p->strobe_active=false;
        if (!persist_store(p)){p->store->setup=old; return error_frame(seq,"FLASH",out,out_size);}
        return make_frame(seq,"ACK,PPR_REQUIRES_TDC",out,out_size);
    }
    if (!strcmp(op,"GATE_US") && parse_uint(strtok_r(NULL,",",save),150u,&a) && a>=40u) {
        if (!setup_can_write(p)) return error_frame(seq,"STOP_ENGINE_WAIT_HV_LT30",out,out_size);
        old=p->store->setup; p->store->setup.gate_pulse_us=(uint16_t)a;
        if (!persist_store(p)){p->store->setup=old; return error_frame(seq,"FLASH",out,out_size);}
        return make_frame(seq,"ACK,GATE_US",out,out_size);
    }
    if (!strcmp(op,"STROBE")) {
        const char *x=strtok_r(NULL,",",save); if (!x) return error_frame(seq,"STROBE",out,out_size);
        if (!strcmp(x,"ON")) { if (p->store->setup.stage<CDI_R7_STAGE_PICKUP_OK||p->hv_enabled) return error_frame(seq,"PICKUP_OR_HV_ACTIVE",out,out_size); p->strobe_active=true; p->strobe_samples=0; }
        else if (!strcmp(x,"OFF")) p->strobe_active=false; else return error_frame(seq,"STROBE",out,out_size);
        return make_frame(seq,p->strobe_active?"ACK,STROBE_ON":"ACK,STROBE_OFF",out,out_size);
    }
    if (!strcmp(op,"OFFSET") && parse_uint(strtok_r(NULL,",",save),35999u,&a)) {
        if (!p->strobe_active) return error_frame(seq,"START_STROBE_FIRST",out,out_size);
        p->setup_trigger_cdeg=(uint16_t)a; return make_frame(seq,"ACK,OFFSET",out,out_size);
    }
    if (!strcmp(op,"SAVE_TDC")) {
        if (!setup_can_write(p)) return error_frame(seq,"STOP_ENGINE_WAIT_HV_LT30",out,out_size);
        if (!p->strobe_active||p->strobe_samples<5u) return error_frame(seq,"STROBE_SAMPLES",out,out_size);
        old=p->store->setup; p->store->setup.trigger_angle_cdeg=p->setup_trigger_cdeg; p->store->setup.stage=CDI_R7_STAGE_TDC_SAVED;
        p->store->setup.center_enabled=0; p->store->setup.side_enabled=0; p->strobe_active=false;
        if (!persist_store(p)){p->store->setup=old; return error_frame(seq,"FLASH",out,out_size);}
        return make_frame(seq,"ACK,TDC_SAVED",out,out_size);
    }
    if (!strcmp(op,"MANUAL_TDC") && parse_uint(strtok_r(NULL,",",save),35999u,&a)) {
        const char *x=strtok_r(NULL,",",save);
        if (!x||strcmp(x,"CONFIRM")) return error_frame(seq,"CONFIRM_REQUIRED",out,out_size);
        if (!setup_can_write(p)||p->store->setup.stage<CDI_R7_STAGE_PICKUP_OK) return error_frame(seq,"PICKUP_OR_HV_ACTIVE",out,out_size);
        old=p->store->setup; p->store->setup.trigger_angle_cdeg=(uint16_t)a; p->store->setup.stage=CDI_R7_STAGE_TDC_SAVED;
        p->store->setup.center_enabled=0; p->store->setup.side_enabled=0; p->setup_trigger_cdeg=(uint16_t)a;
        if (!persist_store(p)){p->store->setup=old; return error_frame(seq,"FLASH",out,out_size);}
        return make_frame(seq,"ACK,TDC_MANUAL_SAVED",out,out_size);
    }
    if (!strcmp(op,"TPS")) {
        const char *x=strtok_r(NULL,",",save); if (!setup_can_write(p)||!x) return error_frame(seq,"STOP_ENGINE_FOR_TPS",out,out_size);
        old=p->store->setup;
        if (!strcmp(x,"CLOSED")){p->store->setup.tps_closed_adc=p->tps_raw;p->store->setup.tps_open_adc=0;}
        else if (!strcmp(x,"OPEN")&&p->tps_raw>p->store->setup.tps_closed_adc+50u) p->store->setup.tps_open_adc=p->tps_raw;
        else return error_frame(seq,"TPS_RANGE",out,out_size);
        if (!persist_store(p)){p->store->setup=old; return error_frame(seq,"FLASH",out,out_size);}
        return make_frame(seq,"ACK,TPS",out,out_size);
    }
    if (!strcmp(op,"FIRST_START")) {
        if (!setup_can_write(p)||p->store->setup.stage<CDI_R7_STAGE_TDC_SAVED) return error_frame(seq,"TDC_OR_HV_ACTIVE",out,out_size);
        old=p->store->setup; p->store->setup.stage=CDI_R7_STAGE_FIRST_START; p->store->setup.center_enabled=1; p->store->setup.side_enabled=0;
        p->store->setup.fan_mode=CDI_R7_FAN_ON; p->store->setup.first_start_proven=0u; p->first_start_seconds=0;
        if (!persist_store(p)){p->store->setup=old; return error_frame(seq,"FLASH",out,out_size);}
        return make_frame(seq,"ACK,FIRST_START",out,out_size);
    }
    if (!strcmp(op,"READY")) {
        const char *x=strtok_r(NULL,",",save); long side=0; char *end=NULL;
        bool first_proven=p->store->setup.stage==CDI_R7_STAGE_READY||
            p->store->setup.first_start_proven!=0u;
        if (!setup_can_write(p)||!first_proven) return error_frame(seq,"FIRST_START_NOT_PROVEN",out,out_size);
        old=p->store->setup; p->store->setup.stage=CDI_R7_STAGE_READY; p->store->setup.center_enabled=1; p->store->setup.side_enabled=0;
        if (x&&!strcmp(x,"THREE")){const char *s=strtok_r(NULL,",",save); if(!s){p->store->setup=old;return error_frame(seq,"SIDE_OFFSET",out,out_size);} side=strtol(s,&end,10); if(*end||side< -3000||side>3000){p->store->setup=old;return error_frame(seq,"SIDE_OFFSET",out,out_size);} p->store->setup.side_offset_cdeg=(int16_t)side;p->store->setup.side_enabled=1;}
        else if (!x||strcmp(x,"CENTER")){p->store->setup=old;return error_frame(seq,"READY_MODE",out,out_size);}
        if (!persist_store(p)){p->store->setup=old; return error_frame(seq,"FLASH",out,out_size);}
        return make_frame(seq,p->store->setup.side_enabled?"ACK,READY_THREE":"ACK,READY_CENTER",out,out_size);
    }
    if (!strcmp(op,"FAN")) {
        const char *x=strtok_r(NULL,",",save); if(!setup_can_write(p)||!x)return error_frame(seq,"STOP_ENGINE_FOR_FAN",out,out_size);
        old=p->store->setup; if(!strcmp(x,"OFF"))p->store->setup.fan_mode=CDI_R7_FAN_OFF; else if(!strcmp(x,"ON"))p->store->setup.fan_mode=CDI_R7_FAN_ON; else if(!strcmp(x,"AUTO"))p->store->setup.fan_mode=CDI_R7_FAN_AUTO; else return error_frame(seq,"FAN_MODE",out,out_size);
        if(!persist_store(p)){p->store->setup=old;return error_frame(seq,"FLASH",out,out_size);} return make_frame(seq,"ACK,FAN",out,out_size);
    }
    if (!strcmp(op,"RESET")) {
        const char *x=strtok_r(NULL,",",save); cdi_r5_store_image_t d;
        if(!x||strcmp(x,"CONFIRM")||!setup_can_write(p))return error_frame(seq,"RESET_BLOCKED",out,out_size);
        old=p->store->setup;cdi_r5_load_defaults(&d);p->store->setup=d.setup;p->setup_trigger_cdeg=d.setup.trigger_angle_cdeg;p->strobe_active=false;p->pickup_quality=0;
        if(!persist_store(p)){p->store->setup=old;return error_frame(seq,"FLASH",out,out_size);}return make_frame(seq,"ACK,SETUP_RESET",out,out_size);
    }
    return error_frame(seq,"SETUP_UNKNOWN",out,out_size);
}

size_t cdi_r5_protocol_handle(cdi_r5_protocol_t *p, const char *frame,
                              char *response, size_t response_size)
{
    char copy[256], *star, *save, *tok, *cmd;
    unsigned long seq, supplied_crc, a, b, c;
    uint16_t actual_crc;
    size_t payload_len;
    if (p == NULL || p->store == NULL || frame == NULL || response == NULL ||
        response_size == 0u || frame[0] != '@') return 0u;
    if (strlen(frame) >= sizeof(copy)) return 0u;
    strcpy(copy, frame + 1);
    copy[strcspn(copy, "\r\n")] = '\0';
    star = strrchr(copy, '*');
    if (star == NULL || strlen(star + 1) != 4u) return 0u;
    *star = '\0';
    supplied_crc = strtoul(star + 1, &save, 16);
    if (*save != '\0' || supplied_crc > 0xFFFFu) return 0u;
    payload_len = strlen(copy);
    actual_crc = cdi_r5_crc16(copy, payload_len);
    save = NULL;
    tok = strtok_r(copy, ",", &save);
    if (!parse_uint(tok, 65535u, &seq)) return 0u;
    if (actual_crc != supplied_crc) return error_frame(seq, "CRC", response, response_size);
    cmd = strtok_r(NULL, ",", &save);
    if (cmd == NULL) return error_frame(seq, "COMMAND", response, response_size);

    if (strcmp(cmd, "PING") == 0)
        /* Legacy token is intentionally stable; GET,CAPS reports R8. */
        return make_frame(seq, "ACK,PONG_R7_2", response, response_size);

    if (strcmp(cmd, "GET") == 0) {
        char body[220];
        const char *what = strtok_r(NULL, ",", &save);
        int n;
        if (what != NULL && strcmp(what, "STATUS") == 0) {
            n = snprintf(body, sizeof(body),
                "STATUS,%lu,%u,%u,%u,%u,%u,%u,%u", (unsigned long)p->rpm,
                p->tps_permille, p->hv_center, p->hv_side,
                p->store->active_slot, p->working.mode,
                p->output_permission ? 1u : 0u, p->pro_enabled ? 1u : 0u);
        } else if (what != NULL && strcmp(what, "META") == 0) {
            n = snprintf(body, sizeof(body), "META,%s,%u,%u,%u,%u,%u,%u,%u,%u",
                p->working.name, p->working.mode, p->working.limiter_type,
                p->working.rpm_limit, p->working.soft_band_rpm,
                p->working.hv_target_volts, p->working.generation,
                p->working.rpm_count, p->working.tps_count);
        } else if (what != NULL && strcmp(what, "SETUP") == 0) {
            const cdi_r7_setup_t *s=&p->store->setup;
            n=snprintf(body,sizeof(body),"SETUP,%u,%u,%u,%d,%u,%u,%u,%u,%u,%u,%u,%u,%u",
                s->stage,s->pickup_edge,p->setup_trigger_cdeg,s->side_offset_cdeg,s->pulses_per_revolution,s->gate_pulse_us,
                s->tps_closed_adc,s->tps_open_adc,s->first_start_hv_volts,s->center_enabled,s->side_enabled,s->fan_mode,p->pickup_quality);
        } else if (what != NULL && strcmp(what, "CAPS") == 0) {
            n=snprintf(body,sizeof(body),
                "CAPS,R8.0,PROTO4,OEM_LEARN,MANUAL,OTA_STAGE,AUTO_FIRST_START,NO_JUMPERS");
        } else if (what != NULL && strcmp(what, "MODE") == 0) {
            const cdi_r7_setup_t *s=&p->store->setup;
            n=snprintf(body,sizeof(body),"MODE,%u,%u,%u,%u",
                s->operating_mode,s->diy_oem_unplug_confirmed,
                s->pro_enabled,s->first_start_proven);
        } else if (what != NULL && strcmp(what, "LEARN") == 0 &&
                   p->oem_learner != NULL) {
            n=snprintf(body,sizeof(body),"LEARN,%u,%u,%u,%u,%u,%d",
                p->oem_learner->state,
                cdi_r8_oem_learn_coverage(p->oem_learner),
                p->oem_learner->profile.accepted_pulses,
                p->oem_learner->profile.rejected_pulses,
                p->oem_learner->profile.side_samples,
                p->oem_learner->profile.side_offset_cdeg);
        } else if (what != NULL && strcmp(what, "OTA") == 0 && p->ota != NULL) {
            n=snprintf(body,sizeof(body),"OTA,%u,%lu,%lu,%u",
                p->ota->state,(unsigned long)p->ota->received,
                (unsigned long)p->ota->expected_length,p->ota->error_code);
        } else if (what != NULL && strcmp(what, "CELL") == 0 &&
                   parse_uint(strtok_r(NULL, ",", &save), 7u, &a) &&
                   parse_uint(strtok_r(NULL, ",", &save), 15u, &b) &&
                   a < p->working.tps_count && b < p->working.rpm_count) {
            n = snprintf(body, sizeof(body), "CELL,%lu,%lu,%d",
                a, b, p->working.advance_cdeg[a][b]);
        } else return error_frame(seq, "GET", response, response_size);
        return n > 0 && (size_t)n < sizeof(body) ?
               make_frame(seq, body, response, response_size) : 0u;
    }

    if (strcmp(cmd,"SETUP")==0) return handle_setup(p,seq,&save,response,response_size);
    if (strcmp(cmd,"MODE")==0) return handle_mode(p,seq,&save,response,response_size);
    if (strcmp(cmd,"LEARN")==0) return handle_learn(p,seq,&save,response,response_size);

    if(strcmp(cmd,"OTA")==0){
        const char *op=strtok_r(NULL,",",&save);
        if(p->ota==NULL||op==NULL)return error_frame(seq,"OTA_UNAVAILABLE",response,response_size);
        if(!strcmp(op,"BEGIN")){
            if(!parse_uint(strtok_r(NULL,",",&save),0xffffffffu,&a)||
               !parse_uint(strtok_r(NULL,",",&save),CDI_R8_APP_MAX_SIZE,&b)||
               !parse_uint(strtok_r(NULL,",",&save),0xffffffffu,&c))
                return error_frame(seq,"OTA_BEGIN",response,response_size);
            if(!cdi_r8_ota_begin(p->ota,(uint32_t)a,(uint32_t)b,(uint32_t)c,
                p->rpm==0u,!p->hv_enabled&&p->hv_center<30u&&p->hv_side<30u&&
                !p->output_permission))
                return error_frame(seq,"OTA_NOT_SAFE",response,response_size);
            p->firmware_update_active=true;
            return make_frame(seq,"ACK,OTA_RECEIVE",response,response_size);
        }
        if(!strcmp(op,"COMMIT")){
            if(!cdi_r8_ota_commit(p->ota))return error_frame(seq,"OTA_VERIFY",response,response_size);
            return make_frame(seq,"ACK,OTA_READY_REBOOT",response,response_size);
        }
        if(!strcmp(op,"ABORT")){cdi_r8_ota_abort(p->ota);p->firmware_update_active=false;
            return make_frame(seq,"ACK,OTA_ABORT",response,response_size);}
        return error_frame(seq,"OTA",response,response_size);
    }

    if (strcmp(cmd,"FEATURE")==0) {
        const char *name=strtok_r(NULL,",",&save);
        const char *value=strtok_r(NULL,",",&save);
        cdi_r7_setup_t old=p->store->setup;
        if (!setup_can_write(p) || name==NULL || value==NULL)
            return error_frame(seq,"STOP_ENGINE_WAIT_HV_LT30",response,response_size);
        if (strcmp(name,"PRO") || (strcmp(value,"ON") && strcmp(value,"OFF")))
            return error_frame(seq,"FEATURE",response,response_size);
        p->store->setup.pro_enabled=!strcmp(value,"ON");
        p->pro_enabled=p->store->setup.pro_enabled!=0u;
        if(!persist_store(p)){p->store->setup=old;return error_frame(seq,"FLASH",response,response_size);}
        return make_frame(seq,p->pro_enabled?"ACK,PRO_ON":"ACK,PRO_OFF",response,response_size);
    }

    if (strcmp(cmd, "LIVE") == 0 &&
        parse_uint(strtok_r(NULL, ",", &save), 7u, &a) &&
        parse_uint(strtok_r(NULL, ",", &save), 15u, &b) &&
        parse_uint(strtok_r(NULL, ",", &save), 4000u, &c)) {
        cdi_r5_status_t s = cdi_r5_live_set_cell(&p->working, (uint8_t)a,
            (uint8_t)b, (int16_t)c, p->rpm != 0u, p->pro_enabled);
        return s == CDI_R5_OK ? make_frame(seq, "ACK,LIVE", response, response_size) :
            error_frame(seq, s == CDI_R5_ERR_LIVE_STEP ? "STEP_MAX_2DEG" : "MAP",
                        response, response_size);
    }

    if (strcmp(cmd, "LIMIT") == 0) {
        const char *kind = strtok_r(NULL, ",", &save);
        cdi_r5_map_t candidate = p->working;
        if (p->rpm != 0u || p->hv_enabled)
            return error_frame(seq, "ENGINE_OR_HV", response, response_size);
        if (kind == NULL || !parse_uint(strtok_r(NULL, ",", &save), 11500u, &a) ||
            !parse_uint(strtok_r(NULL, ",", &save), 1000u, &b))
            return error_frame(seq, "LIMIT", response, response_size);
        candidate.limiter_type = strcmp(kind, "HARD") == 0 ? CDI_R5_LIMITER_HARD :
                                 strcmp(kind, "SOFT") == 0 ? CDI_R5_LIMITER_SOFT : 255u;
        candidate.rpm_limit = (uint16_t)a;
        candidate.soft_band_rpm = (uint16_t)b;
        if (cdi_r5_map_validate(&candidate, p->pro_enabled) != CDI_R5_OK)
            return error_frame(seq, "LIMIT_RANGE", response, response_size);
        p->working = candidate;
        ++p->working.generation;
        return make_frame(seq, "ACK,LIMIT", response, response_size);
    }

    if ((strcmp(cmd, "LOAD") == 0 || strcmp(cmd, "SAVE") == 0) &&
        parse_uint(strtok_r(NULL, ",", &save), 3u, &a)) {
        cdi_r5_status_t s;
        if (p->rpm != 0u || p->hv_enabled)
            return error_frame(seq, "ENGINE_OR_HV", response, response_size);
        if (strcmp(cmd, "LOAD") == 0) {
            uint8_t old_active;
            if (cdi_r5_map_validate(&p->store->slots[a], p->pro_enabled) != CDI_R5_OK)
                return error_frame(seq, "PRO_LOCKED", response, response_size);
            old_active = p->store->active_slot;
            p->store->active_slot = (uint8_t)a;
            cdi_r5_store_seal(p->store);
            if (p->persist != NULL && !p->persist(p->store, p->persist_context)) {
                p->store->active_slot = old_active;
                cdi_r5_store_seal(p->store);
                return error_frame(seq, "FLASH", response, response_size);
            }
            p->working = p->store->slots[a];
            return make_frame(seq, "ACK,LOAD", response, response_size);
        }
        s = cdi_r5_save_slot(p->store, (uint8_t)a, &p->working, p->rpm,
                             p->hv_enabled, p->pro_enabled);
        if (s != CDI_R5_OK)
            return error_frame(seq, "SAVE", response, response_size);
        if (p->persist != NULL && !p->persist(p->store, p->persist_context))
            return error_frame(seq, "FLASH", response, response_size);
        return make_frame(seq, "ACK,SAVE", response, response_size);
    }
    return error_frame(seq, "UNKNOWN", response, response_size);
}
