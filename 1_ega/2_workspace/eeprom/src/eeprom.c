
#include "eeprom.h"
#include "pico/stdlib.h"
#include <string.h>



/* void eeprom_write_data(i2c_inst_t *i2c, uint16_t mem_address, const uint8_t *data, size_t len) {
    uint8_t buffer[EEPROM_PAGE_SIZE + 2];
    buffer[0] = (mem_address >> 8) & 0xFF;  // MSB
    buffer[1] = mem_address & 0xFF;         // LSB
    memcpy(&buffer[2], data, len);
    i2c_write_blocking(i2c, EEPROM_ADDR, buffer, len + 2, false);
    sleep_ms(5);  // Tiempo de escritura típico
} */

// -------------------------
// Escritura segura con manejo de páginas
// -------------------------
#define EEPROM_WRITE_DELAY_MS 5

void eeprom_write_data(i2c_inst_t *i2c, uint16_t mem_address, const uint8_t *data, size_t len) {
    size_t bytes_written = 0;

    while (bytes_written < len) {
        // Calcula la dirección de página y cuánto queda libre en ella
        uint16_t page_offset = (mem_address + bytes_written) % EEPROM_PAGE_SIZE;
        size_t space_in_page = EEPROM_PAGE_SIZE - page_offset;
        size_t bytes_to_write = (len - bytes_written < space_in_page) ? (len - bytes_written) : space_in_page;

        // Arma buffer de escritura: dirección (2 bytes) + datos parciales
        uint8_t buffer[EEPROM_PAGE_SIZE + 2];
        uint16_t addr = mem_address + bytes_written;
        buffer[0] = (addr >> 8) & 0xFF;
        buffer[1] = addr & 0xFF;
        memcpy(&buffer[2], data + bytes_written, bytes_to_write);

        // Escribe bloque
        i2c_write_blocking(i2c, EEPROM_ADDR, buffer, bytes_to_write + 2, false);
        sleep_ms(EEPROM_WRITE_DELAY_MS);  // Espera de escritura

        bytes_written += bytes_to_write;
    }
}

void eeprom_read_data(i2c_inst_t *i2c, uint16_t mem_address, uint8_t *data, size_t len) {
    size_t bytes_read = 0;

    while (bytes_read < len) {
        uint16_t addr = mem_address + bytes_read;
        uint8_t addr_buf[2] = {
            (addr >> 8) & 0xFF,
            addr & 0xFF
        };

        size_t bytes_to_read = (len - bytes_read > EEPROM_PAGE_SIZE) ? EEPROM_PAGE_SIZE : (len - bytes_read);

        i2c_write_blocking(i2c, EEPROM_ADDR, addr_buf, 2, true);
        i2c_read_blocking(i2c, EEPROM_ADDR, data + bytes_read, bytes_to_read, false);

        bytes_read += bytes_to_read;
    }
}

void eeprom_format(i2c_inst_t *i2c) {
    uint8_t buffer[EEPROM_PAGE_SIZE];
    for (int i = 0; i < EEPROM_PAGE_SIZE; i++) buffer[i] = 0xFF;   // valor por defecto (puede ser 0x00)

    //printf("Formateando EEPROM...\n");
    for (uint16_t addr = 0; addr < EEPROM_SIZE_BYTES; addr += EEPROM_PAGE_SIZE) {

        uint8_t data[EEPROM_PAGE_SIZE + 2];
        data[0] = (addr >> 8) & 0xFF;   // MSB
        data[1] = addr & 0xFF;          // LSB
        memcpy(&data[2], buffer, EEPROM_PAGE_SIZE);

        i2c_write_blocking(i2c, EEPROM_ADDR, data, EEPROM_PAGE_SIZE + 2, false);
        sleep_ms(10); // tiempo típico de escritura de página (tWR = 5ms típico)
    }
    //printf("EEPROM formateada correctamente.\n");
}