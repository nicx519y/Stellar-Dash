#include "ch585_iap_app.h"

#include <string.h>

#include "CH58x_common.h"
#include "ch585_iap_protocol.h"

void ch585_iap_mark_running_app(void)
{
    ch585_iap_metadata_t metadata __attribute__((aligned(4)));

    memset(&metadata, 0, sizeof(metadata));
    if((EEPROM_READ(CH585_IAP_METADATA_EEPROM_ADDR,
                    &metadata,
                    sizeof(metadata)) == 0u) &&
       (metadata.magic == CH585_IAP_METADATA_MAGIC) &&
       (metadata.state == CH585_IAP_IMAGE_STATE_VALID))
    {
        return;
    }

    memset(&metadata, 0, sizeof(metadata));
    metadata.magic = CH585_IAP_METADATA_MAGIC;
    metadata.state = CH585_IAP_IMAGE_STATE_VALID;
    metadata.protocol_version = CH585_IAP_PROTOCOL_VERSION;
    if(EEPROM_ERASE(CH585_IAP_METADATA_EEPROM_ADDR, EEPROM_PAGE_SIZE) != 0u)
    {
        return;
    }
    (void)EEPROM_WRITE(CH585_IAP_METADATA_EEPROM_ADDR,
                       &metadata,
                       sizeof(metadata));
}
