#ifndef __UTILS_H__
#define __UTILS_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32h7xx_hal.h"


uint32_t RGBToHex(uint8_t red, uint8_t green, uint8_t blue);

struct RGBColor {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

struct RGBColor hexToRGB(uint32_t color);

/**
 * 已知两个测试点的磁场强度以及这两个测试点的距离、磁芯半径、磁芯长度，求解磁化强度和第一个测试点的位置
 * 用牛顿-拉夫森法求解方程组
 * @param vars 磁化强度和第一个测试点的位置  vars[0] = M, vars[1] = z1
 * @param B1 第一个测试点的磁场强度
 * @param B2 第二个测试点的磁场强度
 * @param L 磁芯长度
 * @param R 磁芯半径
 * @param d 两个测试点的距离
 */
void newton_raphson(float_t vars[], float_t B1, float_t B2, float_t L, float_t R, float_t d);

/**
 * 已知磁芯长度、磁芯半径、磁化强度，求解磁芯轴向磁场为某个值时，磁芯轴向距离
 * @param L 磁芯长度
 * @param R 磁芯半径
 * @param M 磁化强度
 * @param B_target 目标磁场
 * @return 磁芯轴向距离
 */
float_t find_distance_for_axial_field(float_t L, float_t R, float_t M, float_t B_target);

/******************************** hack 解决 未定义的符号__aeabi_assert 报错 begin ******************************************/
__attribute__((weak, noreturn)) void __aeabi_assert(const char *expr, const char *file, int line);
__attribute__((weak)) void abort(void);
/******************************** hack 解决 未定义的符号__aeabi_assert 报错 end ********************************************/

/******************************** RAM内存动态分配 begin ******************************************/
void* ram_alloc(size_t size);
/******************************** RAM内存动态分配 end ******************************************/

/******************************** 大端读取函数 begin ******************************************/
uint32_t read_uint32_be(const uint8_t* data);
/******************************** 大端读取函数 end ******************************************/

/******************************** 小端读取函数 begin ******************************************/
uint32_t read_uint32_le(const uint8_t* data);
/******************************** 小端读取函数 end ******************************************/

/******************************** 打印二进制函数 begin ******************************************/
void printBinary(const char* prefix, uint32_t value);
/******************************** 打印二进制函数 end ******************************************/

/******************************** STM32唯一ID读取函数 begin ******************************************/
/**
 * 返回STM32唯一ID字符串
 * @return 包含96位唯一ID的缓冲区
 */
char* str_stm32_unique_id();

/**
 * 基于STM32唯一ID生成安全的设备ID哈希值
 * 使用SHA-256风格的哈希算法对原始唯一ID进行处理
 * @return 64位十六进制哈希字符串
 */
char* get_device_id_hash();
/******************************** STM32唯一ID读取函数 end ******************************************/

#ifdef __cplusplus
}
#endif

#endif /* __UTILS_H__ */