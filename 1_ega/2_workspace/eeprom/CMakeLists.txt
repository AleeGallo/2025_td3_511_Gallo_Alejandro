### AT24C32 8X32K EEPROM LIBRARY - PICO 2 ###

# Creo biblioteca estatica eeprom
add_library(eeprom STATIC eeprom.c)

# Linkeo dependencias
target_link_libraries(eeprom PUBLIC hardware_i2c)

# Incluyo headers
target_include_directories(eeprom PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})