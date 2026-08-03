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
static uint8_t* data__images_cyber_scene_webp = NULL;
static uint8_t* data__index_html = NULL;
static uint8_t* data___next_static_chunks_polyfills_42372ed130431b0a_js = NULL;
static uint8_t* data___next_static_css_e6967a0efbc76327_css = NULL;
static uint8_t* data___next_static_js_app_layout_52230d42f6fe4aac_js = NULL;
static uint8_t* data___next_static_js_app_page_f5c161f7848ef8ec_js = NULL;
static uint8_t* data___next_static_js_main_app_0f51bb67db9ef4d0_js = NULL;

// 文件大小常量
#define SIZE__FONTS_CUSTOM_EN_TTF 89370
#define SIZE__IMAGES_CYBER_SCENE_WEBP 64530
#define SIZE__INDEX_HTML 18714
#define SIZE___NEXT_STATIC_CHUNKS_POLYFILLS_42372ED130431B0A_JS 39524
#define SIZE___NEXT_STATIC_CSS_E6967A0EFBC76327_CSS 1776
#define SIZE___NEXT_STATIC_JS_APP_LAYOUT_52230D42F6FE4AAC_JS 399940
#define SIZE___NEXT_STATIC_JS_APP_PAGE_F5C161F7848EF8EC_JS 389758
#define SIZE___NEXT_STATIC_JS_MAIN_APP_0F51BB67DB9EF4D0_JS 98808

static bool fsdata_inited = false;

struct fsdata_file file__fonts_custom_en_ttf[] = {{
    file_NULL,
    NULL,  // 将在运行时设置
    NULL,  // 将在运行时设置
    SIZE__FONTS_CUSTOM_EN_TTF - 24,
    FS_FILE_FLAGS_HEADER_INCLUDED | FS_FILE_FLAGS_HEADER_PERSISTENT
}};

struct fsdata_file file__images_cyber_scene_webp[] = {{
    file__fonts_custom_en_ttf,
    NULL,  // 将在运行时设置
    NULL,  // 将在运行时设置
    SIZE__IMAGES_CYBER_SCENE_WEBP - 28,
    FS_FILE_FLAGS_HEADER_INCLUDED | FS_FILE_FLAGS_HEADER_PERSISTENT
}};

struct fsdata_file file__index_html[] = {{
    file__images_cyber_scene_webp,
    NULL,  // 将在运行时设置
    NULL,  // 将在运行时设置
    SIZE__INDEX_HTML - 12,
    FS_FILE_FLAGS_HEADER_INCLUDED | FS_FILE_FLAGS_HEADER_PERSISTENT
}};

struct fsdata_file file___next_static_chunks_polyfills_42372ed130431b0a_js[] = {{
    file__index_html,
    NULL,  // 将在运行时设置
    NULL,  // 将在运行时设置
    SIZE___NEXT_STATIC_CHUNKS_POLYFILLS_42372ED130431B0A_JS - 52,
    FS_FILE_FLAGS_HEADER_INCLUDED | FS_FILE_FLAGS_HEADER_PERSISTENT
}};

struct fsdata_file file___next_static_css_e6967a0efbc76327_css[] = {{
    file___next_static_chunks_polyfills_42372ed130431b0a_js,
    NULL,  // 将在运行时设置
    NULL,  // 将在运行时设置
    SIZE___NEXT_STATIC_CSS_E6967A0EFBC76327_CSS - 40,
    FS_FILE_FLAGS_HEADER_INCLUDED | FS_FILE_FLAGS_HEADER_PERSISTENT
}};

struct fsdata_file file___next_static_js_app_layout_52230d42f6fe4aac_js[] = {{
    file___next_static_css_e6967a0efbc76327_css,
    NULL,  // 将在运行时设置
    NULL,  // 将在运行时设置
    SIZE___NEXT_STATIC_JS_APP_LAYOUT_52230D42F6FE4AAC_JS - 48,
    FS_FILE_FLAGS_HEADER_INCLUDED | FS_FILE_FLAGS_HEADER_PERSISTENT
}};

struct fsdata_file file___next_static_js_app_page_f5c161f7848ef8ec_js[] = {{
    file___next_static_js_app_layout_52230d42f6fe4aac_js,
    NULL,  // 将在运行时设置
    NULL,  // 将在运行时设置
    SIZE___NEXT_STATIC_JS_APP_PAGE_F5C161F7848EF8EC_JS - 48,
    FS_FILE_FLAGS_HEADER_INCLUDED | FS_FILE_FLAGS_HEADER_PERSISTENT
}};

struct fsdata_file file___next_static_js_main_app_0f51bb67db9ef4d0_js[] = {{
    file___next_static_js_app_page_f5c161f7848ef8ec_js,
    NULL,  // 将在运行时设置
    NULL,  // 将在运行时设置
    SIZE___NEXT_STATIC_JS_MAIN_APP_0F51BB67DB9EF4D0_JS - 48,
    FS_FILE_FLAGS_HEADER_INCLUDED | FS_FILE_FLAGS_HEADER_PERSISTENT
}};

static void update_file_pointers(void) {
    // 更新undefined的指针
    ((struct fsdata_file *)file__fonts_custom_en_ttf)->name = data__fonts_custom_en_ttf;
    ((struct fsdata_file *)file__fonts_custom_en_ttf)->data = data__fonts_custom_en_ttf + 24;

    // 更新undefined的指针
    ((struct fsdata_file *)file__images_cyber_scene_webp)->name = data__images_cyber_scene_webp;
    ((struct fsdata_file *)file__images_cyber_scene_webp)->data = data__images_cyber_scene_webp + 28;

    // 更新undefined的指针
    ((struct fsdata_file *)file__index_html)->name = data__index_html;
    ((struct fsdata_file *)file__index_html)->data = data__index_html + 12;

    // 更新undefined的指针
    ((struct fsdata_file *)file___next_static_chunks_polyfills_42372ed130431b0a_js)->name = data___next_static_chunks_polyfills_42372ed130431b0a_js;
    ((struct fsdata_file *)file___next_static_chunks_polyfills_42372ed130431b0a_js)->data = data___next_static_chunks_polyfills_42372ed130431b0a_js + 52;

    // 更新undefined的指针
    ((struct fsdata_file *)file___next_static_css_e6967a0efbc76327_css)->name = data___next_static_css_e6967a0efbc76327_css;
    ((struct fsdata_file *)file___next_static_css_e6967a0efbc76327_css)->data = data___next_static_css_e6967a0efbc76327_css + 40;

    // 更新undefined的指针
    ((struct fsdata_file *)file___next_static_js_app_layout_52230d42f6fe4aac_js)->name = data___next_static_js_app_layout_52230d42f6fe4aac_js;
    ((struct fsdata_file *)file___next_static_js_app_layout_52230d42f6fe4aac_js)->data = data___next_static_js_app_layout_52230d42f6fe4aac_js + 48;

    // 更新undefined的指针
    ((struct fsdata_file *)file___next_static_js_app_page_f5c161f7848ef8ec_js)->name = data___next_static_js_app_page_f5c161f7848ef8ec_js;
    ((struct fsdata_file *)file___next_static_js_app_page_f5c161f7848ef8ec_js)->data = data___next_static_js_app_page_f5c161f7848ef8ec_js + 48;

    // 更新undefined的指针
    ((struct fsdata_file *)file___next_static_js_main_app_0f51bb67db9ef4d0_js)->name = data___next_static_js_main_app_0f51bb67db9ef4d0_js;
    ((struct fsdata_file *)file___next_static_js_main_app_0f51bb67db9ef4d0_js)->data = data___next_static_js_main_app_0f51bb67db9ef4d0_js + 48;

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
        data__images_cyber_scene_webp = (uint8_t*)addr;
        addr += size;

        size = read_uint32_be(base_ptr + 12);
        data__index_html = (uint8_t*)addr;
        addr += size;

        size = read_uint32_be(base_ptr + 16);
        data___next_static_chunks_polyfills_42372ed130431b0a_js = (uint8_t*)addr;
        addr += size;

        size = read_uint32_be(base_ptr + 20);
        data___next_static_css_e6967a0efbc76327_css = (uint8_t*)addr;
        addr += size;

        size = read_uint32_be(base_ptr + 24);
        data___next_static_js_app_layout_52230d42f6fe4aac_js = (uint8_t*)addr;
        addr += size;

        size = read_uint32_be(base_ptr + 28);
        data___next_static_js_app_page_f5c161f7848ef8ec_js = (uint8_t*)addr;
        addr += size;

        size = read_uint32_be(base_ptr + 32);
        data___next_static_js_main_app_0f51bb67db9ef4d0_js = (uint8_t*)addr;
        addr += size;


        // 更新文件结构体中的指针
        update_file_pointers();

        fsdata_inited = true;
    }

    return file___next_static_js_main_app_0f51bb67db9ef4d0_js;
}

const uint8_t numfiles = 8;
