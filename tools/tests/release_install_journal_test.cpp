#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include "../../application/Src/firmware/release_installer.cpp"

static std::vector<uint8_t> flash(0x800000,0xff);
static int budget=-1;
static bool readFailure=false;
static bool signaturesValid=false, txHealthy=true, programOk=true, metadataOk=true;
static unsigned programmed=0, verified=0, metadataWrites=0, resets=0;
static FirmwareSlot runningSlot=FIRMWARE_SLOT_A;
TestStorage testStorage = {{34}};
TestLink testLink;
bool TestLink::getReleaseIdentity(xora_release_identity_t&){return false;}
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
    verified++;if(!txHealthy)return false;*id=self;strcpy(id->version,"1.0.0");strcpy(id->build_id,"tx-build");return true;
}
bool Ch585IapClient::programCombinedImage(uint32_t at,uint32_t n){assert(at==CH585_FIRMWARE_STAGING_DATA_ADDR && n==8192);programmed++;return programOk;}
int8_t QSPI_W25Qxx_WriteBuffer_WithXIPOrNot(uint8_t* p,uint32_t at,uint32_t n){
    assert(at==METADATA_ADDR-EXTERNAL_FLASH_BASE && n==METADATA_STRUCT_SIZE && verified>0);
    metadataWrites++;if(!metadataOk)return -1;memcpy(flash.data()+at,p,n);return 0;
}
static bool step(){return budget<0 || budget-- > 0;}
int8_t QSPI_W25Qxx_ReadBuffer(uint8_t* p,uint32_t at,uint32_t n){
    if(readFailure || at+n>flash.size())return -1;memcpy(p,flash.data()+at,n);return 0;
}
int8_t QSPI_W25Qxx_ReadBuffer_WithXIPOrNot(uint8_t* p,uint32_t at,uint32_t n){return QSPI_W25Qxx_ReadBuffer(p,at,n);}
int8_t QSPI_W25Qxx_ExitMemoryMappedMode(){return 0;}
int8_t QSPI_W25Qxx_EnterMemoryMappedMode(){return 0;}
int8_t QSPI_W25Qxx_SectorErase(uint32_t at){if(!step())return -1;assert(at%4096==0);memset(flash.data()+at,0xff,4096);return 0;}
int8_t QSPI_W25Qxx_WritePage(uint8_t* p,uint32_t at,uint16_t n){
    assert(n && n<=256 && (at%256)+n<=256);
    if(!step())return -1;
    for(unsigned i=0;i<n;i++){assert((flash[at+i]&p[i])==p[i]);flash[at+i]&=p[i];}return 0;
}
static void fixture(FirmwareSlot target){
    flash.assign(flash.size(),0xff);memset(&state,0,sizeof(state));loaded=false;storageFault=false;declaring=false;bank=0;budget=-1;load();
    signaturesValid=txHealthy=programOk=metadataOk=true;programmed=verified=metadataWrites=resets=0;
    state.configVersion=34;strcpy(state.session,"test-release");state.target.target_slot=target;strcpy(state.target.firmware_version,self.version);
    auto& app=state.target.components[0];strcpy(app.name,"application");app.active=1;app.address=target==FIRMWARE_SLOT_A?0x90000000:0x902b0000;app.size=64;
    hash(flash.data()+app.address-EXTERNAL_FLASH_BASE,app.size,app.sha256);
    char metaHash[65],full[65],part[65];hash(&state.target,sizeof(state.target),metaHash);
    hash(flash.data()+CH585_FIRMWARE_STAGING_DATA_ADDR-EXTERNAL_FLASH_BASE,8192,full);
    hash(flash.data()+CH585_FIRMWARE_STAGING_DATA_ADDR-EXTERNAL_FLASH_BASE+4096,4096,part);
    state.manifestSize=snprintf(state.manifest,sizeof(state.manifest),
        "{\"schemaVersion\":2,\"product\":\"XORA\",\"deviceModel\":\"%s\",\"hardwareVersion\":\"%s\",\"version\":\"3.0.0\","
        "\"bootSecurityMode\":\"unlocked-development\",\"requiresManualLifecycleProvisioning\":false,"
        "\"install\":{\"protocol\":1,\"order\":\"tx-then-stm32\",\"configRead\":{\"min\":34,\"max\":34},\"configWrite\":34,"
        "\"stm32Maintenance\":{\"min\":1,\"max\":1},\"txMaintenance\":{\"min\":1,\"max\":1}},\"artifacts\":["
        "{\"component\":\"stm32\",\"slot\":\"%s\",\"metadataSha256\":\"%s\",\"version\":\"%s\",\"buildId\":\"%s\"},"
        "{\"component\":\"tx\",\"size\":8192,\"applicationOffset\":4096,\"applicationSize\":4096,\"imageFormat\":\"ch585-tx-combined\","
        "\"sha256\":\"%s\",\"applicationSha256\":\"%s\",\"version\":\"1.0.0\",\"buildId\":\"tx-build\"}]}",
        DEVICE_MODEL_STRING,HARDWARE_VERSION_STRING,target==FIRMWARE_SLOT_A?"A":"B",metaHash,self.version,self.build_id,full,part);
    assert(persist(Prepared));
}
int main(){
    const auto blank=flash;
    // The very first unactivated snapshot has no previous bank. A torn body
    // with the other bank erased must not strand the source firmware.
    for(int stop=0;stop<60;stop++){
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
    for(int stop=0;stop<60;stop++){
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
    }
    fixture(FIRMWARE_SLOT_B);programOk=false;assert(RELEASE_INSTALLER.activate(state.session));
    assert(RELEASE_INSTALLER.runBoot());assert(!RELEASE_INSTALLER.runBoot());assert(state.phase==Failed && programmed==2 && metadataWrites==0);
    programOk=true;assert(RELEASE_INSTALLER.retry());assert(RELEASE_INSTALLER.runBoot());
    fixture(FIRMWARE_SLOT_B);assert(RELEASE_INSTALLER.activate(state.session));metadataOk=false;
    assert(!RELEASE_INSTALLER.runBoot());assert(state.phase==Failed && metadataWrites==1);
    fixture(FIRMWARE_SLOT_B);assert(RELEASE_INSTALLER.activate(state.session));assert(RELEASE_INSTALLER.runBoot());
    runningSlot=FIRMWARE_SLOT_A;RELEASE_INSTALLER.verifyStartup(true);assert(state.phase==Failed);
    fixture(FIRMWARE_SLOT_B);assert(RELEASE_INSTALLER.activate(state.session));assert(RELEASE_INSTALLER.runBoot());
    runningSlot=FIRMWARE_SLOT_B;RELEASE_INSTALLER.verifyStartup(false);assert(state.phase==Failed);
    fixture(FIRMWARE_SLOT_B);signaturesValid=false;assert(!RELEASE_INSTALLER.activate(state.session));assert(metadataWrites==0 && programmed==0);
    fixture(FIRMWARE_SLOT_B);assert(persist(Receiving));received[0]=32;
    uint8_t bytes[16];memset(bytes,0xff,sizeof(bytes));ChunkData chunk={};chunk.data=bytes;chunk.chunk_size=sizeof(bytes);
    chunk.target_address=state.target.components[0].address;hash(bytes,sizeof(bytes),chunk.checksum);
    assert(RELEASE_INSTALLER.upload(state.session,"application",chunk));
    bytes[0]=0;hash(bytes,sizeof(bytes),chunk.checksum);assert(!RELEASE_INSTALLER.upload(state.session,"application",chunk));
    chunk.chunk_offset=48;chunk.target_address+=48;assert(!RELEASE_INSTALLER.upload(state.session,"application",chunk));
    puts("release journal interruption tests passed");
}
