#include <numeric>   // 为 std::accumulate
#include <algorithm> // 为 std::sort
#include <cmath>
#include "adc_btns/adc_manager.hpp"
#include "board_cfg.h"
#include "system_logger.h"
#include "qspi-w25q64.h"
#include <cstring>
#include "cpp_utils.hpp"
#include "micro_timer.hpp"
#include "CRC32.hpp"
#include "sha256_simple.h"
#include <cstddef>
#include <cctype>

namespace {
constexpr uint32_t LEGACY_ADC_MAPPING_VERSION = 0x000001u;
constexpr uint32_t SHARED_MAPPING_MAGIC = 0x4D414248u; // "HBAM"
constexpr uint16_t SHARED_MAPPING_SCHEMA_VERSION = 1u;
constexpr uint32_t SHARED_MAPPING_BANK_OFFSETS[2] = {0x1000u, 0x2000u};

struct SharedMappingRecord {
    uint32_t magic;
    uint16_t schemaVersion;
    uint16_t payloadLength;
    uint32_t sequence;
    ADCValuesMapping mapping;
    uint32_t crc32;
};

static_assert(sizeof(SharedMappingRecord) <= 4096u,
              "shared ADC mapping record must fit one QSPI sector");

static bool isTerminatedString(const char* value, size_t capacity)
{
    if (value == nullptr || capacity == 0u || value[0] == '\0') {
        return false;
    }

    return std::memchr(value, '\0', capacity) != nullptr;
}

static bool isMappingValid(const ADCValuesMapping& mapping)
{
    if (!isTerminatedString(mapping.id, sizeof(mapping.id)) ||
        !isTerminatedString(mapping.name, sizeof(mapping.name)) ||
        mapping.length < 2u || mapping.length > MAX_ADC_VALUES_LENGTH ||
        !std::isfinite(mapping.step) || mapping.step < 0.1f ||
        mapping.step > 10.0f || mapping.samplingFrequency == 0u) {
        return false;
    }
    for (size_t i = 0u; i < mapping.length; ++i) {
        if (mapping.originalValues[i] > UINT16_MAX) {
            return false;
        }
    }
    const uint32_t first = mapping.originalValues[0];
    const uint32_t last = mapping.originalValues[mapping.length - 1u];
    if (first != 0u && last != 0u && first == last) {
        return false;
    }
    return true;
}

static uint32_t sharedRecordCrc(const SharedMappingRecord& record)
{
    return CRC32::calculate(
        reinterpret_cast<const uint8_t*>(&record),
        static_cast<uint16_t>(offsetof(SharedMappingRecord, crc32)));
}

static bool isSharedRecordValid(const SharedMappingRecord& record)
{
    return record.magic == SHARED_MAPPING_MAGIC &&
           record.schemaVersion == SHARED_MAPPING_SCHEMA_VERSION &&
           record.payloadLength == sizeof(ADCValuesMapping) &&
           record.sequence != 0u &&
           record.crc32 == sharedRecordCrc(record) &&
           isMappingValid(record.mapping);
}

static bool sequenceNewer(uint32_t left, uint32_t right)
{
    return static_cast<int32_t>(left - right) > 0;
}

static void writeLe16(uint8_t* output, uint16_t value)
{
    output[0] = static_cast<uint8_t>(value);
    output[1] = static_cast<uint8_t>(value >> 8u);
}

static void writeLe32(uint8_t* output, uint32_t value)
{
    output[0] = static_cast<uint8_t>(value);
    output[1] = static_cast<uint8_t>(value >> 8u);
    output[2] = static_cast<uint8_t>(value >> 16u);
    output[3] = static_cast<uint8_t>(value >> 24u);
}

static bool mappingDigestMatches(const ADCValuesMapping& mapping,
                                 const char* expected)
{
    if (expected == nullptr || strlen(expected) != 64u) return false;
    uint8_t canonical[220] = {};
    static const uint8_t prefix[16] = {
        'H','B','O','X','-','A','D','C','-','M','A','P','-','V','1',0
    };
    memcpy(canonical, prefix, sizeof(prefix));
    memcpy(canonical + 16u, mapping.id,
           std::min(strlen(mapping.id), static_cast<size_t>(15u)));
    memcpy(canonical + 32u, mapping.name,
           std::min(strlen(mapping.name), static_cast<size_t>(15u)));
    writeLe32(canonical + 48u, static_cast<uint32_t>(mapping.length));
    uint32_t stepBits = 0u;
    static_assert(sizeof(stepBits) == sizeof(mapping.step), "float size mismatch");
    memcpy(&stepBits, &mapping.step, sizeof(stepBits));
    writeLe32(canonical + 52u, stepBits);
    writeLe16(canonical + 56u, mapping.samplingNoise);
    writeLe16(canonical + 58u, mapping.samplingFrequency);
    for (size_t i = 0u; i < mapping.length; ++i) {
        writeLe32(canonical + 60u + i * 4u, mapping.originalValues[i]);
    }
    char actual[65] = {};
    if (sha256_calculate(canonical, sizeof(canonical), actual) != 1) {
        return false;
    }
    for (size_t i = 0u; i < 64u; ++i) {
        const char a = static_cast<char>(std::tolower(
            static_cast<unsigned char>(actual[i])));
        const char b = static_cast<char>(std::tolower(
            static_cast<unsigned char>(expected[i])));
        if (a != b) return false;
    }
    return true;
}

static bool isLegacyMappingStoreCompatible(const ADCValuesMappingStore& store)
{
    if (store.version != LEGACY_ADC_MAPPING_VERSION ||
        store.num == 0u ||
        store.num > NUM_ADC_VALUES_MAPPING) {
        return false;
    }

    for (uint8_t i = 0u; i < store.num; ++i) {
        const ADCValuesMapping& mapping = store.mapping[i];
        if (!isTerminatedString(mapping.id, sizeof(mapping.id)) ||
            mapping.length < 2u ||
            mapping.length > MAX_ADC_VALUES_LENGTH ||
            !(mapping.step > 0.0f)) {
            return false;
        }
    }

    return true;
}

static void invalidateDmaBuffer(const ADCBufferInfo& info)
{
    uintptr_t start = reinterpret_cast<uintptr_t>(info.buffer) & ~static_cast<uintptr_t>(31u);
    uintptr_t end = (reinterpret_cast<uintptr_t>(info.buffer) + info.size + 31u) & ~static_cast<uintptr_t>(31u);
    SCB_InvalidateDCache_by_Addr(reinterpret_cast<uint32_t*>(start), static_cast<int32_t>(end - start));
}

static void setDmaCompletionInterrupt(ADC_HandleTypeDef& hadc, bool enabled)
{
    if (hadc.DMA_Handle == nullptr) {
        return;
    }

    if (enabled) {
        __HAL_DMA_ENABLE_IT(hadc.DMA_Handle, DMA_IT_TC | DMA_IT_HT);
    } else {
        __HAL_DMA_DISABLE_IT(hadc.DMA_Handle, DMA_IT_TC | DMA_IT_HT);
    }
}

static void disableAllDmaCompletionInterrupts()
{
    setDmaCompletionInterrupt(hadc1, false);
    setDmaCompletionInterrupt(hadc2, false);
    setDmaCompletionInterrupt(hadc3, false);
}

static ADC_HandleTypeDef* adcHandleForIndex(uint8_t adcIndex)
{
    if (adcIndex == 0u) return &hadc1;
    if (adcIndex == 1u) return &hadc2;
    if (adcIndex == 2u) return &hadc3;
    return nullptr;
}

static const uint32_t* completedDmaSequence(const ADCBufferInfo& info,
                                            ADC_HandleTypeDef* hadc)
{
    if (hadc == nullptr || hadc->DMA_Handle == nullptr || info.count == 0u) {
        return info.buffer;
    }

    const uint32_t remaining = __HAL_DMA_GET_COUNTER(hadc->DMA_Handle);
    /*
     * The buffer contains two complete channel sequences.  NDTR greater than
     * one sequence means DMA is filling the first half, so the second half is
     * stable; otherwise the first half is stable.  At NDTR==0 either previous
     * half is coherent, and selecting the first half remains safe.
     */
    return (remaining > info.count)
        ? (info.buffer + info.count)
        : info.buffer;
}
}

// 内存图
/*
 * QSPI Flash 内存布局 (从 ADC_VALUES_MAPPING_ADDR 开始):
 *
 * ADCValuesMappingStore 结构体:
 * +------------------------+ 0x00
 * | 版本号 (4 bytes)      |
 * +------------------------+ 0x04
 * | 映射数量 (1 byte)     |
 * +------------------------+ 0x05
 * | 默认映射ID (16 bytes) |
 * +------------------------+ 0x15
 * | 对齐填充 (3 bytes)     |
 * +------------------------+ 0x18
 * | 映射数据              |
 * | - ADCValuesMapping[0] |
 * | - ADCValuesMapping[1] |
 * | ...                   |
 * +------------------------+
 */

// 定义静态 ADC DMA 缓冲区
__attribute__((section(".DMA_Section"), aligned(32))) uint32_t ADCManager::ADC1_Values[NUM_ADC1_BUTTONS * 2u];
__attribute__((section(".DMA_Section"), aligned(32))) uint32_t ADCManager::ADC2_Values[NUM_ADC2_BUTTONS * 2u];
// ADC3 BDMA 只能访问 _RAM_D3_Area 区域
__attribute__((section(".BDMA_Section"), aligned(32))) uint32_t ADCManager::ADC3_Values[NUM_ADC3_BUTTONS * 2u];

uint32_t ADCManager::ADC_Values_Result[NUM_ADC_BUTTONS];

#define ADC_VALUES_MAPPING_ADDR_QSPI (ADC_VALUES_MAPPING_ADDR & 0x0FFFFFFF)
#define ADC_COMMON_CONFIG_ADDR_QSPI (ADC_COMMON_CONFIG_ADDR & 0x0FFFFFFF)

uint8_t ADC1_BUTTONS_MAPPING[NUM_ADC1_BUTTONS] = {0};
uint8_t ADC2_BUTTONS_MAPPING[NUM_ADC2_BUTTONS] = {0};
uint8_t ADC3_BUTTONS_MAPPING[NUM_ADC3_BUTTONS] = {0};

ADCManager::ADCManager()
{
    APP_DBG("ADCManager constructor");

    // Dynamically populate mapping arrays from ADC_PIN_MAP in board_cfg.h
    // This replaces the static macros ADCx_BUTTONS_MAPPING_DMA_TO_VIRTUALPIN
    for (uint8_t i = 0; i < NUM_ADC1_BUTTONS; i++)
    {
        ADC1_BUTTONS_MAPPING[i] = ADC1_PIN_MAP[i].virtualPin;
    }
    for (uint8_t i = 0; i < NUM_ADC2_BUTTONS; i++)
    {
        ADC2_BUTTONS_MAPPING[i] = ADC2_PIN_MAP[i].virtualPin;
    }
    for (uint8_t i = 0; i < NUM_ADC3_BUTTONS; i++)
    {
        ADC3_BUTTONS_MAPPING[i] = ADC3_PIN_MAP[i].virtualPin;
    }

    // 读取整个存储结构
    std::memset(&store, 0, sizeof(store));
    const int8_t mappingReadResult = QSPI_W25Qxx_ReadBuffer_WithXIPOrNot(
        reinterpret_cast<uint8_t*>(&store),
        ADC_VALUES_MAPPING_ADDR_QSPI,
        sizeof(ADCValuesMappingStore));

    APP_DBG("ADCValuesMappingUtils version: 0x%x", store.version);
    APP_DBG("ADC_MAPPING_VERSION == version: %d", ADC_MAPPING_VERSION == store.version);

    if (mappingReadResult == QSPI_W25Qxx_OK &&
        isLegacyMappingStoreCompatible(store)) {
        /*
         * PCB V2 的随包映射已经包含 18 路 ADC 校准槽位，但其文件头仍是
         * 历史版本 1。映射数组起始偏移没有变化，因此可安全地在 RAM 中
         * 升级版本并继续使用。不要在启动阶段反写 QSPI；后续正常保存配置
         * 时会按当前结构写入版本 2。
         */
        store.version = ADC_MAPPING_VERSION;
        std::memset(store.defaultId, 0, sizeof(store.defaultId));
        std::strncpy(store.defaultId,
                     store.mapping[0].id,
                     sizeof(store.defaultId) - 1u);
        APP_STAGE("I03", "accepted compatible ADC mapping v1: count=%u default=%s",
                  static_cast<unsigned>(store.num),
                  store.mapping[0].id);
    } else if (mappingReadResult != QSPI_W25Qxx_OK ||
               store.version != ADC_MAPPING_VERSION ||
               store.num > NUM_ADC_VALUES_MAPPING) {
        const uint32_t rejectedVersion = store.version;
        const uint8_t rejectedCount = store.num;

        /*
         * A bad/mismatched artifact must not destroy the mapping partition.
         * Keep the failure local to this boot so a correct full application
         * flash can restore operation without first repairing QSPI contents.
         */
        memset(&store, 0, sizeof(ADCValuesMappingStore));
        store.version = ADC_MAPPING_VERSION;
        APP_STAGE_ERROR("I03",
                        "ADC mapping rejected without modifying QSPI: read=%d version=%lu count=%u",
                        static_cast<int>(mappingReadResult),
                        static_cast<unsigned long>(rejectedVersion),
                        static_cast<unsigned>(rejectedCount));
    }

    APP_DBG("ADCManager init: store version - %d, num - %d, defaultId - %s", store.version, store.num, store.defaultId);

    QSPI_W25Qxx_ReadBuffer_WithXIPOrNot((uint8_t *)&common, ADC_COMMON_CONFIG_ADDR_QSPI, sizeof(ADCCommonConfig));
    if (common.version != ADC_COMMON_VERSION)
    {
        memset(&common, 0, sizeof(ADCCommonConfig));
        common.version = ADC_COMMON_VERSION;
        if (store.num > 0)
        {
            int8_t didx = -1;
            if (store.defaultId[0] != '\0')
            {
                didx = findMappingById(store.defaultId);
            }
            if (didx == -1)
            {
                strncpy(common.defaultMappingId, store.mapping[0].id, sizeof(common.defaultMappingId) - 1);
                common.defaultMappingId[sizeof(common.defaultMappingId) - 1] = '\0';
                didx = 0;
            }
            else
            {
                strncpy(common.defaultMappingId, store.defaultId, sizeof(common.defaultMappingId) - 1);
                common.defaultMappingId[sizeof(common.defaultMappingId) - 1] = '\0';
            }
            if (didx >= 0)
            {
                const ADCValuesMapping &m = store.mapping[didx];
                bool hasManual = false;
                bool hasAuto = false;
                for (uint8_t i = 0; i < NUM_ADC_BUTTONS; i++)
                {
                    if (m.manualCalibrationValues[i].topValue != 0 || m.manualCalibrationValues[i].bottomValue != 0)
                    {
                        hasManual = true;
                    }
                    if (m.autoCalibrationValues[i].topValue != 0 || m.autoCalibrationValues[i].bottomValue != 0)
                    {
                        hasAuto = true;
                    }
                }
                if (hasManual || hasAuto)
                {
                    for (uint8_t i = 0; i < NUM_ADC_BUTTONS; i++)
                    {
                        common.manualCalibrationValues[i].topValue = m.manualCalibrationValues[i].topValue;
                        common.manualCalibrationValues[i].bottomValue = m.manualCalibrationValues[i].bottomValue;
                        common.autoCalibrationValues[i].topValue = m.autoCalibrationValues[i].topValue;
                        common.autoCalibrationValues[i].bottomValue = m.autoCalibrationValues[i].bottomValue;
                    }
                    strncpy(common.calibratedMappingId, m.id, sizeof(common.calibratedMappingId) - 1);
                    common.calibratedMappingId[sizeof(common.calibratedMappingId) - 1] = '\0';
                }
            }
        }
        QSPI_W25Qxx_WriteBuffer_WithXIPOrNot((uint8_t *)&common, ADC_COMMON_CONFIG_ADDR_QSPI, sizeof(ADCCommonConfig));
    }
    if (store.num > 0)
    {
        if (findMappingById(common.defaultMappingId) == -1)
        {
            memset(common.manualCalibrationValues, 0, sizeof(common.manualCalibrationValues));
            memset(common.autoCalibrationValues, 0, sizeof(common.autoCalibrationValues));
            memset(common.calibratedMappingId, 0, sizeof(common.calibratedMappingId));
            strncpy(common.defaultMappingId, store.mapping[0].id, sizeof(common.defaultMappingId) - 1);
            common.defaultMappingId[sizeof(common.defaultMappingId) - 1] = '\0';
            QSPI_W25Qxx_WriteBuffer_WithXIPOrNot((uint8_t *)&common, ADC_COMMON_CONFIG_ADDR_QSPI, sizeof(ADCCommonConfig));
        }
    }

    // 注册采样统计完成消息
    MC.registerMessage(MessageId::ADC_SAMPLING_STATS_COMPLETE); // ADC 采样统计完成消息

    this->samplingCountMax = 1000;                                                                       // 采样次数 默认1000次
    this->samplingRateEnabled = false;                                                                   // 采样率统计是否开启 默认关闭
    this->ADCButtonStats = {0};                                                                          // 采样统计信息
    this->samplingADCInfo = ADCIndexInfo{0, 0};                                                          // 采样ADC信息
    this->adcBufferInfo[0] = {ADC1_Values, sizeof(ADC1_Values), ADC1_BUTTONS_MAPPING, NUM_ADC1_BUTTONS}; // ADC1缓存信息
    this->adcBufferInfo[1] = {ADC2_Values, sizeof(ADC2_Values), ADC2_BUTTONS_MAPPING, NUM_ADC2_BUTTONS}; // ADC2缓存信息
    this->adcBufferInfo[2] = {ADC3_Values, sizeof(ADC3_Values), ADC3_BUTTONS_MAPPING, NUM_ADC3_BUTTONS}; // ADC3缓存信息

    for (uint8_t i = 0; i < NUM_ADC1_BUTTONS; i++)
    {
        this->ADCBufferInfoList[i].valuePtr = &ADC_Values_Result[ADC1_BUTTONS_MAPPING[i]];
        this->ADCBufferInfoList[i].virtualPin = ADC1_BUTTONS_MAPPING[i];
    }

    // A shared, server-installed singleton wins over the slot component.
    // Without one, compact only the RAM view to the current factory default;
    // the accepted A/B firmware artifact remains untouched.
    loadSharedSingleton();

    for (uint8_t j = NUM_ADC1_BUTTONS; j < NUM_ADC1_BUTTONS + NUM_ADC2_BUTTONS; j++)
    {
        const uint8_t index = j - NUM_ADC1_BUTTONS;
        this->ADCBufferInfoList[j].valuePtr = &ADC_Values_Result[ADC2_BUTTONS_MAPPING[index]];
        this->ADCBufferInfoList[j].virtualPin = ADC2_BUTTONS_MAPPING[index];
    }

    for (uint8_t k = NUM_ADC1_BUTTONS + NUM_ADC2_BUTTONS; k < NUM_ADC1_BUTTONS + NUM_ADC2_BUTTONS + NUM_ADC3_BUTTONS; k++)
    {
        const uint8_t index = k - NUM_ADC1_BUTTONS - NUM_ADC2_BUTTONS;
        this->ADCBufferInfoList[k].valuePtr = &ADC_Values_Result[ADC3_BUTTONS_MAPPING[index]];
        this->ADCBufferInfoList[k].virtualPin = ADC3_BUTTONS_MAPPING[index];
    }

    // 使用 std::sort 按 virtualPin 排序
    std::sort(this->ADCBufferInfoList.begin(), this->ADCBufferInfoList.end(),
              [](const ADCButtonValueInfo &a, const ADCButtonValueInfo &b)
              {
                  return a.virtualPin < b.virtualPin;
              });

}

ADCManager::~ADCManager()
{
    this->forceStopAllSampling();

    MC.unregisterMessage(MessageId::ADC_SAMPLING_STATS_COMPLETE);
}

const std::array<ADCButtonValueInfo, NUM_ADC_BUTTONS>&
ADCManager::readADCValues() const
{
    for (uint8_t i = 0; i < NUM_ADC; i++) {
        const ADCBufferInfo& info = adcBufferInfo[i];
        invalidateDmaBuffer(info);
        const uint32_t* sequence = completedDmaSequence(
            info, adcHandleForIndex(i));

        for (uint8_t j = 0; j < info.count; j++) {
            const uint8_t virtualPin = info.indexMap[j];
            ADC_Values_Result[virtualPin] =
                sequence[j] >> ADC_VALUE_PUBLIC_RIGHT_SHIFT;
        }
    }

    return ADCBufferInfoList;
}

// 保存整个存储结构到Flash
int8_t ADCManager::saveStore()
{
    APP_DBG("ADCManager: saveStore - begin save store to flash.");
    if (sharedMappingInstalled && store.num == 1u) {
        return persistSharedSingleton(store.mapping[0])
            ? QSPI_W25Qxx_OK : W25Qxx_ERROR_TRANSMIT;
    }
    return QSPI_W25Qxx_WriteBuffer_WithXIPOrNot((uint8_t *)&store, ADC_VALUES_MAPPING_ADDR_QSPI, sizeof(ADCValuesMappingStore));
}

void ADCManager::selectFactoryFallback()
{
    if (store.num == 0u) return;
    int8_t selected = findMappingById(common.defaultMappingId);
    if (selected < 0 && store.defaultId[0] != '\0') {
        selected = findMappingById(store.defaultId);
    }
    if (selected < 0) selected = 0;
    ADCValuesMapping fallback = store.mapping[selected];
    memset(&store, 0, sizeof(store));
    store.version = ADC_MAPPING_VERSION;
    store.num = 1u;
    store.mapping[0] = fallback;
    strncpy(store.defaultId, fallback.id, sizeof(store.defaultId) - 1u);
    strncpy(common.defaultMappingId, fallback.id,
            sizeof(common.defaultMappingId) - 1u);
    if (strncmp(common.calibratedMappingId, fallback.id,
                sizeof(common.calibratedMappingId)) != 0) {
        memset(common.calibratedMappingId, 0, sizeof(common.calibratedMappingId));
        memset(common.manualCalibrationValues, 0,
               sizeof(common.manualCalibrationValues));
        memset(common.autoCalibrationValues, 0,
               sizeof(common.autoCalibrationValues));
    }
    sharedMappingInstalled = false;
    sharedMappingSequence = 0u;
    sharedMappingBank = -1;
}

void ADCManager::loadSharedSingleton()
{
    SharedMappingRecord records[2] = {};
    bool valid[2] = {false, false};
    const uint32_t base = (ADC_COMMON_CONFIG_ADDR & 0x0FFFFFFFu);
    for (uint8_t bank = 0u; bank < 2u; ++bank) {
        if (QSPI_W25Qxx_ReadBuffer_WithXIPOrNot(
                reinterpret_cast<uint8_t*>(&records[bank]),
                base + SHARED_MAPPING_BANK_OFFSETS[bank],
                sizeof(records[bank])) == QSPI_W25Qxx_OK) {
            valid[bank] = isSharedRecordValid(records[bank]);
        }
    }
    int8_t selected = -1;
    if (valid[0]) selected = 0;
    if (valid[1] && (selected < 0 ||
        sequenceNewer(records[1].sequence, records[0].sequence))) {
        selected = 1;
    }
    if (selected < 0) {
        selectFactoryFallback();
        return;
    }

    const ADCValuesMapping mapping = records[selected].mapping;
    memset(&store, 0, sizeof(store));
    store.version = ADC_MAPPING_VERSION;
    store.num = 1u;
    store.mapping[0] = mapping;
    strncpy(store.defaultId, mapping.id, sizeof(store.defaultId) - 1u);
    strncpy(common.defaultMappingId, mapping.id,
            sizeof(common.defaultMappingId) - 1u);
    if (strncmp(common.calibratedMappingId, mapping.id,
                sizeof(common.calibratedMappingId)) != 0) {
        memset(common.calibratedMappingId, 0, sizeof(common.calibratedMappingId));
        memset(common.manualCalibrationValues, 0,
               sizeof(common.manualCalibrationValues));
        memset(common.autoCalibrationValues, 0,
               sizeof(common.autoCalibrationValues));
    }
    sharedMappingInstalled = true;
    sharedMappingSequence = records[selected].sequence;
    sharedMappingBank = selected;
}

bool ADCManager::persistSharedSingleton(const ADCValuesMapping& source)
{
    ADCValuesMapping mapping = source;
    memset(mapping.autoCalibrationValues, 0,
           sizeof(mapping.autoCalibrationValues));
    memset(mapping.manualCalibrationValues, 0,
           sizeof(mapping.manualCalibrationValues));
    SharedMappingRecord record = {};
    record.magic = SHARED_MAPPING_MAGIC;
    record.schemaVersion = SHARED_MAPPING_SCHEMA_VERSION;
    record.payloadLength = sizeof(ADCValuesMapping);
    record.sequence = sharedMappingSequence + 1u;
    if (record.sequence == 0u) record.sequence = 1u;
    record.mapping = mapping;
    record.crc32 = sharedRecordCrc(record);

    const uint8_t target = sharedMappingBank == 0 ? 1u : 0u;
    const uint32_t address = (ADC_COMMON_CONFIG_ADDR & 0x0FFFFFFFu) +
        SHARED_MAPPING_BANK_OFFSETS[target];
    if (QSPI_W25Qxx_WriteBuffer_WithXIPOrNot(
            reinterpret_cast<uint8_t*>(&record), address,
            sizeof(record)) != QSPI_W25Qxx_OK) {
        return false;
    }
    SharedMappingRecord verify = {};
    if (QSPI_W25Qxx_ReadBuffer_WithXIPOrNot(
            reinterpret_cast<uint8_t*>(&verify), address,
            sizeof(verify)) != QSPI_W25Qxx_OK ||
        !isSharedRecordValid(verify) ||
        memcmp(&verify, &record, sizeof(record)) != 0) {
        return false;
    }
    sharedMappingBank = static_cast<int8_t>(target);
    sharedMappingSequence = record.sequence;
    sharedMappingInstalled = true;
    return true;
}

ADCBtnsError ADCManager::installSharedMapping(const ADCValuesMapping& source,
                                              const char* expectedSha256)
{
    if (!isMappingValid(source) ||
        !mappingDigestMatches(source, expectedSha256)) {
        return ADCBtnsError::INVALID_PARAMS;
    }
    ADCValuesMapping mapping = source;
    memset(mapping.autoCalibrationValues, 0,
           sizeof(mapping.autoCalibrationValues));
    memset(mapping.manualCalibrationValues, 0,
           sizeof(mapping.manualCalibrationValues));

    // The verified journal record is the commit point.  A power loss before
    // it completes leaves the previous bank selected; a loss afterwards
    // reloads the new mapping and rejects any calibration whose mapping ID no
    // longer matches, even if the common-config mirror was not updated yet.
    if (!persistSharedSingleton(mapping)) {
        return ADCBtnsError::MAPPING_UPDATE_FAILED;
    }

    memset(&store, 0, sizeof(store));
    store.version = ADC_MAPPING_VERSION;
    store.num = 1u;
    store.mapping[0] = mapping;
    strncpy(store.defaultId, mapping.id, sizeof(store.defaultId) - 1u);

    memset(common.manualCalibrationValues, 0,
           sizeof(common.manualCalibrationValues));
    memset(common.autoCalibrationValues, 0,
           sizeof(common.autoCalibrationValues));
    memset(common.calibratedMappingId, 0,
           sizeof(common.calibratedMappingId));
    memset(common.defaultMappingId, 0, sizeof(common.defaultMappingId));
    strncpy(common.defaultMappingId, mapping.id,
            sizeof(common.defaultMappingId) - 1u);
    if (saveCommon() != QSPI_W25Qxx_OK) {
        // The shared record is already durable.  Keep the new singleton live;
        // the ID mismatch on the next boot continues to suppress stale
        // calibration rather than reporting a failed install that actually
        // committed.
        APP_ERR("ADCManager: shared mapping committed; common calibration mirror write failed");
    }
    return ADCBtnsError::SUCCESS;
}

ADCBtnsError ADCManager::clearSharedMapping(const char* expectedMappingId)
{
    if (!sharedMappingInstalled || expectedMappingId == nullptr ||
        expectedMappingId[0] == '\0' || store.num != 1u ||
        strncmp(store.mapping[0].id, expectedMappingId,
                sizeof(store.mapping[0].id)) != 0) {
        return ADCBtnsError::MAPPING_NOT_FOUND;
    }

    // Read and validate the slot component before touching either shared
    // bank. Deleting a user mapping must never leave the runtime without a
    // usable factory fallback.
    ADCValuesMappingStore factoryStore = {};
    const int8_t factoryRead = QSPI_W25Qxx_ReadBuffer_WithXIPOrNot(
        reinterpret_cast<uint8_t*>(&factoryStore),
        ADC_VALUES_MAPPING_ADDR_QSPI,
        sizeof(factoryStore));
    if (factoryRead != QSPI_W25Qxx_OK || factoryStore.num == 0u ||
        factoryStore.num > NUM_ADC_VALUES_MAPPING ||
        (factoryStore.version != ADC_MAPPING_VERSION &&
         !isLegacyMappingStoreCompatible(factoryStore))) {
        return ADCBtnsError::MAPPING_STORAGE_EMPTY;
    }
    if (factoryStore.version == LEGACY_ADC_MAPPING_VERSION) {
        factoryStore.version = ADC_MAPPING_VERSION;
        memset(factoryStore.defaultId, 0, sizeof(factoryStore.defaultId));
        strncpy(factoryStore.defaultId, factoryStore.mapping[0].id,
                sizeof(factoryStore.defaultId) - 1u);
    }
    for (uint8_t index = 0u; index < factoryStore.num; ++index) {
        if (!isMappingValid(factoryStore.mapping[index])) {
            return ADCBtnsError::MAPPING_STORAGE_EMPTY;
        }
    }

    // Clear the inactive/older bank first and the selected bank last. A power
    // loss between the two operations therefore continues to boot the current
    // valid shared mapping; once both are invalid, boot falls back to the slot.
    const uint8_t first = sharedMappingBank == 0 ? 1u : 0u;
    const uint8_t second = first == 0u ? 1u : 0u;
    const uint8_t clearOrder[2] = {first, second};
    SharedMappingRecord cleared = {};
    const uint32_t base = (ADC_COMMON_CONFIG_ADDR & 0x0FFFFFFFu);
    for (const uint8_t bank : clearOrder) {
        if (QSPI_W25Qxx_WriteBuffer_WithXIPOrNot(
                reinterpret_cast<uint8_t*>(&cleared),
                base + SHARED_MAPPING_BANK_OFFSETS[bank],
                sizeof(cleared)) != QSPI_W25Qxx_OK) {
            break;
        }
    }

    bool anyValid = false;
    bool verificationComplete = true;
    for (uint8_t bank = 0u; bank < 2u; ++bank) {
        SharedMappingRecord verify = {};
        const int8_t readResult = QSPI_W25Qxx_ReadBuffer_WithXIPOrNot(
                reinterpret_cast<uint8_t*>(&verify),
                base + SHARED_MAPPING_BANK_OFFSETS[bank],
                sizeof(verify));
        if (readResult != QSPI_W25Qxx_OK) {
            verificationComplete = false;
        } else if (isSharedRecordValid(verify)) {
            anyValid = true;
        }
    }
    if (!verificationComplete || anyValid) {
        return ADCBtnsError::MAPPING_DELETE_FAILED;
    }

    store = factoryStore;
    selectFactoryFallback();
    memset(common.manualCalibrationValues, 0,
           sizeof(common.manualCalibrationValues));
    memset(common.autoCalibrationValues, 0,
           sizeof(common.autoCalibrationValues));
    memset(common.calibratedMappingId, 0,
           sizeof(common.calibratedMappingId));
    if (saveCommon() != QSPI_W25Qxx_OK) {
        APP_ERR("ADCManager: shared mapping cleared; common fallback mirror write failed");
    }
    return ADCBtnsError::SUCCESS;
}

int8_t ADCManager::saveCommon()
{
    APP_DBG("ADCManager: saveCommon - begin save common to flash.");
    return QSPI_W25Qxx_WriteBuffer_WithXIPOrNot((uint8_t *)&common, ADC_COMMON_CONFIG_ADDR_QSPI, sizeof(ADCCommonConfig));
}

/**
 * @brief 查找映射ID的索引
 * @param id 映射ID
 * @return 映射ID的索引
 */
int8_t ADCManager::findMappingById(const char *const id) const
{
    if (!id)
        return -1;

    // 遍历映射数据，检查名称
    for (uint8_t i = 0; i < store.num; i++)
    {
        if (strcmp(store.mapping[i].id, id) == 0)
        {
            return i;
        }
    }

    return -1;
}

/**
 * @brief 删除映射
 * @param id 映射ID
 * @return 是否删除成功
 */
ADCBtnsError ADCManager::removeADCMapping(const char *id)
{
    if (!id)
        return ADCBtnsError::INVALID_PARAMS;

    // 查找要删除的映射索引
    int8_t targetIdx = findMappingById(id);
    if (targetIdx == -1)
        return ADCBtnsError::MAPPING_NOT_FOUND;

    // 如果只有一个映射，则不能删除
    if (store.num <= 1)
        return ADCBtnsError::MAPPING_DELETE_FAILED;

    // A dangling default id makes calibration and the INPUT worker unusable.
    // Require callers to select another default before deleting this mapping.
    if (getDefaultMapping() == id)
        return ADCBtnsError::MAPPING_DELETE_FAILED;

    ADCValuesMapping removedMapping = store.mapping[targetIdx];

    // 移动数据
    if (targetIdx < store.num - 1)
    {
        memmove(&store.mapping[targetIdx],
                &store.mapping[targetIdx + 1],
                (store.num - targetIdx - 1) * sizeof(ADCValuesMapping));
    }

    store.num--;

    // 保存更新后的存储结构
    if (saveStore() != QSPI_W25Qxx_OK)
    {
        if (targetIdx < store.num)
        {
            memmove(&store.mapping[targetIdx + 1],
                    &store.mapping[targetIdx],
                    (store.num - targetIdx) * sizeof(ADCValuesMapping));
        }
        store.mapping[targetIdx] = removedMapping;
        store.num++;
        return ADCBtnsError::MAPPING_DELETE_FAILED;
    }

    return ADCBtnsError::SUCCESS;
}

/**
 * @brief 创建映射
 * @param id射ID
 * @param length 映射长度
 * @param step 步长
 * @return 是否创建成功
 */
ADCBtnsError ADCManager::createADCMapping(const char *name, size_t length, float_t step,
                                          std::string *createdId)
{
    if (!name || name[0] == '\0' ||
        strlen(name) >= sizeof(ADCValuesMapping::name) ||
        length < 2u || length > MAX_ADC_VALUES_LENGTH ||
        !std::isfinite(step) || step < 0.1f || step > 10.0f)
        return ADCBtnsError::INVALID_PARAMS;

    // 检查映射名称是否已存在
    for (uint8_t i = 0; i < store.num; i++)
    {
        if (strcmp(store.mapping[i].name, name) == 0)
        {
            return ADCBtnsError::MAPPING_ALREADY_EXISTS;
        }
    }

    // 检查映射数量是否已满
    if (store.num >= 1u)
        return ADCBtnsError::MAPPING_STORAGE_FULL;

    // 创建新映射
    ADCValuesMapping &newMapping = store.mapping[store.num];
    memset(&newMapping, 0, sizeof(ADCValuesMapping));

    // 使用ID生成器生成唯一ID
    std::string unique_id = generate_unique_id(name);
    strncpy(newMapping.id, unique_id.c_str(), sizeof(newMapping.id) - 1);
    newMapping.id[sizeof(newMapping.id) - 1] = '\0';
    strncpy(newMapping.name, name, sizeof(newMapping.name) - 1);
    newMapping.name[sizeof(newMapping.name) - 1] = '\0';
    newMapping.length = length;
    newMapping.step = step;
    newMapping.samplingNoise = 0;
    newMapping.samplingFrequency = 0;
    memset(newMapping.originalValues, 0, sizeof(newMapping.originalValues));

    store.num++;

    // 如果这是第一个映射，则设置为默认映射
    if (store.num == 1)
    {
        strncpy(store.defaultId, newMapping.id, sizeof(store.defaultId) - 1);
        store.defaultId[sizeof(store.defaultId) - 1] = '\0';
    }

    // 保存更新后的存储结构
    if (saveStore() != QSPI_W25Qxx_OK)
    {
        store.num--;
        memset(&newMapping, 0, sizeof(ADCValuesMapping));
        return ADCBtnsError::MAPPING_CREATE_FAILED;
    }

    if (createdId != nullptr)
    {
        *createdId = newMapping.id;
    }

    return ADCBtnsError::SUCCESS;
}

ADCBtnsError ADCManager::renameADCMapping(const char *id, const char *name)
{
    if (!id || !name || name[0] == '\0' ||
        strlen(name) >= sizeof(ADCValuesMapping::name))
        return ADCBtnsError::INVALID_PARAMS;

    int idx = findMappingById(id);
    if (idx == -1)
        return ADCBtnsError::MAPPING_NOT_FOUND;

    for (uint8_t i = 0; i < store.num; ++i)
    {
        if (i != static_cast<uint8_t>(idx) && strcmp(store.mapping[i].name, name) == 0)
            return ADCBtnsError::MAPPING_ALREADY_EXISTS;
    }

    char previousName[sizeof(store.mapping[idx].name)] = {};
    memcpy(previousName, store.mapping[idx].name, sizeof(previousName));

    strncpy(store.mapping[idx].name, name, sizeof(store.mapping[idx].name) - 1);
    store.mapping[idx].name[sizeof(store.mapping[idx].name) - 1] = '\0';

    // 保存更新后的存储结构

    if (saveStore() != QSPI_W25Qxx_OK)
    {
        memcpy(store.mapping[idx].name, previousName, sizeof(previousName));
        return ADCBtnsError::MAPPING_UPDATE_FAILED;
    }

    return ADCBtnsError::SUCCESS;
}

ADCBtnsError ADCManager::updateADCMapping(const char *id, const ADCValuesMapping &map)
{
    if (!id)
        return ADCBtnsError::INVALID_PARAMS;
    if (map.length == 0 || map.length > MAX_ADC_VALUES_LENGTH)
        return ADCBtnsError::INVALID_PARAMS;

    int idx = findMappingById(id);
    if (idx == -1)
        return ADCBtnsError::MAPPING_NOT_FOUND;

    // printf("ADCValuesMappingUtils: update - begin update mapping.\n");
    // printf("ADCValuesMappingUtils: update - mapping id: %s, name: %s, length: %d, step: %f, samplingNoise: %d, samplingFrequency: %d\n",
    //        map.id, map.name, map.length, map.step, map.samplingNoise, map.samplingFrequency);

    const ADCValuesMapping previousMapping = store.mapping[idx];

    // 更新映射数据
    memcpy(&store.mapping[idx], &map, sizeof(ADCValuesMapping));

    // 保存更新后的存储结构
    if (saveStore() != QSPI_W25Qxx_OK)
    {
        store.mapping[idx] = previousMapping;
        return ADCBtnsError::MAPPING_UPDATE_FAILED;
    }

    return ADCBtnsError::SUCCESS;
}

/**
 * @brief 设置默认映射
 * @param id 映射ID
 * @return 错误码
 */
ADCBtnsError ADCManager::setDefaultMapping(const char *id)
{
    if (!id)
        return ADCBtnsError::INVALID_PARAMS;

    int idx = findMappingById(id);
    if (idx == -1)
        return ADCBtnsError::MAPPING_NOT_FOUND;

    const ADCValuesMapping &mapping = store.mapping[idx];
    if (mapping.length < 2u || mapping.length > MAX_ADC_VALUES_LENGTH ||
        mapping.originalValues[0] == 0u ||
        mapping.originalValues[mapping.length - 1u] == 0u ||
        mapping.originalValues[0] == mapping.originalValues[mapping.length - 1u])
    {
        return ADCBtnsError::MAPPING_INVALID_RANGE;
    }

    ADCCommonConfig previousCommon = common;

    strncpy(common.defaultMappingId, id, sizeof(common.defaultMappingId) - 1);
    common.defaultMappingId[sizeof(common.defaultMappingId) - 1] = '\0';

    // 保存更新后的存储结构
    if (saveCommon() != QSPI_W25Qxx_OK)
    {
        common = previousCommon;
        return ADCBtnsError::MAPPING_UPDATE_FAILED;
    }

    return ADCBtnsError::SUCCESS;
}

/**
 * @brief 获取映射列表
 * @return 映射列表
 */
std::vector<ADCValuesMapping *> ADCManager::getMappingList()
{
    std::vector<ADCValuesMapping *> mappingList;
    for (uint8_t i = 0; i < store.num; i++)
    {
        mappingList.push_back(&store.mapping[i]);
    }
    return mappingList;
}

/**
 * @brief 获取默认映射名称
 * @return 默认映射名称
 */
std::string ADCManager::getDefaultMapping() const
{
    // 如果映射数量为0，则返回空字符串
    if (store.num == 0)
        return std::string("");
    // 如果默认映射名称未设置，则返回第一个映射名称
    if (common.defaultMappingId[0] == '\0')
    {
        APP_DBG("ADCManager: getDefaultMapping defaultId is empty, return first mapping id.");
        return std::string(store.mapping[0].id);
    };
    return std::string(common.defaultMappingId);
}

/**
 * @brief 获取映射JSON
 * @param name 映射名称
 * @return 映射JSON
 */
const ADCValuesMapping *ADCManager::getMapping(const char *const id) const
{
    if (!id)
        return nullptr;

    // 查找映射
    int8_t idx = findMappingById(id);
    if (idx == -1)
        return nullptr;

    return &store.mapping[idx];
}

/**
 * @brief 获取校准值
 * @param mappingId 映射ID
 * @param buttonIndex 按钮索引
 * @param isAutoCalibration 是否为自动校准
 * @param topValue 返回的顶部值(完全按下)
 * @param bottomValue 返回的底部值(完全释放)
 * @return 错误码
 */
ADCBtnsError ADCManager::getCalibrationValues(const char *mappingId, uint8_t buttonIndex, bool isAutoCalibration, uint16_t &topValue, uint16_t &bottomValue) const
{
    if (!mappingId || buttonIndex >= NUM_ADC_BUTTONS)
    {
        return ADCBtnsError::INVALID_PARAMS;
    }

    int8_t idx = findMappingById(mappingId);
    if (idx == -1)
    {
        return ADCBtnsError::MAPPING_NOT_FOUND;
    }

    if (strncmp(common.calibratedMappingId, mappingId, sizeof(common.calibratedMappingId)) != 0)
    {
        return ADCBtnsError::CALIBRATION_VALUES_NOT_FOUND;
    }

    if (isAutoCalibration)
    {
        topValue = common.autoCalibrationValues[buttonIndex].topValue;
        bottomValue = common.autoCalibrationValues[buttonIndex].bottomValue;
    }
    else
    {
        topValue = common.manualCalibrationValues[buttonIndex].topValue;
        bottomValue = common.manualCalibrationValues[buttonIndex].bottomValue;
    }

    APP_DBG("getCalibrationValues: buttonIndex: %d, topValue: %d, bottomValue: %d", buttonIndex, topValue, bottomValue);

    // 检查校准值是否有效
    if (topValue == bottomValue || topValue == 0 || bottomValue == 0 || topValue == 0xFFFF || bottomValue == 0xFFFF)
    {
        return ADCBtnsError::CALIBRATION_VALUES_NOT_FOUND;
    }

    return ADCBtnsError::SUCCESS;
}

/**
 * @brief 设置校准值
 * @param mappingId 映射ID
 * @param buttonIndex 按钮索引
 * @param isAutoCalibration 是否为自动校准
 * @param topValue 顶部值(完全按下)
 * @param bottomValue 底部值(完全释放)
 * @param withSave 是否保存到存储
 * @return 错误码
 */
ADCBtnsError ADCManager::setCalibrationValues(const char *mappingId, uint8_t buttonIndex, bool isAutoCalibration, uint16_t topValue, uint16_t bottomValue, bool withSave)
{
    if (!mappingId || buttonIndex >= NUM_ADC_BUTTONS)
    {
        return ADCBtnsError::INVALID_PARAMS;
    }

    int8_t idx = findMappingById(mappingId);
    if (idx == -1)
    {
        return ADCBtnsError::MAPPING_NOT_FOUND;
    }

    ADCCommonConfig previousCommon = common;

    strncpy(common.calibratedMappingId, mappingId, sizeof(common.calibratedMappingId) - 1);
    common.calibratedMappingId[sizeof(common.calibratedMappingId) - 1] = '\0';

    if (isAutoCalibration)
    {
        common.autoCalibrationValues[buttonIndex].topValue = topValue;
        common.autoCalibrationValues[buttonIndex].bottomValue = bottomValue;
    }
    else
    {
        common.manualCalibrationValues[buttonIndex].topValue = topValue;
        common.manualCalibrationValues[buttonIndex].bottomValue = bottomValue;
    }

    // 保存到存储
    if (withSave != false && saveCommon() != QSPI_W25Qxx_OK)
    {
        common = previousCommon;
        return ADCBtnsError::MAPPING_UPDATE_FAILED;
    }

    return ADCBtnsError::SUCCESS;
}

ADCBtnsError ADCManager::markMapping(const char *const id,
                                     const uint32_t *const values,
                                     const uint16_t samplingNoise,
                                     const uint16_t samplingFrequency)
{
    if (!id || !values || samplingFrequency == 0)
        return ADCBtnsError::INVALID_PARAMS;

    int idx = findMappingById(id);
    if (idx == -1)
        return ADCBtnsError::MAPPING_NOT_FOUND;

    ADCValuesMapping &mapping = store.mapping[idx];
    if (mapping.length < 2u || mapping.length > MAX_ADC_VALUES_LENGTH ||
        values[0] == 0u || values[mapping.length - 1u] == 0u ||
        values[0] == values[mapping.length - 1u])
    {
        return ADCBtnsError::MAPPING_INVALID_RANGE;
    }
    for (size_t i = 0; i < mapping.length; ++i)
    {
        if (values[i] == 0u || values[i] > UINT16_MAX)
        {
            return ADCBtnsError::MAPPING_INVALID_RANGE;
        }
    }

    const ADCValuesMapping previousMapping = mapping;

    // 更新数据
    mapping.samplingNoise = samplingNoise;
    mapping.samplingFrequency = samplingFrequency;
    memset(mapping.originalValues, 0, sizeof(mapping.originalValues));
    memcpy(mapping.originalValues, values, mapping.length * sizeof(uint32_t));

    if (saveStore() != QSPI_W25Qxx_OK)
    {
        mapping = previousMapping;
        APP_ERR("ADCValuesMappingUtils: mark - save mapping failed");
        return ADCBtnsError::MAPPING_UPDATE_FAILED;
    }

    return ADCBtnsError::SUCCESS;
}

/**
 * @brief 开始ADC采样
 * @param enableSamplingRate 是否启用采样率统计
 * @param buttonIndex 采样按钮索引 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16
 * @param updateInterval 采样间隔
 * @return 错误码
 */
ADCBtnsError ADCManager::startADCSamping(bool enableSamplingRate,
                                         uint8_t virtualPin,
                                         uint32_t samplingCountMax)
{
    ADCIndexInfo requestedAdcInfo{-1, -1};
    if (enableSamplingRate)
    {
        requestedAdcInfo = findADCButtonVirtualPin(virtualPin);
        if (requestedAdcInfo.ADCIndex < 0) {
            APP_ERR("Invalid button index\n");
            return ADCBtnsError::INVALID_PARAMS;
        }
    }

    if (!dmaSamplingActive)
    {
        forceStopAllSampling();

        memset(ADC1_Values, 0, sizeof(ADC1_Values));
        memset(ADC2_Values, 0, sizeof(ADC2_Values));
        memset(ADC3_Values, 0, sizeof(ADC3_Values));
        memset(ADC_Values_Result, 0, sizeof(ADC_Values_Result));

        if (HAL_ADCEx_Calibration_Start(&hadc1, ADC_CALIB_OFFSET, ADC_SINGLE_ENDED) != HAL_OK) {
            APP_ERR("ADC1 calibration failed\n");
            return ADCBtnsError::ADC1_CALIB_FAILED;
        }
        if (HAL_ADCEx_Calibration_Start(&hadc2, ADC_CALIB_OFFSET, ADC_SINGLE_ENDED) != HAL_OK) {
            APP_ERR("ADC2 calibration failed\n");
            return ADCBtnsError::ADC2_CALIB_FAILED;
        }
        if (HAL_ADCEx_Calibration_Start(&hadc3, ADC_CALIB_OFFSET, ADC_SINGLE_ENDED) != HAL_OK) {
            APP_ERR("ADC3 calibration failed\n");
            return ADCBtnsError::ADC3_CALIB_FAILED;
        }

        if (HAL_ADC_Start_DMA(&hadc1, ADC1_Values, NUM_ADC1_BUTTONS * 2u) != HAL_OK) {
            APP_ERR("ADC1 circular DMA start failed\n");
            forceStopAllSampling();
            return ADCBtnsError::DMA1_START_FAILED;
        }
        setDmaCompletionInterrupt(hadc1, false);

        if (HAL_ADC_Start_DMA(&hadc2, ADC2_Values, NUM_ADC2_BUTTONS * 2u) != HAL_OK) {
            APP_ERR("ADC2 circular DMA start failed\n");
            forceStopAllSampling();
            return ADCBtnsError::DMA2_START_FAILED;
        }
        setDmaCompletionInterrupt(hadc2, false);

        if (HAL_ADC_Start_DMA(&hadc3, ADC3_Values, NUM_ADC3_BUTTONS * 2u) != HAL_OK) {
            APP_ERR("ADC3 circular DMA start failed\n");
            forceStopAllSampling();
            return ADCBtnsError::DMA3_START_FAILED;
        }
        setDmaCompletionInterrupt(hadc3, false);

        dmaSamplingActive = true;
        APP_DBG("All ADC circular DMA channels armed\n");
    }

    if (enableSamplingRate)
    {
        stopADCSamping();
        if (samplingCountMax > 0) {
            this->samplingCountMax = samplingCountMax;
        }
        this->samplingADCInfo = requestedAdcInfo;

        ADCButtonStats.adcIndex = this->samplingADCInfo.ADCIndex;
        ADCButtonStats.startTime = HAL_GetTick();
        ADCButtonStats.endTime = 0;
        ADCButtonStats.count = 0;
        ADCButtonStats.averageValue = 0;
        ADCButtonStats.samplingFreq = 0;
        ADCButtonStats.values.clear();
        ADCButtonStats.values.resize(this->samplingCountMax);
        ADCButtonStats.diffValues.clear();
        ADCButtonStats.diffValues.resize(this->samplingCountMax);

        samplingRateEnabled = true;
        if (this->samplingADCInfo.ADCIndex == 0) {
            setDmaCompletionInterrupt(hadc1, true);
        } else if (this->samplingADCInfo.ADCIndex == 1) {
            setDmaCompletionInterrupt(hadc2, true);
        } else {
            setDmaCompletionInterrupt(hadc3, true);
        }
    }

    return ADCBtnsError::SUCCESS;
}

ADCBtnsError ADCManager::clearCalibrationValues(const char *mappingId, bool isAutoCalibration)
{
    if (!mappingId || findMappingById(mappingId) == -1)
    {
        return mappingId ? ADCBtnsError::MAPPING_NOT_FOUND : ADCBtnsError::INVALID_PARAMS;
    }

    ADCCommonConfig previousCommon = common;
    strncpy(common.calibratedMappingId, mappingId, sizeof(common.calibratedMappingId) - 1);
    common.calibratedMappingId[sizeof(common.calibratedMappingId) - 1] = '\0';
    if (isAutoCalibration)
    {
        memset(common.autoCalibrationValues, 0, sizeof(common.autoCalibrationValues));
    }
    else
    {
        memset(common.manualCalibrationValues, 0, sizeof(common.manualCalibrationValues));
    }

    if (saveCommon() != QSPI_W25Qxx_OK)
    {
        common = previousCommon;
        return ADCBtnsError::MAPPING_UPDATE_FAILED;
    }
    return ADCBtnsError::SUCCESS;
}

void ADCManager::copyBackup(ADCValuesMappingStore& storeOut,
                            ADCCommonConfig& commonOut) const
{
    memcpy(&storeOut, &store, sizeof(storeOut));
    memcpy(&commonOut, &common, sizeof(commonOut));
}

ADCBtnsError ADCManager::restoreBackup(const ADCValuesMappingStore& storeIn,
                                       const ADCCommonConfig& commonIn)
{
    if (storeIn.version != ADC_MAPPING_VERSION ||
        commonIn.version != ADC_COMMON_VERSION ||
        storeIn.num == 0u ||
        storeIn.num > NUM_ADC_VALUES_MAPPING ||
        strncmp(storeIn.defaultId, commonIn.defaultMappingId,
                sizeof(storeIn.defaultId)) != 0)
    {
        return ADCBtnsError::INVALID_PARAMS;
    }

    bool defaultFound = false;
    bool calibratedFound = commonIn.calibratedMappingId[0] == '\0';
    for (uint8_t i = 0; i < storeIn.num; ++i)
    {
        const ADCValuesMapping& mapping = storeIn.mapping[i];
        if (mapping.id[0] == '\0' || mapping.name[0] == '\0' ||
            mapping.length == 0u || mapping.length > MAX_ADC_VALUES_LENGTH)
        {
            return ADCBtnsError::INVALID_PARAMS;
        }
        if (strncmp(mapping.id, commonIn.defaultMappingId,
                    sizeof(commonIn.defaultMappingId)) == 0)
        {
            defaultFound = true;
        }
        if (strncmp(mapping.id, commonIn.calibratedMappingId,
                    sizeof(commonIn.calibratedMappingId)) == 0)
        {
            calibratedFound = true;
        }
        for (uint8_t j = i + 1u; j < storeIn.num; ++j)
        {
            if (strncmp(mapping.id, storeIn.mapping[j].id,
                        sizeof(mapping.id)) == 0)
            {
                return ADCBtnsError::MAPPING_ALREADY_EXISTS;
            }
        }
    }
    if (!defaultFound || !calibratedFound)
    {
        return ADCBtnsError::INVALID_PARAMS;
    }

    ADCValuesMappingStore oldStore;
    ADCCommonConfig oldCommon;
    copyBackup(oldStore, oldCommon);
    memcpy(&store, &storeIn, sizeof(store));
    memcpy(&common, &commonIn, sizeof(common));

    if (saveStore() != QSPI_W25Qxx_OK)
    {
        memcpy(&store, &oldStore, sizeof(store));
        memcpy(&common, &oldCommon, sizeof(common));
        return ADCBtnsError::MAPPING_UPDATE_FAILED;
    }
    if (saveCommon() != QSPI_W25Qxx_OK)
    {
        memcpy(&store, &oldStore, sizeof(store));
        memcpy(&common, &oldCommon, sizeof(common));
        (void)saveStore();
        (void)saveCommon();
        return ADCBtnsError::MAPPING_UPDATE_FAILED;
    }

    return ADCBtnsError::SUCCESS;
}

void ADCManager::stopADCSamping()
{
    samplingRateEnabled = false;
    disableAllDmaCompletionInterrupts();
}

void ADCManager::forceStopAllSampling()
{
    samplingRateEnabled = false;
    dmaSamplingActive = false;
    disableAllDmaCompletionInterrupts();

    if (hadc1.Instance != nullptr) {
        (void)HAL_ADC_Stop_DMA(&hadc1);
    }
    if (hadc2.Instance != nullptr) {
        (void)HAL_ADC_Stop_DMA(&hadc2);
    }
    if (hadc3.Instance != nullptr) {
        (void)HAL_ADC_Stop_DMA(&hadc3);
    }
}

/**
 * @brief 处理ADC转换完成中断
 * 在每次采样完成后，会调用此函数，用于更新采样统计信息
 * 更新ADCButtonValues的值
 * 如果采样率统计开启，则更新采样统计信息，包括每个通道的平均值、最小值、最大值
 * @param hadc ADC句柄
 */
void ADCManager::handleADCStats(ADC_HandleTypeDef *hadc)
{
    const int8_t adcIndex = (hadc->Instance == ADC1) ? 0
                            : (hadc->Instance == ADC2) ? 1
                            : (hadc->Instance == ADC3) ? 2
                                                      : -1;

    // 如果采样ADC索引不匹配，则返回，此处只处理采样ADC索引对应的ADC
    if (!samplingRateEnabled || adcIndex < 0 ||
        adcIndex != this->samplingADCInfo.ADCIndex)
        return;

    // 处理数据...
    const auto &info = adcBufferInfo[static_cast<uint8_t>(adcIndex)];
    invalidateDmaBuffer(info);

    const uint32_t* sequence = completedDmaSequence(
        info, adcHandleForIndex(static_cast<uint8_t>(adcIndex)));
    uint32_t value = sequence[this->samplingADCInfo.indexInDMA] >> ADC_VALUE_PUBLIC_RIGHT_SHIFT;

    if (value == 0)
        return;
    if (ADCButtonStats.count >= samplingCountMax ||
        ADCButtonStats.count >= ADCButtonStats.values.size()) {
        samplingRateEnabled = false;
        disableAllDmaCompletionInterrupts();
        return;
    }

    ADCButtonStats.values[ADCButtonStats.count] = value; // 保存当前值
    ADCButtonStats.count++;                              // 计数器加1

    if (ADCButtonStats.count >= samplingCountMax)
    {
        samplingRateEnabled = false;
        disableAllDmaCompletionInterrupts();
        uint32_t t = HAL_GetTick();
        const uint32_t elapsedMs = std::max<uint32_t>(1u, t - ADCButtonStats.startTime);
        ADCButtonStats.samplingFreq = (uint32_t)(ADCButtonStats.count * 1000 / elapsedMs);
        ADCButtonStats.endTime = t;

        ADCButtonStats.averageValue = std::accumulate(ADCButtonStats.values.begin(), ADCButtonStats.values.end(), 0) / ADCButtonStats.count;

        for (uint32_t i = 0; i < ADCButtonStats.count; i++)
        {
            ADCButtonStats.diffValues[i] = abs((int32_t)ADCButtonStats.averageValue - (int32_t)ADCButtonStats.values[i]);
        }

        ADCButtonStats.noiseValue = std::accumulate(ADCButtonStats.diffValues.begin(), ADCButtonStats.diffValues.end(), 0) / ADCButtonStats.count * 2;
        // ADCButtonStats.noiseValue = 100;

        uint32_t crossCount = 0;
        for (uint32_t i = 0; i < ADCButtonStats.count; i++)
        {
            if (ADCButtonStats.diffValues[i] > ADCButtonStats.noiseValue * 2)
            {
                crossCount++;
            }
        }

        APP_DBG("avg: %d, noise: %d, freq: %d, cross: %d", ADCButtonStats.averageValue, ADCButtonStats.noiseValue, ADCButtonStats.samplingFreq, crossCount);

        MC.publish(MessageId::ADC_SAMPLING_STATS_COMPLETE, &ADCButtonStats);
    }
}

// 根据按钮索引查找对应的ADC索引
ADCIndexInfo ADCManager::findADCButtonVirtualPin(uint8_t virtualPin)
{
    // 检查 ADC1
    for (uint8_t i = 0; i < NUM_ADC1_BUTTONS; i++)
    {
        if (ADC1_BUTTONS_MAPPING[i] == virtualPin)
        {
            return ADCIndexInfo{0, int8_t(i)}; // 返回 ADC1 的索引
        }
    }

    // 检查 ADC2
    for (uint8_t i = 0; i < NUM_ADC2_BUTTONS; i++)
    {
        if (ADC2_BUTTONS_MAPPING[i] == virtualPin)
        {
            return ADCIndexInfo{1, int8_t(i)}; // 返回 ADC2 的索引
        }
    }

    // 检查 ADC3
    for (uint8_t i = 0; i < NUM_ADC3_BUTTONS; i++)
    {
        if (ADC3_BUTTONS_MAPPING[i] == virtualPin)
        {
            return ADCIndexInfo{2, int8_t(i)}; // 返回 ADC3 的索引
        }
    }

    return ADCIndexInfo{-1, -1}; // 如果没找到，默认返回 ADC1
}

// ADC转换完成回调
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    ADCManager::getInstance().notifyConversionComplete(hadc);
}

void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *hadc)
{
    ADCManager::getInstance().notifyConversionComplete(hadc);
}

void ADCManager::notifyConversionComplete(ADC_HandleTypeDef *hadc)
{
    if (hadc != nullptr && dmaSamplingActive && samplingRateEnabled) {
        handleADCStats(hadc);
    }
}

void ADCManager::notifyConversionError(ADC_HandleTypeDef *hadc)
{
    (void)hadc;
    samplingRateEnabled = false;
    dmaSamplingActive = false;
    disableAllDmaCompletionInterrupts();
}

// ADC错误回调
void HAL_ADC_ErrorCallback(ADC_HandleTypeDef *hadc)
{
    ADCManager::getInstance().notifyConversionError(hadc);
    uint32_t error = HAL_ADC_GetError(hadc);
    LOG_ERROR("ADC", "ADC Error: Instance=0x%p", (void *)hadc->Instance);
    LOG_ERROR("ADC", "State=0x%x", HAL_ADC_GetState(hadc));
    LOG_ERROR("ADC", "Error flags: 0x%lx", error);

    if (error & HAL_ADC_ERROR_INTERNAL)
        LOG_ERROR("ADC", "- Internal error");
    if (error & HAL_ADC_ERROR_OVR)
        LOG_ERROR("ADC", "- Overrun error");
    if (error & HAL_ADC_ERROR_DMA)
        LOG_ERROR("ADC", "- DMA transfer error");
}
