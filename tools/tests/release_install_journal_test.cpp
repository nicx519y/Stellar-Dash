#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include "../../application/Src/firmware/release_installer.cpp"

// The read-only host decoder must agree with this production journal layout.
static_assert(sizeof(Snapshot)==10712 && offsetof(Snapshot,crc)==10124 &&
    offsetof(Snapshot,crc2)==10688 && offsetof(Snapshot,commit2)==10692 &&
    offsetof(Snapshot,crc3)==10704 && offsetof(Snapshot,commit3)==10708);

static std::vector<uint8_t> flash(0x800000,0xff);
static int budget=-1;
static bool readFailure=false;
static bool qspiMapped=true, exitFailure=false, enterFailure=false;
static unsigned modeExits=0,modeEnters=0,rawReads=0;
static int readsBeforeFailure=-1;
static bool signaturesValid=false, txHealthy=true, programOk=true, metadataOk=true;
static unsigned programmed=0, verified=0, metadataWrites=0, resets=0, restoredWrites=0;
static bool restoreOk=true,backupReadOk=true,changedTx=false,changedIdentity=false,oldImage=false;
static Ch585IapTransferMode installMode=Ch585IapTransferMode::Dma, recoveryMode=Ch585IapTransferMode::Dma;
static std::vector<uint8_t> txImage(XORA_TX_BACKUP_APP_BYTES,0x5a);
static xora_release_identity_t oldIdentity(){auto id=self;id.component=2;strcpy(id.version,"old-tx");strcpy(id.build_id,"old-tx-build");return id;}
static FirmwareSlot runningSlot=FIRMWARE_SLOT_A;
TestStorage testStorage = {{34}};
TestLink testLink;
static uint16_t backupChunk=XORA_TX_BULK_READ_BYTES;
static unsigned backupReads=0;
bool TestLink::getReleaseIdentity(xora_release_identity_t& id){id=oldIdentity();if(changedIdentity && backupSecondPass)strcpy(id.build_id,"changed");return true;}
bool TestLink::getTxImageInfo(){return true;}
uint16_t TestLink::txImageReadBytes() const{return backupChunk;}
bool TestLink::readTxImage(uint32_t at,uint8_t* p,uint16_t n){
    assert(n && n<=backupChunk && at<=XORA_TX_BACKUP_APP_BYTES && n<=XORA_TX_BACKUP_APP_BYTES-at);++backupReads;
    if(!backupReadOk)return false;memcpy(p,txImage.data()+at,n);
    if(changedTx && backupSecondPass)p[0]^=1;
    return true;
}
bool TestLink::isCompatible(){return false;}
uint32_t HAL_GetTick(){return 1;}
void MainRuntime_RequestReset(){resets++;}
extern "C" bool firmware_release_verify_bytes(const uint8_t*,uint32_t,const uint8_t[64]){return signaturesValid;}
extern "C" FirmwareValidationResult firmware_metadata_verify_signature(const FirmwareMetadata*){return signaturesValid?FIRMWARE_VALID:FIRMWARE_INVALID_SIGNATURE;}
FirmwareManager::FirmwareManager():current_metadata{},metadata_loaded(false),current_session(nullptr),session_active(false){}
FirmwareManager* FirmwareManager::GetInstance(){static FirmwareManager fm;return &fm;}
const FirmwareMetadata* FirmwareManager::GetCurrentMetadata(){return nullptr;}
FirmwareSlot FirmwareManager::GetCurrentSlot(){return runningSlot;}
bool FirmwareManager::CreateUpgradeSession(const char*,const FirmwareMetadata*){return false;}
bool FirmwareManager::ProcessFirmwareChunk(const char*,const char*,const ChunkData*){return false;}
void FirmwareManager::ForceCleanupSession(){}
bool Ch585FirmwareUpdate::begin(uint32_t,const uint8_t[32]){return false;}
bool Ch585FirmwareUpdate::isPending()const{return false;}
bool Ch585FirmwareUpdate::write(uint32_t,const uint8_t*,uint32_t){return false;}
bool Ch585IapClient::validateApplication(xora_release_identity_t* id){
    verified++;if(oldImage){*id=oldIdentity();return restoreOk;}if(!txHealthy)return false;*id=self;id->component=2;strcpy(id->version,"1.0.0");strcpy(id->build_id,"tx-build");return true;
}
bool Ch585IapClient::programCombinedImage(uint32_t at,uint32_t n,TransferCheckpoint checkpoint){assert(at==CH585_FIRMWARE_STAGING_DATA_ADDR && n==8192);if(checkpoint && !checkpoint(installMode))return false;programmed++;oldImage=false;return programOk;}
bool Ch585IapClient::programApplicationImage(uint32_t at,uint32_t n,TransferCheckpoint checkpoint){
    if(checkpoint && !checkpoint(recoveryMode))return false;
    assert(at==state.backupAddress && n==XORA_TX_BACKUP_APP_BYTES);restoredWrites++;oldImage=true;return restoreOk;
}
int8_t QSPI_W25Qxx_WriteBuffer_WithXIPOrNot(uint8_t* p,uint32_t at,uint32_t n){
    assert(at==METADATA_ADDR-EXTERNAL_FLASH_BASE && n==METADATA_STRUCT_SIZE && verified>0);
    metadataWrites++;if(!metadataOk)return -1;memcpy(flash.data()+at,p,n);return 0;
}
static bool step(){return budget<0 || budget-- > 0;}
int8_t QSPI_W25Qxx_ReadBuffer(uint8_t* p,uint32_t at,uint32_t n){
    ++rawReads;
    if(readFailure || (readsBeforeFailure>=0 && readsBeforeFailure--==0) || at>flash.size() || n>flash.size()-at)return -1;memcpy(p,flash.data()+at,n);return 0;
}
int8_t QSPI_W25Qxx_ReadBuffer_WithXIPOrNot(uint8_t* p,uint32_t at,uint32_t n){return QSPI_W25Qxx_ReadBuffer(p,at,n);}
bool QSPI_W25Qxx_IsMemoryMappedMode(){return qspiMapped;}
int8_t QSPI_W25Qxx_ExitMemoryMappedMode(){++modeExits;qspiMapped=false;return exitFailure?-1:0;}
int8_t QSPI_W25Qxx_EnterMemoryMappedMode(){++modeEnters;if(enterFailure)return -1;qspiMapped=true;return 0;}
int8_t QSPI_W25Qxx_SectorErase(uint32_t at){if(!step())return -1;assert(at%4096==0);memset(flash.data()+at,0xff,4096);return 0;}
int8_t QSPI_W25Qxx_WritePage(uint8_t* p,uint32_t at,uint16_t n){
    assert(n && n<=256 && (at%256)+n<=256);
    if(!step())return -1;
    for(unsigned i=0;i<n;i++){assert((flash[at+i]&p[i])==p[i]);flash[at+i]&=p[i];}return 0;
}
static void fixture(FirmwareSlot target){
    backupReads=0;
    installMode=recoveryMode=Ch585IapTransferMode::Dma;
    flash.assign(flash.size(),0xff);memset(&state,0,sizeof(state));loaded=false;storageFault=false;declaring=false;legacyJournal=false;backupActive=false;bank=0;budget=-1;load();
    signaturesValid=txHealthy=programOk=metadataOk=true;programmed=verified=metadataWrites=resets=restoredWrites=0;restoreOk=backupReadOk=true;changedTx=changedIdentity=oldImage=false;
    state.configVersion=34;strcpy(state.session,"test-release");state.target.target_slot=target;strcpy(state.target.firmware_version,self.version);
    auto& app=state.target.components[0];strcpy(app.name,"application");app.active=1;app.address=target==FIRMWARE_SLOT_A?0x90000000:0x902b0000;app.size=64;
    hash(flash.data()+app.address-EXTERNAL_FLASH_BASE,app.size,app.sha256);
    auto& web=state.target.components[1];strcpy(web.name,"webresources");web.address=app.address+0x100000;state.target.webresources_optional=1;
    runningSlot=target==FIRMWARE_SLOT_A?FIRMWARE_SLOT_B:FIRMWARE_SLOT_A;
    state.source=state.target;state.source.target_slot=runningSlot;state.source.components[0].address=runningSlot==FIRMWARE_SLOT_A?0x90000000:0x902b0000;
    state.source.components[1].address=state.source.components[0].address+0x100000;state.oldStm=self;
    memcpy(flash.data()+METADATA_ADDR-EXTERNAL_FLASH_BASE,&state.source,sizeof(state.source));
    char metaHash[65],full[65],part[65];hash(&state.target,sizeof(state.target),metaHash);
    hash(flash.data()+CH585_FIRMWARE_STAGING_DATA_ADDR-EXTERNAL_FLASH_BASE,8192,full);
    hash(flash.data()+CH585_FIRMWARE_STAGING_DATA_ADDR-EXTERNAL_FLASH_BASE+4096,4096,part);
    state.manifestSize=snprintf(state.manifest,sizeof(state.manifest),
        "{\"schemaVersion\":2,\"product\":\"XORA\",\"deviceModel\":\"%s\",\"hardwareVersion\":\"%s\",\"version\":\"3.0.0\","
        "\"bootSecurityMode\":\"unlocked-development\",\"requiresManualLifecycleProvisioning\":false,"
        "\"install\":{\"protocol\":2,\"order\":\"tx-then-stm32\",\"configRead\":{\"min\":34,\"max\":34},\"configWrite\":34,"
        "\"stm32Maintenance\":{\"min\":2,\"max\":2},\"txMaintenance\":{\"min\":2,\"max\":2}},\"artifacts\":["
        "{\"component\":\"stm32\",\"slot\":\"%s\",\"metadataSha256\":\"%s\",\"version\":\"%s\",\"buildId\":\"%s\"},"
        "{\"component\":\"tx\",\"size\":8192,\"applicationOffset\":4096,\"applicationSize\":4096,\"imageFormat\":\"ch585-tx-combined\","
        "\"sha256\":\"%s\",\"applicationSha256\":\"%s\",\"version\":\"1.0.0\",\"buildId\":\"tx-build\"}]}",
        DEVICE_MODEL_STRING,HARDWARE_VERSION_STRING,target==FIRMWARE_SLOT_A?"A":"B",metaHash,self.version,self.build_id,full,part);
    assert(persist(Declared));assert(RELEASE_INSTALLER.backup(state.session));
    for(int i=0;state.phase==BackingUp && i<2000;i++)RELEASE_INSTALLER.poll();
    assert(state.phase==Receiving && backupValid());assert(persist(Prepared));
}
int main(){
    // Full 444 KiB verification keeps all 888 reads but only one QSPI reset,
    // rather than 888 resets / 4440 ms of fixed delay. No extra RAM buffer.
    char imageDigest[65];hash(flash.data(),XORA_TX_BACKUP_APP_BYTES,imageDigest);
    assert(flashHash(EXTERNAL_FLASH_BASE,XORA_TX_BACKUP_APP_BYTES,imageDigest));
    assert(qspiMapped && modeExits==1 && modeEnters==1 && rawReads==888);
    auto exits=modeExits,enters=modeEnters;
    qspiMapped=false;assert(flashHash(EXTERNAL_FLASH_BASE,XORA_TX_BACKUP_APP_BYTES,imageDigest));
    assert(!qspiMapped && modeExits==exits && modeEnters==enters);qspiMapped=true;
    readsBeforeFailure=2;assert(!flashHash(EXTERNAL_FLASH_BASE,XORA_TX_BACKUP_APP_BYTES,imageDigest));
    assert(qspiMapped);readsBeforeFailure=-1;
    flash[512]^=1;assert(!flashHash(EXTERNAL_FLASH_BASE,XORA_TX_BACKUP_APP_BYTES,imageDigest));assert(qspiMapped);flash[512]^=1;
    exits=modeExits;assert(!flashHash(EXTERNAL_FLASH_BASE-1,512,imageDigest));
    assert(!flashHash(EXTERNAL_FLASH_BASE+W25Qxx_FlashSize-1,2,imageDigest));assert(modeExits==exits);
    exitFailure=true;assert(!flashHash(EXTERNAL_FLASH_BASE,512,imageDigest));assert(qspiMapped);exitFailure=false;
    enterFailure=true;assert(!flashHash(EXTERNAL_FLASH_BASE,XORA_TX_BACKUP_APP_BYTES,imageDigest));assert(!qspiMapped);enterFailure=false;qspiMapped=true;
    const auto blank=flash;
    for(uint16_t chunk:{uint16_t(XORA_TX_READ_BYTES),uint16_t(XORA_TX_BULK_READ_BYTES)}) {
        backupChunk=chunk;fixture(FIRMWARE_SLOT_B);
        assert(backupReads==2u*((XORA_TX_BACKUP_APP_BYTES+chunk-1u)/chunk));
        assert(state.backupSize==XORA_TX_BACKUP_APP_BYTES && backupValid());
    }
    backupChunk=XORA_TX_BULK_READ_BYTES;
    assert(xora_tx_read_range_valid(0,48));
    assert(xora_tx_read_range_valid(XORA_TX_BACKUP_APP_BYTES-48,48));
    assert(!xora_tx_read_range_valid(0,0));assert(!xora_tx_read_range_valid(0,49));
    assert(!xora_tx_read_range_valid(XORA_TX_BACKUP_APP_BYTES-47,48));
    assert(!xora_tx_read_range_valid(0xffffffffu,48));
    // The very first unactivated snapshot has no previous bank. A torn body
    // with the other bank erased must not strand the source firmware.
    for(int stop=0;stop<70;stop++){
        flash=blank;loaded=false;storageFault=false;bank=0;budget=-1;load();
        budget=stop;(void)persist(Receiving);budget=-1;loaded=false;storageFault=false;load();
        assert(!storageFault && (state.phase==Empty || state.phase==Receiving));
    }
    flash=blank;loaded=false;storageFault=false;bank=0;
    loaded=false;load();assert(!storageFault && state.phase==Empty);
    strcpy(state.session,"transaction");strcpy(state.confirmedVersion,"1.0.0");
    assert(persist(Prepared));const auto committed=flash;
    const auto firstGeneration=state.generation;
    // Power interruption at every erase/program step must retain the old bank
    // or the fully committed new bank, never a partly written state.
    for(int stop=0;stop<70;stop++){
        flash=committed;loaded=false;storageFault=false;budget=-1;load();
        budget=stop;(void)persist(Activated);budget=-1;loaded=false;storageFault=false;load();
        assert(!storageFault);
        assert(state.phase==Prepared || state.phase==Activated);
        assert(state.generation==firstGeneration || state.generation==firstGeneration+1);
        assert(strcmp(state.confirmedVersion,"1.0.0")==0);
        assert(flash[METADATA_ADDR-EXTERNAL_FLASH_BASE]==0xff);
        assert(flash[LOG_STORAGE_ADDR-EXTERNAL_FLASH_BASE]==0xff);
    }
    readFailure=true;loaded=false;storageFault=false;load();assert(storageFault);
    assert(!persist(Activated));readFailure=false;
    for(auto target:{FIRMWARE_SLOT_A,FIRMWARE_SLOT_B}){
        fixture(target);runningSlot=target==FIRMWARE_SLOT_A?FIRMWARE_SLOT_B:FIRMWARE_SLOT_A;
        assert(RELEASE_INSTALLER.activate(state.session));assert(state.phase==Activated);
        // Lost activation ACK is idempotent and cannot cancel an active install.
        assert(RELEASE_INSTALLER.activate(state.session));assert(!RELEASE_INSTALLER.abort(state.session));
        assert(RELEASE_INSTALLER.runBoot());assert(state.phase==Verifying && programmed==1 && metadataWrites==1 && resets==1);
        runningSlot=target;RELEASE_INSTALLER.verifyStartup(true);
        assert(state.phase==Completed && !strcmp(state.confirmedVersion,"3.0.0"));
        loaded=false;load();assert(state.txInstallMode==2 && state.txRecoveryMode==0);
        // An old controller accepts this complete prefix, including its final marker.
        assert(state.magic==0x33524f58u && state.commit2==0x54494d43u && state.crc2==crc32(&state,offsetof(Snapshot,crc2)));
        cJSON* out=RELEASE_INSTALLER.inventory();assert(!strcmp(cJSON_GetObjectItem(out,"txInstallMode")->valuestring,"dma"));cJSON_Delete(out);
    }
    // One new-image attempt, then rollback to the exact old TX and controller.
    fixture(FIRMWARE_SLOT_B);programOk=false;installMode=Ch585IapTransferMode::SmallPacket;assert(RELEASE_INSTALLER.activate(state.session));
    assert(RELEASE_INSTALLER.runBoot());assert(state.phase==RollbackVerifying && programmed==1 && restoredWrites==1);
    RELEASE_INSTALLER.verifyStartup(true);assert(state.phase==Restored && !RELEASE_INSTALLER.busy());
    assert(runningSlot==FIRMWARE_SLOT_A && !memcmp(flash.data()+METADATA_ADDR-EXTERNAL_FLASH_BASE,&state.source,sizeof(state.source)));
    assert(!RELEASE_INSTALLER.retry());
    loaded=false;load();assert(state.txInstallMode==1 && state.txRecoveryMode==2);
    fixture(FIRMWARE_SLOT_B);txHealthy=false;assert(RELEASE_INSTALLER.activate(state.session));
    assert(RELEASE_INSTALLER.runBoot());assert(state.phase==RollbackVerifying && programmed==1 && restoredWrites==1);
    RELEASE_INSTALLER.verifyStartup(true);assert(state.phase==Restored);
    fixture(FIRMWARE_SLOT_B);programOk=restoreOk=false;assert(RELEASE_INSTALLER.activate(state.session));
    assert(!RELEASE_INSTALLER.runBoot());assert(state.phase==RestoreFailed && state.restoreAttempts==2 && restoredWrites==2);
    assert(strstr(state.installError,"stage=") && strstr(state.recoveryError,"offset="));
    assert(state.txInstallMode==2 && state.txRecoveryMode==2);
    loaded=false;load();assert(state.restoreAttempts==2 && !RELEASE_INSTALLER.bootPending());assert(!RELEASE_INSTALLER.runBoot());
    assert(strstr(state.installError,"stage=") && strstr(state.recoveryError,"offset="));
    // Interrupted recovery cannot reset its persisted attempt counter.
    fixture(FIRMWARE_SLOT_B);state.restoreAttempts=1;strcpy(state.installError,"interrupted");assert(persist(Restoring));
    loaded=false;load();assert(RELEASE_INSTALLER.runBoot());assert(restoredWrites==1 && state.restoreAttempts==2);
    fixture(FIRMWARE_SLOT_B);state.restoreAttempts=2;assert(persist(Restoring));loaded=false;load();
    assert(!RELEASE_INSTALLER.runBoot() && restoredWrites==0 && state.phase==RestoreFailed);
    fixture(FIRMWARE_SLOT_B);state.restoreAttempts=2;oldImage=true;assert(persist(Restoring));loaded=false;load();
    assert(RELEASE_INSTALLER.runBoot() && restoredWrites==0 && state.phase==RollbackVerifying);
    // A lost declaration/backup ACK can be replayed after its phase advances.
    fixture(FIRMWARE_SLOT_B);
    declarationSize=declarationReceived=24;memset(declaration,0x42,declarationSize);
    ChunkData repeated={};repeated.data=declaration;repeated.chunk_size=24;
    hash(repeated.data,repeated.chunk_size,repeated.checksum);
    for(Phase phase:{Declared,BackingUp,Receiving,Prepared}){
        assert(persist(phase));assert(RELEASE_INSTALLER.upload(state.session,"declaration",repeated));
        uint8_t changed[24];memcpy(changed,declaration,sizeof(changed));changed[0]^=1;
        repeated.data=changed;hash(changed,sizeof(changed),repeated.checksum);
        assert(!RELEASE_INSTALLER.upload(state.session,"declaration",repeated));
        repeated.data=declaration;hash(declaration,24,repeated.checksum);
    }
    assert(RELEASE_INSTALLER.backup(state.session));assert(restoredWrites==0 && programmed==0);
    state.backupReady=0;assert(!RELEASE_INSTALLER.backup(state.session));
    // Missing/damaged backup never permits a new TX write.
    for(int fault=0;fault<3;fault++){
        fixture(FIRMWARE_SLOT_B);
        if(fault==0)state.backupReady=0;
        if(fault==1)state.backupAddress++;
        if(fault==2)flash[state.backupAddress-EXTERNAL_FLASH_BASE]^=1;
        assert(!RELEASE_INSTALLER.activate(state.session) && programmed==0);
        assert(persist(Activated));assert(!RELEASE_INSTALLER.runBoot());assert(programmed==0 && state.phase==RestoreFailed);
    }
    // Backup source changes or read failure block component upload and activation.
    for(int fault=0;fault<3;fault++){
        fixture(FIRMWARE_SLOT_B);assert(persist(Declared));backupActive=false;
        backupReadOk=fault!=0;changedTx=fault==1;assert(RELEASE_INSTALLER.backup(state.session));changedIdentity=fault==2;
        for(int i=0;state.phase==BackingUp && i<2000;i++)RELEASE_INSTALLER.poll();
        assert(state.phase==Failed && !state.backupReady && programmed==0);
    }
    // Header body and commit interruptions never produce a usable backup.
    for(int stop=0;stop<3;stop++){
        fixture(FIRMWARE_SLOT_B);assert(persist(Declared));backupActive=false;assert(RELEASE_INSTALLER.backup(state.session));
        while(state.phase==BackingUp && !(backupSecondPass && backupOffset>=XORA_TX_BACKUP_APP_BYTES-768))RELEASE_INSTALLER.poll();
        budget=stop;RELEASE_INSTALLER.poll();budget=-1;loaded=false;backupActive=false;load();
        assert(!backupValid());assert(!RELEASE_INSTALLER.activate(state.session));
    }
    // A failure after metadata switch verifies the source, restores metadata, then reboots.
    fixture(FIRMWARE_SLOT_B);assert(RELEASE_INSTALLER.activate(state.session));assert(RELEASE_INSTALLER.runBoot());
    runningSlot=FIRMWARE_SLOT_B;RELEASE_INSTALLER.verifyStartup(false);assert(state.phase==RollbackVerifying);
    assert(!memcmp(flash.data()+METADATA_ADDR-EXTERNAL_FLASH_BASE,&state.source,sizeof(state.source)));
    runningSlot=FIRMWARE_SLOT_A;RELEASE_INSTALLER.verifyStartup(true);assert(state.phase==Restored);
    // A v2 record retains its backup and starts with unknown transfer modes.
    fixture(FIRMWARE_SLOT_B);state.magic=0x33524f58u;state.commit2=0x54494d43u;state.crc2=crc32(&state,offsetof(Snapshot,crc2));
    memset(flash.data()+XORA_RELEASE_BANK_A-EXTERNAL_FLASH_BASE,0xff,XORA_RELEASE_BANK_SIZE);
    memset(flash.data()+XORA_RELEASE_BANK_B-EXTERNAL_FLASH_BASE,0xff,XORA_RELEASE_BANK_SIZE);
    memcpy(flash.data()+XORA_RELEASE_BANK_A-EXTERNAL_FLASH_BASE,&state,offsetof(Snapshot,txInstallMode));loaded=false;load();
    assert(!storageFault && state.backupReady && state.txInstallMode==0 && state.txRecoveryMode==0 && backupValid());
    assert(persist(Activated));const auto modeBase=flash;
    // A mode checkpoint must commit before BEGIN; a torn checkpoint cannot claim it.
    for(int stop=0;stop<70;stop++){
        flash=modeBase;budget=-1;loaded=false;storageFault=false;load();budget=stop;
        const bool ok=recordInstallMode(Ch585IapTransferMode::Dma);budget=-1;loaded=false;load();
        assert(!storageFault && state.txRecoveryMode==0 && state.txInstallMode==(ok?2u:0u));
    }
    // Legacy failed transactions are decoded and reported, never silently erased or retried.
    fixture(FIRMWARE_SLOT_B);state.magic=0x32524f58u;state.phase=Failed;state.commit=0x54494d43u;state.crc=crc32(&state,offsetof(Snapshot,crc));
    memset(flash.data()+XORA_RELEASE_BANK_A-EXTERNAL_FLASH_BASE,0xff,XORA_RELEASE_BANK_SIZE);
    memset(flash.data()+XORA_RELEASE_BANK_B-EXTERNAL_FLASH_BASE,0xff,XORA_RELEASE_BANK_SIZE);
    memcpy(flash.data()+XORA_RELEASE_BANK_A-EXTERNAL_FLASH_BASE,&state,offsetof(Snapshot,restoreAttempts));loaded=false;load();
    assert(legacyJournal && state.phase==Failed && !state.backupReady && !RELEASE_INSTALLER.bootPending());
    assert(!strcmp(RELEASE_INSTALLER.recoveryErrorCode(),"LEGACY_NO_BACKUP"));
    fixture(FIRMWARE_SLOT_B);signaturesValid=false;assert(!RELEASE_INSTALLER.activate(state.session));assert(metadataWrites==0 && programmed==0);
    fixture(FIRMWARE_SLOT_B);assert(persist(Receiving));received[0]=32;
    uint8_t bytes[16];memset(bytes,0xff,sizeof(bytes));ChunkData chunk={};chunk.data=bytes;chunk.chunk_size=sizeof(bytes);
    chunk.target_address=state.target.components[0].address;hash(bytes,sizeof(bytes),chunk.checksum);
    assert(RELEASE_INSTALLER.upload(state.session,"application",chunk));
    bytes[0]=0;hash(bytes,sizeof(bytes),chunk.checksum);assert(!RELEASE_INSTALLER.upload(state.session,"application",chunk));
    chunk.chunk_offset=48;chunk.target_address+=48;assert(!RELEASE_INSTALLER.upload(state.session,"application",chunk));
    puts("release journal interruption tests passed");
}
