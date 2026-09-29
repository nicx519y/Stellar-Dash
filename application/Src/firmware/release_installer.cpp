#include "release_installer.hpp"
#include "release_build_identity.h"
#include "firmware_signature.h"
#include "sha256_simple.h"
#include "qspi-w25q64.h"
#include "ch585_firmware_update.hpp"
#include "ch585_iap_client.hpp"
#include "usb_board_link.hpp"
#include "storagemanager.hpp"
#include "board_cfg.h"
#include "main_runtime_control.hpp"
#include "stm32h7xx_hal.h"
#include <cstring>
#include <cstdio>
#include <cstddef>
#include <initializer_list>
#pragma GCC optimize("Os")

namespace {
const xora_release_identity_t self = {XORA_RELEASE_IDENTITY_MAGIC, 1,
    XORA_INSTALL_PROTOCOL,
    XORA_MAINTENANCE_PROTOCOL, CONFIG_VERSION, XORA_RELEASE_VERSION, XORA_RELEASE_BUILD_ID};
enum Phase : uint32_t { Empty, Receiving, Prepared, Activated, TxWriting, TxVerified,
    Committing, Verifying, Completed, Failed, Aborted };
const char* names[] = {"idle", "receiving", "prepared", "activated", "tx-writing", "tx-verified",
    "committing", "verifying", "completed", "failed", "aborted"};
struct Snapshot {
    uint32_t magic, generation, phase, attempts, configVersion, manifestSize;
    char session[33], confirmedVersion[32], confirmedDigest[65], error[96];
    FirmwareMetadata source, target;
    uint8_t signature[64];
    char manifest[XORA_RELEASE_MANIFEST_MAX + 1];
    uint32_t crc, commit;
};
static_assert(sizeof(Snapshot) < XORA_RELEASE_BANK_SIZE, "release snapshot exceeds bank");
static_assert(METADATA_STRUCT_SIZE == 807 && XORA_RELEASE_BANK_A >= METADATA_ADDR+4096 &&
    XORA_RELEASE_BANK_A+XORA_RELEASE_BANK_SIZE == XORA_RELEASE_BANK_B &&
    XORA_RELEASE_BANK_B+XORA_RELEASE_BANK_SIZE == METADATA_ADDR+METADATA_SIZE &&
    XORA_RELEASE_BANK_A%4096 == 0 && XORA_RELEASE_BANK_SIZE%4096 == 0,
    "release banks must remain within the existing metadata partition");
// AXI SRAM also contains executable code. These large, explicitly initialized
// work buffers use the existing D2 scratch section, never stack/newlib heap.
__attribute__((section(".DMA_Section.ReleaseInstall"),aligned(32))) Snapshot state;
__attribute__((section(".DMA_Section.ReleaseInstall"),aligned(32)))
uint8_t declaration[64 + METADATA_STRUCT_SIZE + XORA_RELEASE_MANIFEST_MAX];
uint32_t declarationSize = 0, declarationReceived = 0, bank = 0, resetAt = 0;
bool loaded = false, storageFault = false, declaring = false;
__attribute__((section(".DMA_Section.ReleaseInstall"))) char lastError[96];
__attribute__((section(".DMA_Section.ReleaseInstall"))) char targetVersion[32], targetBuild[65], txVersion[32], txBuild[65];
__attribute__((section(".DMA_Section.ReleaseInstall"))) char releaseDigest[65], txHash[65], txAppHash[65];
uint32_t txSize = 0, targetConfig = 0;
__attribute__((section(".DMA_Section.ReleaseInstall"))) uint32_t received[FIRMWARE_COMPONENT_COUNT];
bool fail(const char* message) { snprintf(lastError, sizeof(lastError), "%s", message); return false; }
uint32_t crc32(const void* p, uint32_t n) {
    const auto* b = static_cast<const uint8_t*>(p); uint32_t crc = ~0u;
    while (n--) { crc ^= *b++; for (int i=0;i<8;i++) crc=(crc>>1)^(0xedb88320u & (0u-(crc&1u))); }
    return ~crc;
}
bool read(uint32_t address, void* p, uint32_t size) {
    return QSPI_W25Qxx_ReadBuffer_WithXIPOrNot(static_cast<uint8_t*>(p), address-EXTERNAL_FLASH_BASE, size) == QSPI_W25Qxx_OK;
}
bool hash(const void* bytes, uint32_t size, char out[65]) {
    return sha256_calculate(static_cast<const uint8_t*>(bytes), size, out) == 1;
}
bool flashHash(uint32_t address, uint32_t size, const char* expected) {
    uint8_t bytes[512], digest[32]; char encoded[65]; sha256_simple_ctx_t ctx;
    sha256_simple_init(&ctx);
    while (size) { uint32_t n = size < sizeof(bytes) ? size : sizeof(bytes);
        if (!read(address, bytes, n)) return false;
        sha256_simple_update(&ctx, bytes, n); address += n; size -= n; }
    sha256_simple_final(&ctx, digest);
    for (unsigned i=0;i<32;i++) snprintf(encoded+i*2,3,"%02x",digest[i]);
    return strcmp(expected, encoded) == 0;
}
bool valid(const Snapshot& s) {
    return s.magic == 0x32524f58u && s.commit == 0x54494d43u && s.generation &&
        s.phase <= Aborted && s.manifestSize <= XORA_RELEASE_MANIFEST_MAX &&
        s.manifest[s.manifestSize] == 0 && s.session[32] == 0 && s.error[95] == 0 &&
        s.confirmedVersion[31] == 0 && s.confirmedDigest[64] == 0 &&
        s.crc == crc32(&s, offsetof(Snapshot, crc));
}
void load() {
    if (loaded) return; loaded = true;
    lastError[0]=targetVersion[0]=targetBuild[0]=txVersion[0]=txBuild[0]=releaseDigest[0]=txHash[0]=txAppHash[0]=0;
    memset(received,0,sizeof(received));
    uint32_t best = 0, generation = 0, blankBanks = 0; bool nonblank = false, initialTorn = false;
    for (uint32_t address : {XORA_RELEASE_BANK_A, XORA_RELEASE_BANK_B}) {
        if (!read(address, &state, sizeof(state))) { memset(&state,0,sizeof(state)); storageFault = true; return; }
        nonblank |= state.magic != 0xffffffffu;
        if(state.magic==0xffffffffu)++blankBanks;
        initialTorn |= state.magic==0x32524f58u && state.generation==1 && state.commit!=0x54494d43u &&
            (state.phase<=Prepared || state.phase==Aborted);
        if (valid(state) && state.generation > generation) { best=address; generation=state.generation; }
    }
    bank = best;
    if (best) { if (!read(best, &state, sizeof(state))) {memset(&state,0,sizeof(state));storageFault=true;} }
    else { memset(&state,0,sizeof(state)); storageFault=nonblank && !(blankBanks==1 && initialTorn); }
}
bool persist(Phase phase) {
    if (storageFault || state.generation == 0xffffffffu) return fail("Installation journal unavailable");
    state.magic=0x32524f58u; ++state.generation; state.phase=phase;
    snprintf(state.error,sizeof(state.error),"%s",phase==Failed?lastError:"");
    state.crc=crc32(&state,offsetof(Snapshot,crc)); state.commit=0xffffffffu;
    const uint32_t dest = bank == XORA_RELEASE_BANK_A ? XORA_RELEASE_BANK_B : XORA_RELEASE_BANK_A;
    if (QSPI_W25Qxx_ExitMemoryMappedMode() != QSPI_W25Qxx_OK) return fail("Cannot suspend QSPI");
    bool ok = true;
    for (uint32_t off=0; ok && off<XORA_RELEASE_BANK_SIZE; off+=4096)
        ok=QSPI_W25Qxx_SectorErase(dest-EXTERNAL_FLASH_BASE+off)==QSPI_W25Qxx_OK;
    for (uint32_t off=0; ok && off<offsetof(Snapshot,commit);) {
        uint32_t n=offsetof(Snapshot,commit)-off; if(n>256) n=256;
        ok=QSPI_W25Qxx_WritePage(reinterpret_cast<uint8_t*>(&state)+off,dest-EXTERNAL_FLASH_BASE+off,n)==QSPI_W25Qxx_OK;
        off+=n;
    }
    uint8_t check[256];
    for(uint32_t off=0;ok && off<offsetof(Snapshot,commit);) {
        uint32_t n=offsetof(Snapshot,commit)-off; if(n>sizeof(check))n=sizeof(check);
        ok=QSPI_W25Qxx_ReadBuffer(check,dest-EXTERNAL_FLASH_BASE+off,n)==QSPI_W25Qxx_OK &&
            memcmp(check,reinterpret_cast<uint8_t*>(&state)+off,n)==0; off+=n;
    }
    uint32_t marker=0x54494d43u;
    if(ok) ok=QSPI_W25Qxx_WritePage(reinterpret_cast<uint8_t*>(&marker),dest-EXTERNAL_FLASH_BASE+offsetof(Snapshot,commit),4)==QSPI_W25Qxx_OK;
    bool mapped=QSPI_W25Qxx_EnterMemoryMappedMode()==QSPI_W25Qxx_OK;
    if(!ok || !mapped) { loaded=false; load(); return fail("Installation journal commit uncertain"); }
    SCB_InvalidateDCache_by_Addr(reinterpret_cast<uint32_t*>(dest),XORA_RELEASE_BANK_SIZE);
    __DSB(); __ISB();
    uint32_t committed=0;
    if(!read(dest+offsetof(Snapshot,commit),&committed,4) || committed!=marker) {
        loaded=false; load(); return fail("Installation journal readback failed");
    }
    state.commit=marker; bank=dest; return true;
}
const cJSON* item(const cJSON* o,const char* key) {return cJSON_GetObjectItemCaseSensitive(o,key);}
const char* str(const cJSON* o,const char* key) {const auto* v=item(o,key);return cJSON_IsString(v)?v->valuestring:"";}
uint32_t num(const cJSON* o,const char* key) {
    const auto* v=item(o,key); if(!cJSON_IsNumber(v) || v->valuedouble<0 || v->valuedouble>4294967295.0 ||
        v->valuedouble!=static_cast<uint32_t>(v->valuedouble)) return 0xffffffffu;
    return static_cast<uint32_t>(v->valuedouble);
}
bool range(const cJSON* o,const char* key,uint32_t value) {
    const auto* r=item(o,key); const auto lo=num(r,"min"),hi=num(r,"max");
    return lo!=0xffffffffu && hi!=0xffffffffu && lo<=value && value<=hi;
}
bool uniqueKeys(const cJSON* o) {
    for(const cJSON* a=o?o->child:nullptr;a;a=a->next) {
        for(const cJSON* b=a->next;b;b=b->next) if(a->string && b->string && strcmp(a->string,b->string)==0)return false;
        if(!uniqueKeys(a))return false;
    } return true;
}
bool parseManifest(bool preflight) {
    if(!firmware_release_verify_bytes(reinterpret_cast<uint8_t*>(state.manifest),state.manifestSize,state.signature))return fail("Release signature rejected");
    cJSON* root=cJSON_ParseWithLength(state.manifest,state.manifestSize+1);
    if(!root)return fail("Invalid release JSON");
    const cJSON* c=item(root,"install");
    bool ok=uniqueKeys(root) && num(root,"schemaVersion")==2 && strcmp(str(root,"product"),"XORA")==0 &&
        strcmp(str(root,"deviceModel"),DEVICE_MODEL_STRING)==0 && strcmp(str(root,"hardwareVersion"),HARDWARE_VERSION_STRING)==0 &&
        strcmp(str(root,"bootSecurityMode"),"unlocked-development")==0 && cJSON_IsFalse(item(root,"requiresManualLifecycleProvisioning")) &&
        num(c,"protocol")==1 && strcmp(str(c,"order"),"tx-then-stm32")==0 &&
        range(c,"stm32Maintenance",1) && range(c,"txMaintenance",1) && range(c,"configRead",state.configVersion);
    targetConfig=num(c,"configWrite");
    // Initial installer preserves the exact on-flash configuration format. A migration
    // needs its own verified, reversible contract before it can be published as installable.
    ok &= targetConfig==state.configVersion;
    snprintf(targetVersion,sizeof(targetVersion),"%s",str(root,"version"));
    const cJSON* artifacts=item(root,"artifacts"); bool seenStm=false,seenTx=false;
    char metadataHash[65]; hash(&state.target,sizeof(state.target),metadataHash);
    const cJSON* a=nullptr;
    cJSON_ArrayForEach(a,artifacts) {
        if(strcmp(str(a,"component"),"stm32")==0 && strcmp(str(a,"slot"),state.target.target_slot==0?"A":"B")==0) {
            if(seenStm)ok=false; seenStm=true;
            ok &= strcmp(str(a,"metadataSha256"),metadataHash)==0 &&
                strcmp(str(a,"version"),state.target.firmware_version)==0;
            snprintf(targetBuild,sizeof(targetBuild),"%s",str(a,"buildId"));
        }
        if(strcmp(str(a,"component"),"tx")==0) {
            if(seenTx)ok=false; seenTx=true; txSize=num(a,"size");
            ok &= txSize>4096 && txSize<=CH585_FIRMWARE_STAGING_DATA_SIZE && txSize%4==0 &&
                num(a,"applicationOffset")==4096 && num(a,"applicationSize")==txSize-4096 &&
                strcmp(str(a,"imageFormat"),"ch585-tx-combined")==0;
            snprintf(txHash,sizeof(txHash),"%s",str(a,"sha256"));
            snprintf(txAppHash,sizeof(txAppHash),"%s",str(a,"applicationSha256"));
            snprintf(txVersion,sizeof(txVersion),"%s",str(a,"version"));
            snprintf(txBuild,sizeof(txBuild),"%s",str(a,"buildId"));
        }
    }
    ok &= seenStm && seenTx && strlen(txHash)==64 && strlen(txAppHash)==64 && targetBuild[0] && txBuild[0] && targetVersion[0];
    if(preflight) {
        xora_release_identity_t tx={};
        ok &= self.protocol==1 && USB_BOARD_LINK.getReleaseIdentity(tx) && tx.protocol==1 &&
              range(c,"txMaintenance",tx.maintenance) && range(c,"stm32Maintenance",self.maintenance);
    }
    hash(state.manifest,state.manifestSize,releaseDigest); cJSON_Delete(root);
    return ok ? true : fail("Incompatible release, configuration or maintenance protocol");
}
bool verifyImages(bool applicationOnly=false) {
    if(firmware_metadata_verify_signature(&state.target)!=FIRMWARE_VALID)return false;
    for(uint32_t i=0;i<FIRMWARE_COMPONENT_COUNT;i++) {
        const auto& c=state.target.components[i];
        if(c.active && (!applicationOnly || strcmp(c.name,"application")==0) && !flashHash(c.address,c.size,c.sha256))return false;
    }
    return applicationOnly || (flashHash(CH585_FIRMWARE_STAGING_DATA_ADDR,txSize,txHash) &&
        flashHash(CH585_FIRMWARE_STAGING_DATA_ADDR+4096,txSize-4096,txAppHash));
}
bool verifyTx() {
    xora_release_identity_t tx={};
    if(!CH585_IAP_CLIENT.validateApplication(&tx))return false;
    return strcmp(tx.version,txVersion)==0 && strcmp(tx.build_id,txBuild)==0 && tx.protocol==1 && tx.maintenance==1;
}
bool terminalFailure(const char* error) { fail(error); return persist(Failed); }
}

ReleaseInstaller& ReleaseInstaller::instance(){static ReleaseInstaller i;return i;}
const char* ReleaseInstaller::error() const{load();return lastError;}
bool ReleaseInstaller::busy(){load();return storageFault || declaring || (state.phase>=Receiving && state.phase<=Verifying) || state.phase==Failed;}
bool ReleaseInstaller::owns(const char* session){load();return session && !storageFault && strcmp(session,state.session)==0 && busy();}
bool ReleaseInstaller::failed(){load();return storageFault || state.phase==Failed ||
    (state.phase>=Activated && state.phase<=Committing && lastError[0]);}
bool ReleaseInstaller::bootPending(){load();return !storageFault && state.phase>=Activated && state.phase<=Committing;}
bool ReleaseInstaller::protectConfiguration(){load();return storageFault || (state.phase>=Activated && state.phase<=Failed);}
bool ReleaseInstaller::begin(const char* session,uint32_t size) {
    load();
    if(!session || !*session || strlen(session)>32 || size<=64+METADATA_STRUCT_SIZE || size>sizeof(declaration))return fail("Invalid declaration");
    if(busy())return fail("Resolve the existing installation before starting another");
    auto* fm=FirmwareManager::GetInstance(); const auto* metadata=fm->GetCurrentMetadata();
    if(fm->IsUpgradeActive() || CH585_FIRMWARE_UPDATE.isPending())return fail("Another firmware update is active");
    if(!metadata || metadata->target_slot!=fm->GetCurrentSlot())return fail("Running slot and metadata disagree");
    snprintf(state.session,sizeof(state.session),"%s",session);
    state.source=*metadata; state.configVersion=STORAGE_MANAGER.config.version;
    state.attempts=0; declarationSize=size; declarationReceived=0; declaring=true;
    memset(received,0,sizeof(received));
    lastError[0]=0; return true;
}
bool ReleaseInstaller::upload(const char* session,const char* component,const ChunkData& chunk) {
    if(!owns(session) || !component || !chunk.data || !chunk.chunk_size || chunk.chunk_size>4096)return fail("Invalid transaction chunk");
    char digest[65]; if(!hash(chunk.data,chunk.chunk_size,digest) || strcmp(digest,chunk.checksum))return fail("Chunk digest mismatch");
    if(strcmp(component,"declaration")==0 && !declaring && state.phase==Receiving &&
        declarationReceived==declarationSize && chunk.chunk_offset<=declarationSize &&
        chunk.chunk_size<=declarationSize-chunk.chunk_offset)
        return memcmp(declaration+chunk.chunk_offset,chunk.data,chunk.chunk_size)==0;
    if(strcmp(component,"declaration")==0 && declaring) {
        if(chunk.chunk_offset>declarationSize || chunk.chunk_size>declarationSize-chunk.chunk_offset)return fail("Declaration boundary");
        if(chunk.chunk_offset<declarationReceived)return chunk.chunk_offset+chunk.chunk_size<=declarationReceived &&
            memcmp(declaration+chunk.chunk_offset,chunk.data,chunk.chunk_size)==0;
        if(chunk.chunk_offset!=declarationReceived)return fail("Declaration out of sequence");
        memcpy(declaration+declarationReceived,chunk.data,chunk.chunk_size); declarationReceived+=chunk.chunk_size;
        if(declarationReceived<declarationSize)return true;
        memcpy(state.signature,declaration,64); memcpy(&state.target,declaration+64,METADATA_STRUCT_SIZE);
        state.manifestSize=declarationSize-64-METADATA_STRUCT_SIZE;
        memcpy(state.manifest,declaration+64+METADATA_STRUCT_SIZE,state.manifestSize);state.manifest[state.manifestSize]=0;
        if(!parseManifest(true))return false;
        if(!FirmwareManager::GetInstance()->CreateUpgradeSession(session,&state.target))return fail("STM32 manifest rejected");
        uint8_t sha[32];for(unsigned i=0;i<32;i++){unsigned n=0;if(sscanf(txHash+i*2,"%2x",&n)!=1)return false;sha[i]=n;}
        if(!CH585_FIRMWARE_UPDATE.begin(txSize,sha))return fail("TX staging unavailable");
        declaring=false;return persist(Receiving);
    }
    if(state.phase!=Receiving || declaring)return fail("Installation is not receiving");
    if(strcmp(component,"tx")==0) {
        if(chunk.target_address!=CH585_FIRMWARE_STAGING_DATA_ADDR+chunk.chunk_offset)return fail("TX address mismatch");
        return CH585_FIRMWARE_UPDATE.write(chunk.chunk_offset,chunk.data,chunk.chunk_size);
    }
    for(unsigned i=0;i<FIRMWARE_COMPONENT_COUNT;i++) {
        const auto& c=state.target.components[i];if(strcmp(c.name,component))continue;
        if(!c.active || chunk.chunk_offset>c.size || chunk.chunk_size>c.size-chunk.chunk_offset ||
           chunk.target_address!=c.address+chunk.chunk_offset)return fail("Component chunk boundary");
        if(chunk.chunk_offset<received[i])return chunk.chunk_offset+chunk.chunk_size<=received[i] &&
            flashHash(chunk.target_address,chunk.chunk_size,chunk.checksum);
        if(chunk.chunk_offset!=received[i])return fail("Component chunk out of sequence");
        if(!FirmwareManager::GetInstance()->ProcessFirmwareChunk(session,component,&chunk))return false;
        received[i]+=chunk.chunk_size;return true;
    }
    return fail("Unknown release component");
}
bool ReleaseInstaller::prepare(const char* session) {
    if(!owns(session))return fail("Unknown transaction");
    if(state.phase==Prepared)return true;
    if(state.phase!=Receiving || !parseManifest(false) || !verifyImages())return fail("Staged images failed verification");
    return persist(Prepared);
}
bool ReleaseInstaller::activate(const char* session) {
    if(!owns(session))return fail("Unknown transaction");
    if(state.phase>=Activated && state.phase<=Verifying)return true;
    if(state.phase!=Prepared || !parseManifest(false) || !verifyImages())return fail("Installation is not prepared");
    if(!persist(Activated))return false; resetAt=HAL_GetTick()+500;return true;
}
bool ReleaseInstaller::abort(const char* session) {
    if(!owns(session) || (!declaring && state.phase!=Receiving && state.phase!=Prepared))return fail("Installation cannot be cancelled");
    declaring=false; FirmwareManager::GetInstance()->ForceCleanupSession();return persist(Aborted);
}
bool ReleaseInstaller::retry() {
    load();if(!failed() || !parseManifest(false) || !verifyImages())return fail("No verified image to retry");
    state.attempts=0;if(!persist(Activated))return false;resetAt=HAL_GetTick()+500;return true;
}
bool ReleaseInstaller::runBoot() {
    if(!bootPending())return false;
    if(!parseManifest(false) || !verifyImages()) {terminalFailure("Persisted installation verification failed");return false;}
    if(state.phase<=TxWriting) {
        if(state.attempts>=2){terminalFailure("TX update needs a local retry");return false;}
        ++state.attempts;if(!persist(TxWriting))return false;
        const bool ok=CH585_IAP_CLIENT.programCombinedImage(CH585_FIRMWARE_STAGING_DATA_ADDR,txSize) && verifyTx();
        if(!ok) {
            if(state.attempts<2 && persist(Activated)){MainRuntime_RequestReset();return true;}
            else terminalFailure("TX verification failed; use local retry");
            return false;
        }
        if(!persist(TxVerified))return false;
    }
    if(!verifyTx()){terminalFailure("TX identity changed before STM32 commit");return false;}
    if(!persist(Committing))return false;
    // The original signed metadata is written only after both staged images and TX
    // runtime identity have been verified. No bootloader or internal Flash writes.
    if(QSPI_W25Qxx_WriteBuffer_WithXIPOrNot(reinterpret_cast<uint8_t*>(&state.target),
        METADATA_ADDR-EXTERNAL_FLASH_BASE,sizeof(state.target))!=QSPI_W25Qxx_OK) {
        terminalFailure("STM32 metadata commit failed");return false;
    }
    FirmwareMetadata check;
    if(!read(METADATA_ADDR,&check,sizeof(check)) || memcmp(&check,&state.target,sizeof(check))) {
        terminalFailure("STM32 metadata readback failed");return false;
    }
    if(!persist(Verifying))return false;
    MainRuntime_RequestReset();return true;
}
void ReleaseInstaller::poll() {
    if(resetAt && static_cast<int32_t>(HAL_GetTick()-resetAt)>=0){resetAt=0;MainRuntime_RequestReset();}
}
void ReleaseInstaller::verifyStartup(bool configurationReadable) {
    load();if(state.phase!=Verifying)return;
    if(!configurationReadable || !parseManifest(false) || CONFIG_VERSION!=targetConfig || FirmwareManager::GetInstance()->GetCurrentSlot()!=state.target.target_slot ||
        strcmp(self.version,state.target.firmware_version) || strcmp(self.build_id,targetBuild) ||
        STORAGE_MANAGER.config.version!=targetConfig || !verifyImages(true)) {
        terminalFailure("Installed STM32 or configuration does not match release");return;
    }
    // Run before screen/USB/input startup. This selects maintenance locally and
    // tears it down after verification; no browser or active USB session is needed.
    if(!verifyTx()) {
        terminalFailure("Installed TX does not match release");return;
    }
    snprintf(state.confirmedVersion,sizeof(state.confirmedVersion),"%s",targetVersion);
    snprintf(state.confirmedDigest,sizeof(state.confirmedDigest),"%s",releaseDigest);
    (void)persist(Completed);
}
cJSON* ReleaseInstaller::inventory() {
    load();cJSON* out=cJSON_CreateObject();
    if(state.manifestSize && !storageFault)(void)parseManifest(false);
    cJSON_AddNumberToObject(out,"protocol",self.protocol);
    cJSON_AddStringToObject(out,"deviceModel",DEVICE_MODEL_STRING);
    cJSON_AddStringToObject(out,"hardwareVersion",HARDWARE_VERSION_STRING);
    cJSON_AddStringToObject(out,"currentSlot",FirmwareManager::GetInstance()->GetCurrentSlot()==FIRMWARE_SLOT_A?"A":"B");
    cJSON_AddNumberToObject(out,"configVersion",STORAGE_MANAGER.config.version);
    const auto* meta=FirmwareManager::GetInstance()->GetCurrentMetadata();
    cJSON_AddNumberToObject(out,"securityVersion",meta?meta->security_version:0);
    cJSON_AddBoolToObject(out,"metadataConsistent",meta && meta->target_slot==FirmwareManager::GetInstance()->GetCurrentSlot());
    auto addIdentity=[out](const char* key,const xora_release_identity_t& identity){
        cJSON* v=cJSON_AddObjectToObject(out,key);cJSON_AddStringToObject(v,"version",identity.version);
        cJSON_AddStringToObject(v,"buildId",identity.build_id);cJSON_AddNumberToObject(v,"maintenance",identity.maintenance);
        cJSON_AddNumberToObject(v,"protocol",identity.protocol);
    };
    addIdentity("stm32",self);xora_release_identity_t tx={};
    const bool txKnown=USB_BOARD_LINK.isCompatible() && USB_BOARD_LINK.getReleaseIdentity(tx);
    if(txKnown)addIdentity("tx",tx);
    bool matched=false;
    if(state.phase==Completed && parseManifest(false)) matched=txKnown && meta && meta->target_slot==state.target.target_slot &&
        FirmwareManager::GetInstance()->GetCurrentSlot()==state.target.target_slot && strcmp(tx.version,txVersion)==0 && strcmp(tx.build_id,txBuild)==0 &&
        strcmp(self.build_id,targetBuild)==0 && strcmp(self.version,state.target.firmware_version)==0 && verifyImages(true);
    cJSON_AddStringToObject(out,"installationState",storageFault?"unknown":state.phase==Completed?(matched?"installed":"mixed"):
        (state.phase>=Receiving && state.phase<=Failed)?"incomplete":"unknown");
    cJSON_AddStringToObject(out,"confirmedVersion",state.confirmedVersion);
    cJSON_AddStringToObject(out,"confirmedDigest",state.confirmedDigest);
    cJSON_AddStringToObject(out,"sessionId",state.session);
    cJSON_AddStringToObject(out,"phase",declaring?"declaring":storageFault?"failed":names[state.phase]);
    cJSON_AddStringToObject(out,"targetVersion",targetVersion);
    cJSON_AddStringToObject(out,"targetDigest",releaseDigest);
    cJSON_AddStringToObject(out,"error",storageFault?"Installation journal is unreadable":state.error);
    cJSON_AddBoolToObject(out,"canAbort",declaring || state.phase==Receiving || state.phase==Prepared);
    cJSON_AddBoolToObject(out,"canRetry",state.phase==Failed);
    cJSON_AddNumberToObject(out,"txReceived",CH585_FIRMWARE_UPDATE.receivedSize());
    return out;
}
