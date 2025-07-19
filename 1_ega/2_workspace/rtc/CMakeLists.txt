### DS3231 3V3 RTC LIBRARY - PICO 2 ###

# Creo biblioteca estatica rtc
add_library(rtc STATIC rtc.c)

# Linkeo dependencias
target_link_libraries(rtc PUBLIC hardware_i2c)

# Incluyo headers
target_include_directories(rtc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})