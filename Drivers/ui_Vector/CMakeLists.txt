SET(SOURCES screens/ui_ScreenStart.c
    screens/ui_ScreenHeadMain.c
    screens/ui_ScreenMenu.c
    screens/ui_ScreenMeasurements.c
    screens/ui_ScreenTestSound.c
    ui_theme_manager.c
    ui_themes.c
    ui.c
    components/ui_comp_hook.c
    ui_helpers.c
    images/ui_img_imagehead_mirax_png.c
    images/ui_img_imagehead_battery_png.c
    images/ui_img_imagehead_group_png.c
    images/ui_img_imagehead_battery_charge_png.c
    images/ui_img_imagehead_ble_png.c
    images/ui_img_imagehead_fan_png.c
    images/ui_img_imagehead_lora_png.c
    images/ui_img_imagehead_lte_vector_png.c
    images/ui_img_imagehead_lte_png.c
    images/ui_img_imagehead_transmit_png.c
    images/ui_img_imagehead_vector_png.c
    images/ui_img_imagehead_volume_mute_on_png.c
    images/ui_img_imagemenu_arrow_png.c
    images/ui_img_imagemenu_arrow1_png.c
    images/ui_img_imagemenu_checked_png.c
    images/ui_img_imagemenu_info_png.c
    images/ui_img_imagemenu_service_menu_png.c
    images/ui_img_imagemenu_setting_png.c
    images/ui_img_imagemenu_unchecked_png.c
    fonts/ui_font_number11.c
    fonts/ui_font_number16.c
    fonts/ui_font_number18.c
    fonts/ui_font_number36.c)

add_library(ui ${SOURCES})
