#include "utils.h"
#include <stdio.h>
#include <math.h>
#include "board_cfg.h"

uint32_t RGBToHex(uint8_t red, uint8_t green, uint8_t blue)
{
  return ((green & 0xff) << 16 | (red & 0xff) << 8 | (blue & 0xff));
}

struct RGBColor hexToRGB(uint32_t color)
{
    struct RGBColor c = {
        .r = (uint8_t) (color >> 16 & 0xff),
        .g = (uint8_t) (color >> 8 & 0xff),
        .b = (uint8_t) (color & 0xff),
    };
    return c;
}


// 定义常量
#ifndef M_PI
#define M_PI		3.14159265358979323846
#endif

#define MU_0 (4 * M_PI * 1e-7) // 真空磁导率

// 定义方程组
void equations(float_t vars[], float_t B1, float_t B2, float_t L, float_t R, float_t d, float_t eqs[]) {
    float_t M = vars[0];
    float_t z1 = vars[1];

    float_t term1_1 = (L / 2 + z1) / sqrt(R * R + (L / 2 + z1) * (L / 2 + z1));
    float_t term1_2 = (L / 2 - z1) / sqrt(R * R + (L / 2 - z1) * (L / 2 - z1));
    float_t term2_1 = (L / 2 + (z1 + d)) / sqrt(R * R + (L / 2 + (z1 + d)) * (L / 2 + (z1 + d)));
    float_t term2_2 = (L / 2 - (z1 + d)) / sqrt(R * R + (L / 2 - (z1 + d)) * (L / 2 - (z1 + d)));

    eqs[0] = B1 - (MU_0 / 2) * M * (term1_1 - term1_2);
    eqs[1] = B2 - (MU_0 / 2) * M * (term2_1 - term2_2);
}

/**
 * 牛顿-拉夫森法求解方程组，求解磁化强度
 * 通过迭代法求解方程组，直到误差小于1e-6
 * 
 * @param vars 用于存储猜测的磁化强度范围
 * @param B1 第一个点的磁场强度
 * @param B2 第二个点的磁场强度
 * @param L 磁芯长度
 * @param R 磁芯半径
 * @param d 两个测试点之间的距离
 */
void newton_raphson(float_t vars[], float_t B1, float_t B2, float_t L, float_t R, float_t d) {
    float_t tol = 1e-6; // 误差容限
    int max_iter = 100; // 最大迭代次数
    float_t eqs[2];
    float_t J[2][2]; // 雅可比矩阵
    float_t delta[2];
    int iter;

    for (iter = 0; iter < max_iter; iter++) {
        equations(vars, B1, B2, L, R, d, eqs);

        // 计算雅可比矩阵
        float_t h = 1e-6; // 微小增量
        float_t vars_temp[2];

        for (int i = 0; i < 2; i++) {
            for (int j = 0; j < 2; j++) {
                vars_temp[j] = vars[j];
            }
            vars_temp[i] += h;
            float_t eqs_temp[2];
            equations(vars_temp, B1, B2, L, R, d, eqs_temp);
            J[0][i] = (eqs_temp[0] - eqs[0]) / h;
            J[1][i] = (eqs_temp[1] - eqs[1]) / h;
        }

        // 计算 delta = J^(-1) * eqs
        float_t det = J[0][0] * J[1][1] - J[0][1] * J[1][0];
        delta[0] = (J[1][1] * eqs[0] - J[0][1] * eqs[1]) / det;
        delta[1] = (-J[1][0] * eqs[0] + J[0][0] * eqs[1]) / det;

        // 更新变量
        vars[0] -= delta[0];
        vars[1] -= delta[1];

        // 检查收敛
        if (fabs(delta[0]) < tol && fabs(delta[1]) < tol) {
            break;
        }
    }
}

// 计算轴线上磁场强度的函数
float_t calculate_axial_magnetic_field(float_t L, float_t R, float_t M, float_t z) {
    float_t term1 = (L / 2 + z) / sqrt(R * R + (L / 2 + z) * (L / 2 + z));
    float_t term2 = (L / 2 - z) / sqrt(R * R + (L / 2 - z) * (L / 2 - z));
    float_t B_z = (MU_0 / 2) * M * (term1 - term2);
    return B_z;
}

#define TOLERANCE 10 // 误差容忍度

// 使用二分法反向求解轴线上距离
float_t find_distance_for_axial_field(float_t L, float_t R, float_t M, float_t B_target) {
    float_t low = 0.0;
    float_t high = 10.0; // 假设一个较大的初始上限
    float_t mid;

    while (high - low > TOLERANCE) {
        mid = (low + high) / 2.0;
        float_t B_mid = calculate_axial_magnetic_field(L, R, M, mid);

        if (B_mid > B_target) {
            low = mid;
        } else {
            high = mid;
        }
    }

    return (low + high) / 2.0;
}

/******************************** hack 解决 未定义的符号__aeabi_assert 报错 begin ******************************************/

__attribute__((weak, noreturn))
void __aeabi_assert(const char *expr, const char *file, int line)
{
    char str[12], *p;

    fputs("*** assertion failed: ", stderr);
    fputs(expr, stderr);
    fputs(", file ", stderr);
    fputs(file, stderr);
    fputs(", line ", stderr);

    p = str + sizeof(str);
    *--p = '\0';
    *--p = '\n';
    while (line > 0)
    {
        *--p = '0' + (line % 10);
        line /= 10;
    }
    fputs(p, stderr);

    abort();
}

__attribute__((weak))
void abort(void)
{
    for (;;)
        ;
}
/******************************** hack 解决 未定义的符号__aeabi_assert 报错 end ********************************************/

/******************************** RAM内存动态分配 begin ******************************************/
// 定义RAM区域的起始地址和大小（根据链接器脚本中的定义）
#define RAM_START_ADDR      0x24000000
#define RAM_SIZE           (512 * 1024)  // 512KB
#define RAM_ALIGNMENT      32

// 用于跟踪RAM分配的简单分配器
static uint32_t current_ram_addr = RAM_START_ADDR;

// 简单的内存分配函数，返回对齐的地址
void* ram_alloc(size_t size) {
	// 确保大小是32字节对齐的
	size = (size + RAM_ALIGNMENT - 1) & ~(RAM_ALIGNMENT - 1);
	
	// 检查是否有足够的空间
	if (current_ram_addr + size > RAM_START_ADDR + RAM_SIZE) {
		return NULL;
	}
	
	// 保存当前地址
	void* allocated_addr = (void*)current_ram_addr;
	
	// 更新下一个可用地址
	current_ram_addr += size;
	
	return allocated_addr;
}
/******************************** RAM内存动态分配 end ******************************************/

// 大端读取函数定义
uint32_t read_uint32_be(const uint8_t* data) {
    return ((uint32_t)data[0] << 24) | 
           ((uint32_t)data[1] << 16) | 
           ((uint32_t)data[2] << 8) | 
           (uint32_t)data[3];
}

// 小端读取函数定义
uint32_t read_uint32_le(const uint8_t* data) {
    return ((uint32_t)data[3] << 24) | 
           ((uint32_t)data[2] << 16) | 
           ((uint32_t)data[1] << 8) | 
           (uint32_t)data[0];
}

// 将32位整数转换为二进制字符串
void printBinary(const char* prefix, uint32_t value) {
    printf("%s0b", prefix);
    for (int i = 31; i >= 0; i--) {
        printf("%d", (int)((value >> i) & 1));
        if (i % 8 == 0) printf(" ");  // 每8位添加一个空格
    }
    printf("\n");
}

/******************************** STM32唯一ID读取函数 begin ******************************************/
/**
 * 返回STM32唯一ID字符串
 * @return 包含96位唯一ID的缓冲区
 */
char* str_stm32_unique_id() {
    // 静态缓冲区，用于存储格式化后的唯一ID字符串
    // 格式: XXXXXXXX-XXXXXXXX-XXXXXXXX (8+1+8+1+8+1 = 27字符)
    static char uid_str[28];  // 26个字符 + 1个终止符
    
    // 直接从内存中读取唯一ID
    uint32_t *uid_ptr = (uint32_t *)STM32_UNIQUE_ID_BASE_ADDR;
    
    // 读取3个32位字（共96位）
    uint32_t uid_word0 = uid_ptr[0];  // 第一个32位字
    uint32_t uid_word1 = uid_ptr[1];  // 第二个32位字
    uint32_t uid_word2 = uid_ptr[2];  // 第三个32位字
    
    // 格式化为十六进制字符串，格式: XXXXXXXX-XXXXXXXX-XXXXXXXX
    snprintf(uid_str, sizeof(uid_str), "%08X-%08X-%08X", 
             (unsigned int)uid_word0, 
             (unsigned int)uid_word1, 
             (unsigned int)uid_word2);
    
    return uid_str;
}

/**
 * 基于STM32唯一ID生成安全的设备ID哈希值
 * 使用多轮哈希和盐值确保安全性（与release.py中的算法保持一致）
 * @return 16位十六进制哈希字符串（64位哈希值）
 */
char* get_device_id_hash() {
    // 静态缓冲区，用于存储哈希结果
    static char hash_str[17];  // 16个字符 + 1个终止符
    
    // 从内存中读取原始唯一ID
    uint32_t *uid_ptr = (uint32_t *)STM32_UNIQUE_ID_BASE_ADDR;
    uint32_t uid_word0 = uid_ptr[0];
    uint32_t uid_word1 = uid_ptr[1];
    uint32_t uid_word2 = uid_ptr[2];
    
    // 与 release.py 中相同的哈希算法
    // 盐值常量
    uint32_t salt1 = 0x48426F78;  // "HBox"
    uint32_t salt2 = 0x32303234;  // "2024"
    
    // 质数常量
    uint32_t prime1 = 0x9E3779B9;  // 黄金比例的32位表示
    uint32_t prime2 = 0x85EBCA6B;  // 另一个质数
    uint32_t prime3 = 0xC2B2AE35;  // 第三个质数
    
    // 第一轮哈希
    uint32_t hash1 = (uid_word0 ^ salt1) & 0xFFFFFFFF;
    hash1 = ((hash1 << 13) | ((hash1 & 0xFFFFFFFF) >> 19)) & 0xFFFFFFFF;  // 左循环移位13位
    hash1 = (hash1 * prime1) & 0xFFFFFFFF;
    hash1 = (hash1 ^ uid_word1) & 0xFFFFFFFF;
    
    // 第二轮哈希
    uint32_t hash2 = (uid_word1 ^ salt2) & 0xFFFFFFFF;
    hash2 = ((hash2 << 17) | ((hash2 & 0xFFFFFFFF) >> 15)) & 0xFFFFFFFF;  // 左循环移位17位
    hash2 = (hash2 * prime2) & 0xFFFFFFFF;
    hash2 = (hash2 ^ uid_word2) & 0xFFFFFFFF;
    
    // 第三轮哈希
    uint32_t hash3 = (uid_word2 ^ ((salt1 + salt2) & 0xFFFFFFFF)) & 0xFFFFFFFF;
    hash3 = ((hash3 << 21) | ((hash3 & 0xFFFFFFFF) >> 11)) & 0xFFFFFFFF;  // 左循环移位21位
    hash3 = (hash3 * prime3) & 0xFFFFFFFF;
    hash3 = (hash3 ^ hash1) & 0xFFFFFFFF;
    
    // 最终组合
    uint32_t final_hash1 = (hash1 ^ hash2) & 0xFFFFFFFF;
    uint32_t final_hash2 = (hash2 ^ hash3) & 0xFFFFFFFF;
    
    // 转换为16位十六进制字符串 (64位哈希)
    snprintf(hash_str, sizeof(hash_str), "%08X%08X", 
             (unsigned int)final_hash1, 
             (unsigned int)final_hash2);
    
    return hash_str;
}
/******************************** STM32唯一ID读取函数 end ******************************************/


