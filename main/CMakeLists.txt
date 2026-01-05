# Collect source files from subdirectories
file(GLOB CORE_SOURCES core/*.c)
file(GLOB SCREEN_SOURCES ui/screens/*.c)
file(GLOB WIDGET_SOURCES ui/widgets/*.c)

idf_component_register(
    SRCS main.c ${CORE_SOURCES} ${SCREEN_SOURCES} ${WIDGET_SOURCES}
    INCLUDE_DIRS . core ui/screens ui/widgets
    REQUIRES waveshare_bsp esp_wifi esp_netif esp_http_server nvs_flash sd_database json
    EMBED_TXTFILES 
        index.html
        setup.html
        widgets.html
        settings.html
        styles.css
        app.js
        api.js
    )

idf_component_get_property(LVGL_LIB lvgl__lvgl COMPONENT_LIB)
target_compile_options(
    ${LVGL_LIB} 
    PRIVATE
        -DLV_LVGL_H_INCLUDE_SIMPLE
        -DLV_USE_DEMO_MUSIC
)
