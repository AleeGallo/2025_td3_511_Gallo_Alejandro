
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

/* void eeprom_write_data(i2c_inst_t *i2c, uint16_t mem_address, const uint8_t *data, size_t len) {
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
} */

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

/* int eeprom_write_data(i2c_inst_t *i2c, uint16_t mem_address, const uint8_t *data, size_t len) {
    // escribe una página (o varias páginas) — caller se debe asegurar de no cruzar boundary
    uint8_t buffer[EEPROM_PAGE_SIZE + 2];
    buffer[0] = (mem_address >> 8) & 0xFF;  // MSB
    buffer[1] = mem_address & 0xFF;         // LSB
    memcpy(&buffer[2], data, len);
    int ret = i2c_write_blocking(i2c, EEPROM_ADDR, buffer, len + 2, false);
    if (ret < 0) return ret;

    // Poll device ready: intento de escribir la dirección sin liberar bus; si ACK -> listo
    const uint32_t timeout_ms = 50; // ajustá según EEPROM (50-100ms seguro)
    uint32_t t = to_ms_since_boot(get_absolute_time());
    while ((to_ms_since_boot(get_absolute_time()) - t) < timeout_ms) {
        // intento de escribir solo la dirección de memoria (2 bytes) como comprobación
        uint8_t addr_buf[2] = { (mem_address >> 8) & 0xFF, mem_address & 0xFF };
        int r = i2c_write_blocking(i2c, EEPROM_ADDR, addr_buf, 2, true);
        if (r >= 0) return 0; // ack -> ready
        // si NACK (r < 0) esperar un poco y reintentar
        sleep_ms(5);
    }
    return -1; // timeout
}

int eeprom_read_data(i2c_inst_t *i2c, uint16_t mem_address, uint8_t *data, size_t len) {
    uint8_t addr_buf[2] = {
        (mem_address >> 8) & 0xFF,
        mem_address & 0xFF
    };
    int ret = i2c_write_blocking(i2c, EEPROM_ADDR, addr_buf, 2, true);
    if (ret < 0) return ret;
    ret = i2c_read_blocking(i2c, EEPROM_ADDR, data, len, false);
    return ret;
} */



#define EEPROM_I2C_ADDR  0x57   // tu AT24C32 / AT24C256
#define EEPROM_PAGE_SIZE 32     // tamaño de página para escritura
#define EEPROM_RETRIES    5     // reintentos por si está ocupada
#define EEPROM_DELAY_MS   5     // tiempo de espera entre reintentos

/* ========================================================
      LECTURA EEPROM  (DEVUELVE 0 = OK, -1 = ERROR)
   ======================================================== */
int eeprom_read_data(i2c_inst_t *i2c, uint16_t mem_addr, uint8_t *data, size_t len)
{
    uint8_t addr_buf[2] = {
        (uint8_t)(mem_addr >> 8), 
        (uint8_t)(mem_addr & 0xFF)
    };
    // Seleccionar dirección interna
    int ret = i2c_write_blocking(i2c, EEPROM_I2C_ADDR, addr_buf, 2, true);
    if (ret < 0)
        return -1;
    // Leer datos
    ret = i2c_read_blocking(i2c, EEPROM_I2C_ADDR, data, len, false);
    if (ret < 0)
        return -1;
    return 0;
}

/* ========================================================
      ESCRITURA EEPROM (DEVUELVE 0 = OK, -1 = ERROR)
      - Respeta límites de página
      - Reintenta si la EEPROM está ocupada
   ======================================================== */
int eeprom_write_data(i2c_inst_t *i2c, uint16_t mem_addr,
                      const uint8_t *data, size_t len)
{
    size_t bytes_written = 0;

    while (bytes_written < len)
    {
        uint16_t addr = mem_addr + bytes_written;
        uint8_t page_offset = addr % EEPROM_PAGE_SIZE;
        size_t page_remaining = EEPROM_PAGE_SIZE - page_offset;
        size_t chunk = (len - bytes_written < page_remaining) ?
                       (len - bytes_written) : page_remaining;

        // Buffer: [MSB][LSB][DATA...]
        uint8_t buf[2 + EEPROM_PAGE_SIZE];
        buf[0] = addr >> 8;
        buf[1] = addr & 0xFF;
        memcpy(&buf[2], &data[bytes_written], chunk);

        bool success = false;

        for (int attempt = 0; attempt < EEPROM_RETRIES; attempt++)
        {
            int ret = i2c_write_blocking(i2c, EEPROM_I2C_ADDR,
                                         buf, 2 + chunk, false);
            if (ret >= 0) {
                success = true;
                break;
            }
            sleep_ms(EEPROM_DELAY_MS);  // EEPROM ocupada → espero
        }

        if (!success)
            return -1;

        bytes_written += chunk;
        // TYPICAL EEPROM WRITE DELAY
        sleep_ms(EEPROM_DELAY_MS);
    }

    return 0;
}