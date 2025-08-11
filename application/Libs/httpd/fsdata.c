#include "board_cfg.h"
#include "fsdata.h"
#include "qspi-w25q64.h"
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

#define file_NULL (struct fsdata_file *) NULL

#ifndef FS_FILE_FLAGS_HEADER_INCLUDED
#define FS_FILE_FLAGS_HEADER_INCLUDED 1
#endif
#ifndef FS_FILE_FLAGS_HEADER_PERSISTENT
#define FS_FILE_FLAGS_HEADER_PERSISTENT 0
#endif
#ifndef FSDATA_FILE_ALIGNMENT
#define FSDATA_FILE_ALIGNMENT 0
#endif
#ifndef FSDATA_ALIGN_PRE
#define FSDATA_ALIGN_PRE
#endif
#ifndef FSDATA_ALIGN_POST
#define FSDATA_ALIGN_POST
#endif

// 文件数据指针
static uint8_t* data__fonts_custom_en_ttf = NULL;
static uint8_t* data__index_html = NULL;
static uint8_t* data___next_static_js_app_layout_e393621a281e31f6_js = NULL;
static uint8_t* data___next_static_js_app_page_06c2b62519cd8917_js = NULL;
static uint8_t* data___next_static_js_app__not_found_page_74cc9060c45c4b1e_js = NULL;
static uint8_t* data___next_static_js_main_app_967b622ad6c69df8_js = NULL;

// 文件大小常量
#define SIZE__FONTS_CUSTOM_EN_TTF 89370
#define SIZE__INDEX_HTML 30315
#define SIZE___NEXT_STATIC_JS_APP_LAYOUT_E393621A281E31F6_JS 350881
#define SIZE___NEXT_STATIC_JS_APP_PAGE_06C2B62519CD8917_JS 266466
#define SIZE___NEXT_STATIC_JS_APP__NOT_FOUND_PAGE_74CC9060C45C4B1E_JS 1041
#define SIZE___NEXT_STATIC_JS_MAIN_APP_967B622AD6C69DF8_JS 98671

static bool fsdata_inited = false;

struct fsdata_file file__fonts_custom_en_ttf[] = {{
    file_NULL,
    NULL,  // 将在运行时设置
    NULL,  // 将在运行时设置
    SIZE__FONTS_CUSTOM_EN_TTF - 24,
    FS_FILE_FLAGS_HEADER_INCLUDED | FS_FILE_FLAGS_HEADER_PERSISTENT
}};

struct fsdata_file file__index_html[] = {{
    file__fonts_custom_en_ttf,
    NULL,  // 将在运行时设置
    NULL,  // 将在运行时设置
    SIZE__INDEX_HTML - 12,
    FS_FILE_FLAGS_HEADER_INCLUDED | FS_FILE_FLAGS_HEADER_PERSISTENT
}};

struct fsdata_file file___next_static_js_app_layout_e393621a281e31f6_js[] = {{
    file__index_html,
    NULL,  // 将在运行时设置
    NULL,  // 将在运行时设置
    SIZE___NEXT_STATIC_JS_APP_LAYOUT_E393621A281E31F6_JS - 48,
    FS_FILE_FLAGS_HEADER_INCLUDED | FS_FILE_FLAGS_HEADER_PERSISTENT
}};

struct fsdata_file file___next_static_js_app_page_06c2b62519cd8917_js[] = {{
    file___next_static_js_app_layout_e393621a281e31f6_js,
    NULL,  // 将在运行时设置
    NULL,  // 将在运行时设置
    SIZE___NEXT_STATIC_JS_APP_PAGE_06C2B62519CD8917_JS - 48,
    FS_FILE_FLAGS_HEADER_INCLUDED | FS_FILE_FLAGS_HEADER_PERSISTENT
}};

struct fsdata_file file___next_static_js_app__not_found_page_74cc9060c45c4b1e_js[] = {{
    file___next_static_js_app_page_06c2b62519cd8917_js,
    NULL,  // 将在运行时设置
    NULL,  // 将在运行时设置
    SIZE___NEXT_STATIC_JS_APP__NOT_FOUND_PAGE_74CC9060C45C4B1E_JS - 60,
    FS_FILE_FLAGS_HEADER_INCLUDED | FS_FILE_FLAGS_HEADER_PERSISTENT
}};

struct fsdata_file file___next_static_js_main_app_967b622ad6c69df8_js[] = {{
    file___next_static_js_app__not_found_page_74cc9060c45c4b1e_js,
    NULL,  // 将在运行时设置
    NULL,  // 将在运行时设置
    SIZE___NEXT_STATIC_JS_MAIN_APP_967B622AD6C69DF8_JS - 48,
    FS_FILE_FLAGS_HEADER_INCLUDED | FS_FILE_FLAGS_HEADER_PERSISTENT
}};

static void update_file_pointers(void) {
    // 更新undefined的指针
    ((struct fsdata_file *)file__fonts_custom_en_ttf)->name = data__fonts_custom_en_ttf;
    ((struct fsdata_file *)file__fonts_custom_en_ttf)->data = data__fonts_custom_en_ttf + 24;

    // 更新undefined的指针
    ((struct fsdata_file *)file__index_html)->name = data__index_html;
    ((struct fsdata_file *)file__index_html)->data = data__index_html + 12;

    // 更新undefined的指针
    ((struct fsdata_file *)file___next_static_js_app_layout_e393621a281e31f6_js)->name = data___next_static_js_app_layout_e393621a281e31f6_js;
    ((struct fsdata_file *)file___next_static_js_app_layout_e393621a281e31f6_js)->data = data___next_static_js_app_layout_e393621a281e31f6_js + 48;

    // 更新undefined的指针
    ((struct fsdata_file *)file___next_static_js_app_page_06c2b62519cd8917_js)->name = data___next_static_js_app_page_06c2b62519cd8917_js;
    ((struct fsdata_file *)file___next_static_js_app_page_06c2b62519cd8917_js)->data = data___next_static_js_app_page_06c2b62519cd8917_js + 48;

    // 更新undefined的指针
    ((struct fsdata_file *)file___next_static_js_app__not_found_page_74cc9060c45c4b1e_js)->name = data___next_static_js_app__not_found_page_74cc9060c45c4b1e_js;
    ((struct fsdata_file *)file___next_static_js_app__not_found_page_74cc9060c45c4b1e_js)->data = data___next_static_js_app__not_found_page_74cc9060c45c4b1e_js + 60;

    // 更新undefined的指针
    ((struct fsdata_file *)file___next_static_js_main_app_967b622ad6c69df8_js)->name = data___next_static_js_main_app_967b622ad6c69df8_js;
    ((struct fsdata_file *)file___next_static_js_main_app_967b622ad6c69df8_js)->data = data___next_static_js_main_app_967b622ad6c69df8_js + 48;

}

const struct fsdata_file * getFSRoot(void)
{
    if(fsdata_inited == false) {
        uint32_t len;
        uint32_t addr;
        uint32_t size;

        uint8_t *base_ptr = (uint8_t*)(WEB_RESOURCES_ADDR);
        uint32_t *size_ptr = (uint32_t*)base_ptr;
        len = read_uint32_be(base_ptr);
        addr = WEB_RESOURCES_ADDR + 4 * (len + 1);  // 跳过文件数量和所有size

        size = read_uint32_be(base_ptr + 4);
        data__fonts_custom_en_ttf = (uint8_t*)addr;
        addr += size;

        size = read_uint32_be(base_ptr + 8);
        data__index_html = (uint8_t*)addr;
        addr += size;

        size = read_uint32_be(base_ptr + 12);
        data___next_static_js_app_layout_e393621a281e31f6_js = (uint8_t*)addr;
        addr += size;

        size = read_uint32_be(base_ptr + 16);
        data___next_static_js_app_page_06c2b62519cd8917_js = (uint8_t*)addr;
        addr += size;

        size = read_uint32_be(base_ptr + 20);
        data___next_static_js_app__not_found_page_74cc9060c45c4b1e_js = (uint8_t*)addr;
        addr += size;

        size = read_uint32_be(base_ptr + 24);
        data___next_static_js_main_app_967b622ad6c69df8_js = (uint8_t*)addr;
        addr += size;


        // 更新文件结构体中的指针
        update_file_pointers();

        fsdata_inited = true;
    }

    return file___next_static_js_main_app_967b622ad6c69df8_js;
}

const uint8_t numfiles = 6;
