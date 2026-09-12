#include "cdi_r8_ota.h"
#include "cdi_r5.h"

#include <string.h>

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t length)
{
    size_t i;
    for(i=0u;i<length;++i){
        unsigned bit; crc^=data[i];
        for(bit=0u;bit<8u;++bit)
            crc=(crc>>1u)^(0xEDB88320u&(0u-(crc&1u)));
    }
    return crc;
}

void cdi_r8_ota_init(cdi_r8_ota_t *o, cdi_r8_ota_erase_fn erase,
                     cdi_r8_ota_program_fn program,
                     cdi_r8_ota_finalize_fn finalize, void *context)
{
    if(o==NULL)return;
    memset(o,0,sizeof(*o));o->erase=erase;o->program=program;
    o->finalize=finalize;o->io_context=context;o->running_crc32=0xffffffffu;
}

bool cdi_r8_ota_begin(cdi_r8_ota_t *o,uint32_t version,uint32_t length,
                      uint32_t crc,bool stopped,bool safe)
{
    if(o==NULL||o->erase==NULL||o->program==NULL||o->finalize==NULL||
       !stopped||!safe||length<256u||length>CDI_R8_APP_MAX_SIZE){
        if(o!=NULL){o->state=CDI_R8_OTA_ERROR;o->error_code=1u;} return false;
    }
    o->state=CDI_R8_OTA_ERASING;o->error_code=0u;
    if(!o->erase(o->io_context)){o->state=CDI_R8_OTA_ERROR;o->error_code=2u;return false;}
    o->image_version=version;o->expected_length=length;o->expected_crc32=crc;
    o->received=0u;o->running_crc32=0xffffffffu;o->state=CDI_R8_OTA_RECEIVING;
    return true;
}

bool cdi_r8_ota_write(cdi_r8_ota_t *o,uint32_t offset,
                      const uint8_t *data,size_t length)
{
    if(o==NULL||data==NULL||o->state!=CDI_R8_OTA_RECEIVING||
       offset!=o->received||length==0u||length>208u||(offset&7u)!=0u||
       ((length&7u)!=0u&&o->received+length!=o->expected_length)||
       o->received+length>o->expected_length){
        if(o!=NULL){o->state=CDI_R8_OTA_ERROR;o->error_code=3u;}return false;
    }
    if(!o->program(offset,data,length,o->io_context)){
        o->state=CDI_R8_OTA_ERROR;o->error_code=4u;return false;
    }
    o->running_crc32=crc32_update(o->running_crc32,data,length);
    o->received+=(uint32_t)length;return true;
}

bool cdi_r8_ota_manifest_valid(const cdi_r8_ota_manifest_t *m)
{
    return m!=NULL&&m->magic==CDI_R8_OTA_META_MAGIC&&
        m->hardware_id==CDI_R8_HW_ID&&m->staged_address==CDI_R8_STAGE_ADDR&&
        m->image_length>=256u&&m->image_length<=CDI_R8_APP_MAX_SIZE&&
        m->header_crc32==cdi_r5_crc32(m,offsetof(cdi_r8_ota_manifest_t,header_crc32))&&
        m->inverse_crc32==~m->header_crc32;
}

bool cdi_r8_ota_commit(cdi_r8_ota_t *o)
{
    cdi_r8_ota_manifest_t m;
    uint32_t final_crc;
    if(o==NULL||o->state!=CDI_R8_OTA_RECEIVING||o->received!=o->expected_length){
        if(o!=NULL){o->state=CDI_R8_OTA_ERROR;o->error_code=5u;}return false;
    }
    final_crc=~o->running_crc32;
    if(final_crc!=o->expected_crc32){o->state=CDI_R8_OTA_ERROR;o->error_code=6u;return false;}
    memset(&m,0,sizeof(m));m.magic=CDI_R8_OTA_META_MAGIC;m.hardware_id=CDI_R8_HW_ID;
    m.image_version=o->image_version;m.image_length=o->expected_length;
    m.image_crc32=o->expected_crc32;m.staged_address=CDI_R8_STAGE_ADDR;
    m.header_crc32=cdi_r5_crc32(&m,offsetof(cdi_r8_ota_manifest_t,header_crc32));
    m.inverse_crc32=~m.header_crc32;
    if(!o->finalize(&m,o->io_context)){o->state=CDI_R8_OTA_ERROR;o->error_code=7u;return false;}
    o->state=CDI_R8_OTA_READY;return true;
}

void cdi_r8_ota_abort(cdi_r8_ota_t *o)
{
    if(o==NULL)return;
    o->state=CDI_R8_OTA_IDLE;o->received=0u;
    o->expected_length=0u;o->running_crc32=0xffffffffu;o->error_code=0u;
}
