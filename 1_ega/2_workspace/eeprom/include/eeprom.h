
#ifndef EEPROM_H
#define EEPROM_H

#include "hardware/i2c.h"
#include <stdint.h>
#include <stddef.h>

#define EEPROM_ADDR 0x57
#define EEPROM_PAGE_SIZE 32

typedef enum {
    EEPROM_DATA_SETPOINT,
    EEPROM_DATA_ALARMA
} eeprom_data_type_t;

typedef enum {
    ID_VMAX, ID_IMAX, ID_R1, ID_R2, ID_R3, ID_R4,
    ID_R5, ID_R6, ID_R7, ID_R8, ID_R9, ID_R10, ID_UNKNOWN
} eeprom_data_id_t;

void eeprom_write_data(i2c_inst_t *i2c, uint16_t mem_address, const uint8_t *data, size_t len);
void eeprom_read_data(i2c_inst_t *i2c, uint16_t mem_address, uint8_t *data, size_t len);

#endif // EEPROM_H
