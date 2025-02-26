#include "utils.h"
#include <stdio.h>
#include <math.h>

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
        printf("%d", (value >> i) & 1);
        if (i % 8 == 0) printf(" ");  // 每8位添加一个空格
    }
    printf("\n");
}


