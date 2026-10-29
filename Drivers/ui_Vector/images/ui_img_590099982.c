#ifdef __has_include
    #if __has_include("lvgl.h")
        #ifndef LV_LVGL_H_INCLUDE_SIMPLE
            #define LV_LVGL_H_INCLUDE_SIMPLE
        #endif
    #endif
#endif

#if defined(LV_LVGL_H_INCLUDE_SIMPLE)
    #include "lvgl.h"
#else
    #include "lvgl/lvgl.h"
#endif


#ifndef LV_ATTRIBUTE_MEM_ALIGN
#define LV_ATTRIBUTE_MEM_ALIGN
#endif

#ifndef LV_ATTRIBUTE_IMAGE_UI_IMG_590099982
#define LV_ATTRIBUTE_IMAGE_UI_IMG_590099982
#endif

const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST LV_ATTRIBUTE_IMAGE_UI_IMG_590099982 uint8_t ui_img_590099982_map[] = {
  0x92, 0x46, 0x72, 0x46, 0x72, 0x46, 
  0x72, 0x46, 0x72, 0x46, 0x92, 0x46, 
  0x72, 0x46, 0x72, 0x46, 0x92, 0x46, 
  0x72, 0x46, 0x72, 0x46, 0x92, 0x46, 
  0x92, 0x4e, 0x92, 0x4e, 0x00, 0x00, 

  0xdf, 0xff, 0x30, 
  0xff, 0xff, 0x3f, 
  0xff, 0xff, 0x3f, 
  0xff, 0xff, 0x3f, 
  0x3f, 0x3f, 0x00, 
};

const lv_image_dsc_t ui_img_590099982 = {
  .header.cf = LV_COLOR_FORMAT_RGB565A8,
  .header.magic = LV_IMAGE_HEADER_MAGIC,
  .header.w = 3,
  .header.h = 5,
  .data_size = 15 * 3,
  .data = ui_img_590099982_map,
};
