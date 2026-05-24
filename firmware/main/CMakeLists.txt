idf_component_register(
    SRCS             "app_main.cpp" "app_driver.cpp" "dm631.c"
    INCLUDE_DIRS     "."
    PRIV_INCLUDE_DIRS "." "${ESP_MATTER_PATH}/examples/common/utils"
)

target_compile_options(${COMPONENT_LIB} PRIVATE "-DCHIP_HAVE_CONFIG_H")
