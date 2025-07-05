# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.1.1)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.1.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico CACHE STRING "Board type")

# Crear la biblioteca estática "lcd" con los archivos fuente
add_library(lcd STATIC
    src/lcd.c
)

# Linkeo dependencias de la bibliotecas
target_link_libraries(lcd
    pico_stdlib
    hardware_i2c
)

# Incluir las cabeceras de la biblioteca
target_include_directories(lcd PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/include
)
