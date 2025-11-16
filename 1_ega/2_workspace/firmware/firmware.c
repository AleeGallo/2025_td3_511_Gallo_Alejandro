#include <stdio.h>
#include <string.h>
#include <math.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "lcd.h"
#include "eeprom.h"
#include "ds3231.h"
#include "mcp4725.h"

/* ----------------- CONSTANTES ---------------- */

// Maximo valor de cuenta para el semaforo
#define MAX_COUNT       10000
#define PIN_LCD_SDA     20      // GP20 (pin 26 físico)
#define PIN_LCD_SCL     21      // GP21 (pin 27 físico)
#define PIN_RTC_SDA     20
#define PIN_RTC_SCL     21  
#define PIN_ENC_CLK     12      // GP12 (pin 16 físico)
#define PIN_ENC_DT      11      // GP11 (pin 15 físico)
#define PIN_ENC_SW      10      // GP10 (pin 14 físico)
#define PIN_BTN_CONFIG  9
#define PIN_LED_MAX     8
#define PIN_LED_MIN     7
#define PIN_ADC0        26
#define PIN_ADC1        27

#define LCD_DIR     0x27
#define RTC_DIR     0x68
#define EEPROM_DIR  0x57
#define DAC_DIR     0x60

// Constantes del sistema
#define SHUNT_RESISTANCE 10.0f  	  // 10 ohms
#define MAX_CURRENT_MA 250.0f    	  // 250 mA máximo
#define CURRENT_RESOLUTION_MA 0.1f 	  // Resolución de 0.1 mA
#define MAX_VOLTAGE_SENSOR 0.12f  	  // 0.12V representa 12V
#define VOLTAGE_SCALE_FACTOR 100.0f 	  // 0.12V -> 12V (x100)
#define VOLTAGE_RESOLUTION_V 0.1f  	  // Resolución de 0.1 V
#define DEBOUNCE_US 7000    // Tiempo antirrebote en microsegundos

// Constantes de configuración
#define MAX_RESISTENCIAS 10
#define MAX_PANTALLAS   (6 + MAX_RESISTENCIAS)
#define MAX_I_mA_Value   250
#define MAX_V_mV_Value   120       // 12.0 V max
#define MAX_tiempo_mS_Value 120    // 2 minutos maximo
#define MAX_Resistencia_Value 9999

#define TIEMPO_REFRESH_LCD_MS  500  // Tiempo de refresco de LCD en MODO ACTIVO

#define UART_ID uart0
#define UART_TX_PIN 16
#define UART_RX_PIN 17
#define UART_BAUDRATE 115200
#define UART_BUFFER_SIZE 128


/* ----------------- VARIABLES Y ESTRUCTURAS ----------------- */

// Variables de antirrebote
volatile uint64_t last_clk_time = 0;
volatile uint64_t last_sw_time = 0;
volatile bool last_clk_state = 1;  // estado anterior de CLK (Encoder)
volatile bool last_sw_state = 1;   // estado anterior de SW (Encoder)
static uint64_t last_config_time = 0;
static bool last_config_state = 1;  // Se asume que el botón está en reposo (pull-up -> alto)

ds3231_rtc_t rtc;       // Variable global del RTC
ds3231_datetime_t dt;   // Datetime global del RTC
mcp4725_t dac;

// Índice y valor de la resistencia actual
// volatile uint8_t indice_R_actual = 0;
// volatile uint32_t R_actual = 0;

typedef struct {
    char textoLCD[4][21];
} lcd_data_t;

typedef struct {
    float Iload_ma;    // Corriente en mA
    float Vin_v;     // Tensión en V
    float Vshunt_v;   // Voltaje en la resistencia shunt
} sensado_data_t;

typedef struct {
    eeprom_data_type_t tipo_dato;    // Setpoint o Alarma
    eeprom_data_id_t id;             // Vmax, Imax, R1-R10, etc.
    ds3231_datetime_t timestamp;     // Fecha y hora del evento
    float valor;                     // Valor del setpoint o de la alarma
} eeprom_data_t;

typedef struct {
    ds3231_datetime_t timestamp;    // Epoch o segundos desde 2000 (4 bytes)
    uint8_t tipo;                   // 1=ALARMA, 0=LECTURA
    sensado_data_t valor;           // Valores sensados
} eeprom_log_t;

typedef struct {
    ds3231_datetime_t timestamp;
    float Vmax;
    float Imax;
    float Vmin;         
    float Imin;
    uint32_t tiempo_ms;
    uint8_t  cantidad_resistencias;
    uint16_t R_setpoints[10];
} setpoint_data_t;

typedef struct {
    sensado_data_t lectura;
    uint32_t resistencia;
} alarma_info_t;

//setpoint_data_t setpoint_global;


typedef struct {
    float Kp, Ki, Kd, Ts;
} pid_params_t;

typedef struct {
    float integral;
    float prev_error;
    float prev_output;
    float d_filt;
} pid_state_t;

/*------------- COLAS Y SEMAFOROS  -------------*/

// Queues de manejo de uart
QueueHandle_t Queue_uart_RX, Queue_uart_TX;

QueueHandle_t Queue_Setpoints, Queue_SetpointActual;
QueueHandle_t Queue_Resistencia;
QueueHandle_t Queue_EEPROM;
QueueHandle_t Queue_EscribirLCD;
QueueHandle_t Queue_Sensado;
QueueHandle_t Queue_DAC;
QueueHandle_t Queue_Alarma, Queue_AlarmaEEPROM;
SemaphoreHandle_t Sem_Bin_Select_Mas, Sem_Bin_Select_Menos, Sem_Bin_OK;
SemaphoreHandle_t Sem_Bin_Config, Sem_Bin_Memory;
SemaphoreHandle_t Sem_Bin_FueraDeRango, Sem_Bin_RangoOK;
SemaphoreHandle_t Sem_Bin_Resistencia, Sem_Bin_AskAlarma, Sem_Bin_AskSetpoint, Sem_Bin_ReadyToRead;
SemaphoreHandle_t Sem_I2C0_Mutex;


/*------------- INTERRUPCIONES  -------------*/

// Interrupcion de giro encoder
void gpio_callback(uint gpio, uint32_t events) {
    uint64_t now = time_us_64();
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    if (gpio == PIN_ENC_CLK) {
        bool clk = gpio_get(PIN_ENC_CLK);
        bool dt  = gpio_get(PIN_ENC_DT);

        // Detectar flanco y hacer antirrebote por tiempo
        if (clk == 1 && last_clk_state == 0 && (now - last_clk_time) > 10000) {
            last_clk_time = now;

            if (dt != clk) {
                xSemaphoreGiveFromISR(Sem_Bin_Select_Mas, &xHigherPriorityTaskWoken);
            } else {
                xSemaphoreGiveFromISR(Sem_Bin_Select_Menos, &xHigherPriorityTaskWoken);
            }
        }

        last_clk_state = clk;
    }

    if (gpio == PIN_ENC_SW) {
        bool sw = gpio_get(PIN_ENC_SW);

        // Detectar flanco de bajada con antirrebote
        if (sw == 0 && last_sw_state == 1 && (now - last_sw_time) > 25000) {
            last_sw_time = now;
            xSemaphoreGiveFromISR(Sem_Bin_OK, &xHigherPriorityTaskWoken);
        }

        last_sw_state = sw;
    }

    if (gpio == PIN_BTN_CONFIG) {
        uint64_t now = time_us_64();
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;

        // Detectar flanco de bajada con antirrebote
        if ((now - last_config_time) > 20000) {
            last_config_time = now;
            xSemaphoreGiveFromISR(Sem_Bin_Config, &xHigherPriorityTaskWoken);
        }

    }

    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

/* ISR de recepción UART */
void ISR_uart_rx() {
    static char uart_rx_buffer[UART_BUFFER_SIZE];
    static uint16_t uart_rx_index = 0;

    while (uart_is_readable(UART_ID)) {
        char c = uart_getc(UART_ID);
        // if (c == '\r') continue;
        if (c == '\r' || c == '\n') {
            if (uart_rx_index > 0) {
                uart_rx_buffer[uart_rx_index] = '\0';
                BaseType_t xHigherPriorityTaskWoken = pdFALSE;
                xQueueSendFromISR(Queue_uart_RX, uart_rx_buffer, &xHigherPriorityTaskWoken);
                uart_rx_index = 0;
                portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
            }
        } else if (uart_rx_index < UART_BUFFER_SIZE - 1) {
            uart_rx_buffer[uart_rx_index++] = c;
        }
    }
}

/*-------------------FUNCIONES------------------*/

/* void mostrar_ultimos_setpoints(void) {
    lcd_data_t lcd_buffer;
    eeprom_data_t dato;
    uint8_t buffer[sizeof(eeprom_data_t)];
    uint16_t addr = EEPROM_ADDR_SETPOINTS;

    for (int i = 0; i < 12; i++) {
        eeprom_read_data(i2c_default, addr, buffer, sizeof(buffer));
        memcpy(&dato, buffer, sizeof(dato));

        // Mostrar en LCD
        snprintf(lcd_buffer.textoLCD[0], 21, "ID: %d Tipo: %d", dato.id, dato.tipo_dato);
        snprintf(lcd_buffer.textoLCD[1], 21, "Valor: %.2f", dato.valor);
        snprintf(lcd_buffer.textoLCD[2], 21, "%02d/%02d/%04d", dato.timestamp.day, dato.timestamp.month, dato.timestamp.year);
        snprintf(lcd_buffer.textoLCD[3], 21, "%02d:%02d:%02d", dato.timestamp.hour, dato.timestamp.minutes, dato.timestamp.seconds);

        xQueueSend(Queue_EscribirLCD, &lcd_buffer, portMAX_DELAY);
        vTaskDelay(pdMS_TO_TICKS(2000));  // 2 segundos por pantalla
        
        addr += sizeof(eeprom_data_t);
    }
} */


bool i2c_safe_read(uint16_t addr, uint8_t *buf, size_t len)
{
    return (eeprom_read_data(i2c_default, addr, buf, len) == 0);
}

bool i2c_safe_write(uint16_t addr, const uint8_t *buf, size_t len)
{
    return (eeprom_write_data(i2c_default, addr, buf, len) == 0);
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    printf("STACK OVERFLOW: %s\n", pcTaskName);

    // Freno el sistema para debug
    taskDISABLE_INTERRUPTS();

    while (1) {
        // Podés parpadear un LED
        gpio_put(PIN_LED_MAX, 1);
        gpio_put(PIN_LED_MIN, 1);
        sleep_ms(200);
        gpio_put(PIN_LED_MAX, 0);
        gpio_put(PIN_LED_MIN, 0);
        sleep_ms(200);
    }
}

void vApplicationMallocFailedHook(void)
{
    printf("ERROR: MALLOC FAILED\n");
    taskDISABLE_INTERRUPTS();
    while(1);
}

void imprimirLog (eeprom_log_t log_data)
{
    printf( "%02d/%02d/%04d %02d:%02d:%02d | "
        "Tipo: %d | "
        "I(ma): %.2f | "
        "Vin: %.2f | "
        "Vshunt: %.4f\n",
        log_data.timestamp.day,
        log_data.timestamp.month,
        log_data.timestamp.year,
        log_data.timestamp.hour,
        log_data.timestamp.minutes,
        log_data.timestamp.seconds,
        log_data.tipo,
        log_data.valor.Iload_ma,
        log_data.valor.Vin_v,
        log_data.valor.Vshunt_v
    );
}

#define EEPROM_RETRIES 3

// Función auxiliar: calcular columna del cursor según parámetro
int columna_cursor(int pantalla_actual, int digit_selected) {
    if (pantalla_actual == 0 || pantalla_actual == 2) return 3 - 2*digit_selected;      // Vmax/Vmin XX.X
    if (pantalla_actual == 1 || pantalla_actual == 3) return 2 - digit_selected;        // Imax/Imin XXX
    if (pantalla_actual == 4) return 2 - digit_selected;                                // Tiempo XXX
    if (pantalla_actual == 5) return 1 - digit_selected;                                // Cantidad XX
    return 3 - digit_selected;                                                          // Resistencias 7 dígitos
}



// Función auxiliar: actualizar valor según giro del encoder
void actualizar_valor(int *valor, int max_val, int digit, bool incrementar) {
    // VALOR sera el puntero a las variables de cada modo de configuracion
    // MAX_VAL es el valor maximo para ese parametro
    // DIGIT es el digito que se aumenta o decrementa
    // INCREMENTAR es booleano
    int factor = 1;
    for(int i = 0; i < digit; i++) factor *= 10;

    if(incrementar) {
        *valor += factor;
        if(*valor > max_val) *valor = max_val;
    } else {
        *valor -= factor;
        if(*valor < 0) *valor = 0;
    }
}

// Funcion para limites MAX y MIN
alarma_flag_t  check_limits (setpoint_data_t *setpoint, sensado_data_t *measurement, sensado_data_t *alarma_measurement) {
    alarma_flag_t alarma = ALARMA_NONE;

    // Set alarmas
    if (measurement->Vin_v > setpoint->Vmax)        alarma |= ALARMA_VMAX; 
    if (measurement->Vin_v < setpoint->Vmin)        alarma |= ALARMA_VMIN;
    if (measurement->Iload_ma > setpoint->Imax)     alarma |= ALARMA_IMAX;
    if (measurement->Iload_ma < setpoint->Imin)     alarma |= ALARMA_IMIN;

    *alarma_measurement=*measurement;
    return alarma;
}



/*------------------- TAREAS ---------------------*/

void task_Control (void *pvParameters) {
    
    //pid_params_t pid = { .Kp = 1.85f, .Ki = 0.90f, .Kd = 0.0f, .Ts = 0.07f };
    pid_params_t pid = { .Kp = 1.85f, .Ki = 3.22f, .Kd = 0.24f, .Ts = 0.1f };
    pid_state_t pid_state = {0};
    setpoint_data_t setpoint;
    sensado_data_t measurement, alarma_measurement;
    alarma_info_t alarma_info;
    //eeprom_data_t alarma_eeprom;
    lcd_data_t buffer_lcd;
    //alarma_flag_t alarma_flags;
    float dac_out, error, error_anterior;

    uint16_t R_actual, ResistenciaSetpoint;

    TickType_t last_lcd_update = 0;
    TickType_t last_res_update = 0;   // Control de tiempo para Sem_Resistencia
    TickType_t last_auxdac_update = 0;
    TickType_t last_alarmaeeprom_update = 0;
    TickType_t alarma_timer = 0;
    
    bool alerta = false;
    bool alarma_activa = false;
    float valorM=0;
    bool inicializado=false;

    while(1) {
        /*  ----------- Ajustes PID --------------
        
        if(xSemaphoreTake(Sem_Bin_Select_Mas, 0) == pdTRUE)
            pid.Kp = pid.Kp + 0.01;
        if(xSemaphoreTake(Sem_Bin_Select_Menos, 0) == pdTRUE)
            pid.Kp = pid.Kp - 0.01; */
/* 
        if(xSemaphoreTake(Sem_Bin_Select_Mas, 0) == pdTRUE)
            pid.Ki = pid.Ki + 0.02;
        if(xSemaphoreTake(Sem_Bin_Select_Menos, 0) == pdTRUE)
            pid.Ki = pid.Ki - 0.02; 
 */

    

        /* Leer resistencia enviada por task_Resistencia (no bloqueante) */
        if (!inicializado){
            xQueueReceive(Queue_Resistencia, &ResistenciaSetpoint, portMAX_DELAY);
            R_actual = ResistenciaSetpoint;
            inicializado = true;
        }

        if (xQueueReceive(Queue_Resistencia, &ResistenciaSetpoint, 0) == pdTRUE) {
            R_actual = ResistenciaSetpoint;   // usa la resistencia recibida en el control
            pid_state.integral = 0.0f; // Resetear integral al cambiar setpoint
            pid_state.prev_error = 0.0f;
        }

        
        if(xQueueReceive(Queue_Sensado, &measurement, 10) == pdTRUE) {
            // Verificar si hay alarma activa (Semáforo dado por task_Alarma)
            if (xSemaphoreTake(Sem_Bin_FueraDeRango, 0) == pdTRUE) {
                alarma_activa = true;
            }

            // Si hay alarma activa -> detener control
            if (alarma_activa) {
                if (xSemaphoreTake(Sem_Bin_RangoOK,0)==pdFALSE){
                    dac_out = 0.0f;
                    xQueueSend(Queue_DAC, &dac_out, 0);
                    snprintf(buffer_lcd.textoLCD[0], 21, "%-20s", "ALARMA-Fuera rango");

                } else alarma_activa = false;
            }
            // Si no hay alarma -> FUNCIONAMIENTO NORMAL
            else{
                // Calcular corriente deseada: I = V_max / R
                float I_target;
                if (R_actual > 0) {
                    I_target = (measurement.Vin_v / R_actual); // I_target en A
                } else {
                    I_target = 0;
                }
                float Vshunt_target = I_target * 10.0f; // Vshunt [V] = I_target [A] * 10ohm
                
                // Control PID - Sobre la tension Vshunt
                error = Vshunt_target - measurement.Vshunt_v;

                 // Derivativo con filtro (suaviza respuesta)
                float derivative = (error - pid_state.prev_error) / pid.Ts;
                const float alpha = 0.1f; // 0.0–1.0, cuanto más chico, más filtrado
                pid_state.d_filt = pid_state.d_filt + alpha * (derivative - pid_state.d_filt);

                // Integración con anti-windup
                float u_unsat = pid.Kp * error + pid.Ki * pid_state.integral + pid.Kd * pid_state.d_filt;

                // Limito señal DAC
                float output = u_unsat;  // se saturará más abajo
                if (output < 0.0f) output = 0.0f;
                //if (output < 0.0f) output = pid_state.prev_output * 0.7f;
                if (output > 5.0f) output = 5.0f;


                // Anti-windup condicional
                bool saturado_arriba = (output >= 5.0f);
                bool saturado_abajo  = (output <= 0.0f);

                // Integro solo si no esta saturado o error lleva hacia adentro
                if (!( (saturado_arriba && error > 0.0f) || (saturado_abajo && error < 0.0f) )) {
                    pid_state.integral += error * pid.Ts;
                }

                // Limitante integral
                if (pid_state.integral > 5.00f) pid_state.integral = 5.00f;
                if (pid_state.integral < -5.00f) pid_state.integral = -5.00f;

                pid_state.prev_error = error;    

                // Enviar valor DAC a cola
                dac_out = output;
                xQueueSend(Queue_DAC, &dac_out, portMAX_DELAY);
                snprintf(buffer_lcd.textoLCD[0], 21, "%-20s", "Medicion en curso");
            }


            // -------- REFRESH LCD  -----------
            TickType_t now = xTaskGetTickCount();
            if (((now - last_lcd_update)  >= pdMS_TO_TICKS(TIEMPO_REFRESH_LCD_MS)) && ((now - last_alarmaeeprom_update)  >= pdMS_TO_TICKS(1000))) {
                lcd_show_cursor(false,false);
                //snprintf(buffer_lcd.textoLCD[0], 21, "%-20s", "Medicion en curso");
                //snprintf(buffer_lcd.textoLCD[1], 21, "R: %-4d OHM I:%1.2f", R_actual, pid.Ki);
                snprintf(buffer_lcd.textoLCD[1], 21, "R: %-4d OHM   ", R_actual);
                snprintf(buffer_lcd.textoLCD[2], 21, "V entrada: %-5.2f V", measurement.Vin_v);
                snprintf(buffer_lcd.textoLCD[3], 21, "Corriente: %-4d mA", (int)measurement.Iload_ma);
                //lcd_clear();
                xQueueSend(Queue_EscribirLCD, &buffer_lcd, 0);
                last_lcd_update = now;
            }

            // Recibe pedido para enviar info a TaskAlarma
            if (xSemaphoreTake(Sem_Bin_AskAlarma, 0)==pdTRUE){
                alarma_info.lectura = measurement;
                alarma_info.resistencia = R_actual;
                xQueueSend(Queue_Alarma, &alarma_info,0);
            }

            // Frecuencia de calculo -> 1ms -> 1KHz
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

void task_Sensado(void *pvParameters) {
    sensado_data_t measurement;
    const uint8_t num_samples = 10;
    uint32_t voltage_in_sum;
    uint32_t voltage_shunt_sum;
    float voltage_shunt_adc, voltage_shunt, voltage_sensor_adc, voltage_sensor;
    
    while(1) {
        if (xSemaphoreTake(Sem_Bin_ReadyToRead,portMAX_DELAY)==pdTRUE){
            voltage_shunt_sum = 0;
            voltage_in_sum = 0;
            
            // Da semaforo apenas lo toma
            xSemaphoreGive (Sem_Bin_ReadyToRead);
            // Toma múltiples muestras para promediar
            for(uint8_t i = 0; i < num_samples; i++) {
                // Lee corriente (pin 31 - ADC0)
                adc_select_input(0); // ADC0
                voltage_shunt_sum += adc_read();
                
                // Lee tensión (pin 32 - ADC1)
                adc_select_input(1); // ADC1
                voltage_in_sum += adc_read();
                
                vTaskDelay(pdMS_TO_TICKS(1)); // Pequeño delay entre lecturas
            }
            
            // Promedia las lecturas
            uint16_t voltage_shunt_raw = voltage_shunt_sum / num_samples;
            uint16_t voltage_raw = voltage_in_sum / num_samples;
            
            // --- CONVERSION ---
            
            // VSHUNT Y Icarga
            voltage_shunt_adc = (voltage_shunt_raw  * 3.3f) / 4095.0f; // Ajuste por tensiones en ADC 
            voltage_shunt = voltage_shunt_adc * (2.5f / 3.3f);
            measurement.Vshunt_v = voltage_shunt;
            measurement.Iload_ma = (voltage_shunt / SHUNT_RESISTANCE) * 1000.0f; // A -> mA

            // VIN
            voltage_sensor_adc = (voltage_raw * 3.3f) / 4095.0f;
            voltage_sensor = voltage_sensor_adc * (12.0f / 3.3f); 
            measurement.Vin_v = voltage_sensor;
            
            // Envia por la cola
            xQueueSend(Queue_Sensado, &measurement, 200);
            
            // Frecuencia de muestro -> 10ms -> 100Hz
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

void task_DAC(void *pvParameters) {
    float dac_in;
    while(1) {
        if (xQueueReceive(Queue_DAC, &dac_in, portMAX_DELAY) == pdTRUE) {
            xSemaphoreTake(Sem_I2C0_Mutex, portMAX_DELAY);
            mcp4725_set_voltage(&dac, dac_in, MCP4725_RegisterMode, MCP4725_PowerDown_OFF);
            xSemaphoreGive(Sem_I2C0_Mutex);
        }
    }
}

void task_Alarma(void *pvParameters) {
    alarma_info_t info_check_alarma;
    setpoint_data_t setpoint;
    eeprom_log_t eepromValor;
    bool flag_fuera_rango = false;
    float Iload_ideal;

    while(1) {
        // Tomo semaforos para que no se bloquee
        xSemaphoreTake(Sem_Bin_FueraDeRango,0);
        xSemaphoreTake(Sem_Bin_RangoOK,0);

        // Envio semaforo para tomar muestras
        xSemaphoreGive (Sem_Bin_AskAlarma);
        vTaskDelay(pdMS_TO_TICKS(100));

        if (xQueueReceive(Queue_Alarma, &info_check_alarma, portMAX_DELAY) == pdTRUE) {
            xQueuePeek(Queue_SetpointActual, &setpoint, portMAX_DELAY);
            // ------------- ALARMAS - Si hay alerta guarda en EEPROM --------------

            // ENVIAR COLA PARA task_Alarma

            bool fuera_rango = false;
            bool over_max = false;
            bool under_min = false;

            Iload_ideal = (info_check_alarma.lectura.Vin_v / info_check_alarma.resistencia) * 1000.0f;

            // --- Verificación de límites ---
            if (info_check_alarma.lectura.Vin_v > setpoint.Vmax || 
                Iload_ideal > setpoint.Imax) {
                fuera_rango = true;
                over_max = true;
            } 
            else if (info_check_alarma.lectura.Vin_v < setpoint.Vmin || 
                Iload_ideal < setpoint.Imin) {
                fuera_rango = true;
                under_min = true;
            }

            // --- LEDs de alarma ---
            gpio_put(PIN_LED_MAX, over_max);
            gpio_put(PIN_LED_MIN, under_min);
            
            // Mando log si esta fuera de rango y SEMAFORO para parar la carga
            if (fuera_rango){
                xSemaphoreGive(Sem_Bin_FueraDeRango);
                flag_fuera_rango = true;
                eepromValor.tipo = 1;
            }
            else {
                if (flag_fuera_rango){
                    eepromValor.tipo = 0;
                    xSemaphoreGive(Sem_Bin_RangoOK);
                }
            }

            if (xSemaphoreTake(Sem_I2C0_Mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
                ds3231_get_datetime(&dt, &rtc);
                eepromValor.timestamp  = dt;
                xSemaphoreGive(Sem_I2C0_Mutex);
            }
            eepromValor.valor = info_check_alarma.lectura;
            if (xQueueOverwrite (Queue_EEPROM, &eepromValor)==pdFALSE){
                printf("ERROR EN ESCRIBIR MEMORIA");
            }
            imprimirLog (eepromValor);
            vTaskDelay (500);  // Delay de 500 ms
        }
    }
}


void task_Config(void *params) {
    int pantalla_actual = 0;
    int total_parametros = 6;   // Parametros de seteo
    int NUM_RESISTENCIAS = 10;  // Inicializo condicion con 10 resistencias
    int TOTAL_PANTALLAS = 16;   // Inicializo condicion con 10 resistencias
    int numPantallasUsuario;
    int digit_selected = 0;
    int last_digit_selected = -1;
    bool pressed = false;
    bool cursor_visible = false;
    TickType_t last_cursor_time = 0;
    // Variables de cada parametro
    int V_max_mV=0, I_max_mA=0, V_min_mV=0, I_min_mA=0, tiempo_seg=0, cantResistencias=0;
    int valores_res[10]={0};
    // Punteros a cada parámetro (pantalla_actual) 
    int* ptr_valores[MAX_PANTALLAS];

    // Limites máximos
    int maximos[MAX_PANTALLAS];
    setpoint_data_t setpoint_new;

    int num_digitos_resistencia = 4; 
    const int potencias[7] = {1,10,100,1000,10000,100000,1000000};

    lcd_data_t lcd_buffer;
    //eeprom_data_t dato_eeprom;
    //dato_eeprom.tipo_dato = EEPROM_DATA_SETPOINT;

    // Inicialización de valores de parametros
    ptr_valores[0] = &V_max_mV;     maximos[0] = MAX_V_mV_Value;
    ptr_valores[1] = &I_max_mA;     maximos[1] = MAX_I_mA_Value;
    ptr_valores[2] = &V_min_mV;     maximos[2] = MAX_V_mV_Value;
    ptr_valores[3] = &I_min_mA;     maximos[3] = MAX_I_mA_Value;
    ptr_valores[4] = &tiempo_seg;   maximos[4] = MAX_tiempo_mS_Value;
    ptr_valores[5] = &cantResistencias;   maximos[5] = MAX_RESISTENCIAS;
    for(int i=0; i < MAX_RESISTENCIAS; i++){
        ptr_valores[6 + i] = &valores_res[i];   maximos[6 + i] = MAX_Resistencia_Value;
    }

    while(1) {

        // Tomo semaforo MODO CONFIGURACION
        if (xSemaphoreTake(Sem_Bin_Config, portMAX_DELAY) == pdTRUE){
            lcd_clear();
            // Pausar MODO ACTIVO
            xSemaphoreTake(Sem_Bin_ReadyToRead, 0);

            while(pantalla_actual < TOTAL_PANTALLAS){
                char linea[4][21];

                // MUESTRA CONFIGURACION DEL PARAMETRO
                if(pantalla_actual < total_parametros){

                        // 🔵 Encender LED según el parámetro que estoy editando
                    if (pantalla_actual == 0 || pantalla_actual == 1) {
                        gpio_put(PIN_LED_MAX, 1);
                    }
                    else if (pantalla_actual == 2 || pantalla_actual == 3) {
                        gpio_put(PIN_LED_MIN, 1);
                    }

                    // Linea 0
                    switch(pantalla_actual){
                        case 0: snprintf(linea[0], 21, "CONFIG V MAX"); break;
                        case 1: snprintf(linea[0], 21, "CONFIG I MAX"); break;
                        case 2: snprintf(linea[0], 21, "CONFIG V MIN"); break;
                        case 3: snprintf(linea[0], 21, "CONFIG I MIN"); break;
                        case 4: snprintf(linea[0], 21, "CONFIG TIEMPO"); break;
                        case 5: snprintf(linea[0], 21, "CONFIG CANTIDAD R"); break;
                    }
                    // Linea 1
                    if(pantalla_actual==0 || pantalla_actual==2)
                        snprintf(linea[1], 21, "%2d.%1d V", *ptr_valores[pantalla_actual]/10, *ptr_valores[pantalla_actual]%10);
                    else if(pantalla_actual==1 || pantalla_actual==3)
                        snprintf(linea[1], 21, "%03d mA", *ptr_valores[pantalla_actual]);
                    else if(pantalla_actual==4)
                        snprintf(linea[1], 21, "%03d seg", *ptr_valores[pantalla_actual]);
                    else if(pantalla_actual==5)
                        snprintf(linea[1], 21, "%02d valores", *ptr_valores[pantalla_actual]);
                } 
                else {
                    int res_idx = pantalla_actual - 6;
                    snprintf(linea[0], 21, "CONFIG R%2d", res_idx + 1);
                    snprintf(linea[1], 21, "%04d OHM", *ptr_valores[pantalla_actual]);
                }
                // Linea 2 y 3
                linea[2][0] = '\0';
                linea[3][0] = '\0';

                for(int i=0; i<4; i++)     snprintf(lcd_buffer.textoLCD[i], 21, "%-20s", linea[i]);
                // Envío a imprimir el parametro actual
                xQueueSend(Queue_EscribirLCD, &lcd_buffer, portMAX_DELAY);

                // volver a ubicar cursor si corresponde
                if(cursor_visible){
                    int col = columna_cursor(pantalla_actual, digit_selected);
                    lcd_set_cursor(1, col);
                    lcd_show_cursor(true, true);
                }

                // -- CURSOR --
                if(cursor_visible && digit_selected != last_digit_selected){
                    int col = columna_cursor(pantalla_actual, digit_selected);
                    lcd_set_cursor(1, col);
                    last_cursor_time = xTaskGetTickCount();
                    lcd_show_cursor(true,true);   // solo ON si estaba en modo visible
                    last_digit_selected = digit_selected;
                }

                // -- GIRO DE ENCODER --
                // Actualiza el valor del parametro actual
                if(xSemaphoreTake(Sem_Bin_Select_Mas, 0) == pdTRUE)
                    actualizar_valor(ptr_valores[pantalla_actual], maximos[pantalla_actual], digit_selected, true);
                if(xSemaphoreTake(Sem_Bin_Select_Menos, 0) == pdTRUE)
                    actualizar_valor(ptr_valores[pantalla_actual], maximos[pantalla_actual], digit_selected, false);
                

                // -- BOTON DE ENCODER -- 
                if(gpio_get(PIN_ENC_SW) == 0 && !pressed){
                    pressed = true;
                    last_cursor_time = xTaskGetTickCount();
                }
                if(gpio_get(PIN_ENC_SW) == 1 && pressed){
                    pressed = false;
                    TickType_t elapsed = xTaskGetTickCount() - last_cursor_time;
                    
                    // CONFIRMA VALOR de parametro y pasa al siguiente
                    if(elapsed >= pdMS_TO_TICKS(1000)){

                        // ------ CONFIRMA EN PANTALLA ------
                        snprintf(linea[3], 21, "VALOR CONFIRMADO");
                        for (int i = 0; i < 4; i++)
                            snprintf(lcd_buffer.textoLCD[i], 21, "%-20s", linea[i]);
                        xQueueSend(Queue_EscribirLCD, &lcd_buffer, portMAX_DELAY);

                        vTaskDelay(pdMS_TO_TICKS(1000));  // pequeño delay visible

                        if (pantalla_actual==5) {
                            NUM_RESISTENCIAS = cantResistencias;
                            TOTAL_PANTALLAS = total_parametros + cantResistencias;
                        }
                            //xQueueSend(Queue_EEPROM, &dato_eeprom, portMAX_DELAY);

                        // Apaga LEDs
                        gpio_put(PIN_LED_MAX, 0);
                        gpio_put(PIN_LED_MIN, 0);

                        pantalla_actual++;
                        digit_selected = 0;
                        last_digit_selected = -1;
                    } 

                    // CAMBIA DE DIGITO
                    else if(elapsed >= pdMS_TO_TICKS(100)){
                        // Paso entre dígitos
                        if ((pantalla_actual==0)||(pantalla_actual==2)) digit_selected=(digit_selected+1)%2;
                        else if ((pantalla_actual==1)||(pantalla_actual==3)) digit_selected=(digit_selected+1)%3;
                        else if (pantalla_actual==4) digit_selected=(digit_selected+1)%3;
                        else if (pantalla_actual==5) digit_selected=(digit_selected+1)%2;
                        else digit_selected=(digit_selected+1)% num_digitos_resistencia;

                        lcd_show_cursor(true, true);
                        cursor_visible = true;
                        last_digit_selected = -1; // forzar reubicación del cursor
                    }
                }

                vTaskDelay(pdMS_TO_TICKS(50));
            }
            
            // Guarda setpoints en SETPOINT y los manda por Cola a EEPROM
            if (xSemaphoreTake(Sem_I2C0_Mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                ds3231_get_datetime(&setpoint_new.timestamp, &rtc);
                xSemaphoreGive(Sem_I2C0_Mutex);
            }
            setpoint_new.Vmax = V_max_mV / 10.0f;
            setpoint_new.Imax = (float)I_max_mA;
            setpoint_new.Vmin = V_min_mV / 10.0f;
            setpoint_new.Imin = (float)I_min_mA;
            setpoint_new.tiempo_ms = tiempo_seg * 1000;
            setpoint_new.cantidad_resistencias = (uint8_t)cantResistencias;
            for(int i=0;i<cantResistencias;i++){
                setpoint_new.R_setpoints[i] = valores_res[i];
            }

            // Envio SETPOINT a EEPROM
            if (xQueueSend(Queue_Setpoints, &setpoint_new, pdMS_TO_TICKS(500)) != pdTRUE) {
                // fallo envío: opcional: log por UART o reintento
                snprintf(lcd_buffer.textoLCD[0],21,"ERROR DE ENVIO");
                for(int i=1;i<4;i++)    lcd_buffer.textoLCD[i][0]='\0';
                xQueueSend(Queue_EscribirLCD,&lcd_buffer,portMAX_DELAY);
                vTaskDelay(pdMS_TO_TICKS(2000));
            }

            // Mensaje de CONFIG GUARDADA
            lcd_clear();
            snprintf(lcd_buffer.textoLCD[0],21,"CONFIG GUARDADA");
            for(int i=1;i<4;i++)    lcd_buffer.textoLCD[i][0]='\0';
            xQueueSend(Queue_EscribirLCD,&lcd_buffer,portMAX_DELAY);
            vTaskDelay(pdMS_TO_TICKS(2000));
            
            // Reinicio valores
            NUM_RESISTENCIAS = 10;
            TOTAL_PANTALLAS = 16; 
            pantalla_actual=0;
            lcd_show_cursor(false,false);

            // Toma semaforo Config por si se apreto el boton
            xSemaphoreTake(Sem_Bin_Config, 0);
            // Sale de MODO CONFIGURACION
            xSemaphoreGive(Sem_Bin_ReadyToRead);
        }
    }
}


void task_EEPROM(void *params) {
    eeprom_data_t dato;
    eeprom_log_t log_data;
    setpoint_data_t setpoint_nuevo, setpoint_anterior, setpoint_leido;
    lcd_data_t lcd_text;
    char linea[4][21];

    // Direcciones fijas
    static const uint16_t addr_slot0 = EEPROM_SETPOINT_SLOT0_ADDR;
    static const uint16_t addr_slot1 = EEPROM_SETPOINT_SLOT1_ADDR;
    static uint16_t addr_logs        = 0x0400;
    static uint16_t addr_setpoints = EEPROM_ADDR_SETPOINTS;
    static uint16_t addr_alarmas   = EEPROM_ADDR_ALARMAS;
    static uint16_t addr_lecturas  = EEPROM_ADDR_LECTURAS;
    int ret;

    bool inicializado=false;

    //uint8_t buffer_setpoint_anterior[sizeof(setpoint_data_t)];   
    uint8_t buffer_setpoint[sizeof(setpoint_data_t)];
    uint8_t buffer_log[sizeof(eeprom_log_t)];

    // -------------------- LOG CIRCULAR --------------------
    uint16_t write_ptr = 0;
    uint16_t log_count = 0;

    // Leer punteros persistentes al iniciar
    if (xSemaphoreTake(Sem_I2C0_Mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        eeprom_read_data(i2c_default, EEPROM_LOG_WRITE_PTR,  (uint8_t*)&write_ptr, 2);
        eeprom_read_data(i2c_default, EEPROM_LOG_COUNT,      (uint8_t*)&log_count, 2);
        xSemaphoreGive(Sem_I2C0_Mutex);
    }

    // Si EEPROM recién iniciada, corregir valores inválidos
    if (write_ptr >= EEPROM_LOG_MAX) write_ptr = 0;
    if (log_count > EEPROM_LOG_MAX) log_count = 0;

    while (1) {
        //inicializado=true;
        if (!inicializado){
            if (xSemaphoreTake(Sem_I2C0_Mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
                eeprom_read_data(i2c_default, EEPROM_SETPOINT_SLOT0_ADDR, buffer_setpoint, sizeof(setpoint_data_t));
                xSemaphoreGive(Sem_I2C0_Mutex);
            }
            memcpy(&setpoint_nuevo, buffer_setpoint, sizeof(setpoint_data_t));
            uint8_t *p = (uint8_t*)&setpoint_nuevo;
            bool all_ff = true;
            bool all_00 = true;
            for (size_t i=0; i < sizeof(setpoint_data_t); ++i){
                if (p[i] != 0xFF) all_ff = false;
                if (p[i] != 0x00) all_00 = false;
                if (!all_ff && !all_00) break;
            }

            inicializado = true;
            if (all_ff || all_00) {
                xSemaphoreGive(Sem_Bin_Config);  // EEPROM vacía → modo configuración
            } else {
                //xQueueSend(Queue_Setpoints, &setpoint_nuevo, portMAX_DELAY);
                xQueueOverwrite(Queue_SetpointActual, &setpoint_nuevo);
                xSemaphoreGive(Sem_Bin_ReadyToRead); // EEPROM lista → modo normal
            }
        }


       if (xQueueReceive(Queue_Setpoints, &setpoint_nuevo, pdMS_TO_TICKS(50)) == pdTRUE) {
            
            bool ok = true; // bandera de éxito

            /* snprintf(lcd_text.textoLCD[0], 21, "PREVIO A GUARDAR");
            for (int i = 1; i < 4; i++) lcd_text.textoLCD[i][0] = '\0';
            xQueueSend(Queue_EscribirLCD, &lcd_text, portMAX_DELAY);
            vTaskDelay(pdMS_TO_TICKS(1000)); */

            if (xSemaphoreTake(Sem_I2C0_Mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
                eeprom_read_data(i2c_default, addr_slot0, buffer_setpoint, sizeof(setpoint_data_t));
                vTaskDelay(pdMS_TO_TICKS(10));
                eeprom_write_data(i2c_default, addr_slot1, buffer_setpoint, sizeof(setpoint_data_t));
                xSemaphoreGive(Sem_I2C0_Mutex);
                vTaskDelay(pdMS_TO_TICKS(10));
                memcpy(buffer_setpoint, &setpoint_nuevo, sizeof(setpoint_data_t));
                eeprom_write_data(i2c_default, addr_slot0, buffer_setpoint, sizeof(setpoint_data_t));
                vTaskDelay(pdMS_TO_TICKS(10));
                // Lectura para validacion
                eeprom_read_data(i2c_default, addr_slot0, buffer_setpoint, sizeof(setpoint_data_t));
                xSemaphoreGive(Sem_I2C0_Mutex);
                ok = true;
            } 
            else {
                ok = false; // No pudo tomar el bus
            }
            

             // --- Mensaje en pantalla ---
           /*  if (ok) {
                snprintf(linea[0], 21, "GUARDADO OK");
            } else {
                snprintf(linea[0], 21, "ERROR AL GUARDAR");
            }
            linea[2][0] = '\0';
            linea[3][0] = '\0';
            for(int i=0; i<4; i++)     snprintf(lcd_text.textoLCD[i], 21, "%-20s", linea[i]);
            xQueueSend(Queue_EscribirLCD, &lcd_text, portMAX_DELAY);
            vTaskDelay(pdMS_TO_TICKS(1000)); */
            
            

            //memcpy(&setpoint_leido, buffer_setpoint, sizeof(setpoint_data_t));
            /* lcd_clear();
            snprintf(lcd_text.textoLCD[0], 21, "Cantidad: %d", setpoint_leido.cantidad_resistencias);
            snprintf(lcd_text.textoLCD[1], 21, "R1: %d", setpoint_leido.R_setpoints[0]);
            snprintf(lcd_text.textoLCD[2], 21, "R2: %d", setpoint_leido.R_setpoints[1]);
            snprintf(lcd_text.textoLCD[3], 21, "R3: %d", setpoint_leido.R_setpoints[2]);
            xQueueSend(Queue_EscribirLCD, &lcd_text, portMAX_DELAY);
            vTaskDelay(pdMS_TO_TICKS(2000)); */

            memcpy(&setpoint_leido, buffer_setpoint , sizeof(setpoint_data_t));
            xQueueOverwrite(Queue_SetpointActual, &setpoint_leido);
            
        } 

            // ---------- GUARDAR LOG EN MODO CIRCULAR ----------
        if (xQueueReceive(Queue_EEPROM, &log_data, pdMS_TO_TICKS(50)) == pdTRUE)
        {
            memcpy(buffer_log, &log_data, sizeof(eeprom_log_t));
            uint16_t addr_log = EEPROM_LOG_START + (write_ptr * EEPROM_LOG_SIZE);

            if (xSemaphoreTake(Sem_I2C0_Mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
                // escribir log
                if (!i2c_safe_write(addr_log, buffer_log, EEPROM_LOG_SIZE)) {
                    printf("EEPROM: fallo escritura log en addr 0x%04X\n", addr_log);
                } else {
                    // avanzar puntero circular en RAM
                    write_ptr++;
                    if (write_ptr >= EEPROM_LOG_MAX) write_ptr = 0;
                    if (log_count < EEPROM_LOG_MAX) log_count++;

                    // escribir punteros persistentes (WRITE_PTR y COUNT) en EEPROM
                    if (!i2c_safe_write(EEPROM_LOG_WRITE_PTR, (uint8_t*)&write_ptr, sizeof(write_ptr))) {
                        printf("EEPROM: fallo escritura WRITE_PTR\n");
                    }
                    if (!i2c_safe_write(EEPROM_LOG_COUNT, (uint8_t*)&log_count, sizeof(log_count))) {
                        printf("EEPROM: fallo escritura LOG_COUNT\n");
                    }
                }
                xSemaphoreGive(Sem_I2C0_Mutex);
            } else {
                printf("EEPROM: no pudo tomar mutex para escribir log\n");
            }

            // Muestra por UART / LCD el log que acabamos de guardar (local)
            printf("WritePtr: %d | Log_Count: %d\n", write_ptr, log_count);
            imprimirLog(log_data);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void task_LCD (void *params) {
    lcd_data_t data_print_LCD;
    lcd_data_t prev_data = {0}; // Inicializo vacío
    while (1) {
        // Recibo colas con estructura
        if (xQueueReceive(Queue_EscribirLCD, &data_print_LCD, portMAX_DELAY) == pdTRUE) {
             
            // Toma el mutex antes de escribir en I2C (LCD)
            if (xSemaphoreTake(Sem_I2C0_Mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
                for (int i = 0; i < 4; i++) {
                    if (strncmp(prev_data.textoLCD[i], data_print_LCD.textoLCD[i], 21) != 0) {
                        lcd_set_cursor(i, 0);
                        lcd_string(data_print_LCD.textoLCD[i]);
                        strncpy(prev_data.textoLCD[i], data_print_LCD.textoLCD[i], 21);
                    }
                }
                xSemaphoreGive(Sem_I2C0_Mutex);  // Liberar el mutex
            }else{ 
                printf("ERROR: deadlock I2C\n");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20)); // pequeña pausa para evitar saturar
    }
}

void task_Resistencia(void *pvParameters) {
    TickType_t xLastWakeTime;
    xLastWakeTime = xTaskGetTickCount();
    setpoint_data_t setpoint_recibido;
    uint16_t ResistenciaSetpoint;
    uint8_t indice_R_actual = 0;

    while(1) {
        if (xQueuePeek(Queue_SetpointActual, &setpoint_recibido, portMAX_DELAY) == pdTRUE) {
            // Obtener resistencia actual del Setpoint
            ResistenciaSetpoint = setpoint_recibido.R_setpoints[indice_R_actual];
            // Avanzar al siguiente índice
            indice_R_actual++;
            if (indice_R_actual == setpoint_recibido.cantidad_resistencias) {
                indice_R_actual = 0; // volver a R1
            }
            xQueueSend(Queue_Resistencia, &ResistenciaSetpoint, pdMS_TO_TICKS(500));
                 
            // vTaskDelay(1000);
            vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(setpoint_recibido.tiempo_ms));
        }
    }
}

/* Tarea RX de la UART */
void task_UART_RX(void *pvParams) {
    // BUFFER
    char rx_buffer[UART_BUFFER_SIZE];

    while(1) {
        if (xQueueReceive(Queue_uart_RX, rx_buffer, portMAX_DELAY) == pdTRUE) {
            // COMANDO SET (ESCRIBIR DATOS)
            if (strncmp(rx_buffer, "set", 3) == 0)
                uart_cmd_set(rx_buffer);
            // COMANDO GET (TRAER DATOS)
            else if (strncmp(rx_buffer, "get", 3) == 0)
                uart_cmd_get(rx_buffer);
            else printf("[UART] Comando desconocido: %s\n", rx_buffer);
        }
    }
}

/* Tarea TX de la UART */
void task_UART_TX(void *pvParams) {
    // BUFFER
    char tx_buffer[UART_BUFFER_SIZE];

    while(1) {
        // Espera un mensaje en la cola para enviar
        if (xQueueReceive(Queue_uart_TX, tx_buffer, portMAX_DELAY) == pdTRUE) {
            // Aseguro terminacion de linea
            tx_buffer[UART_BUFFER_SIZE - 1] = '\0';
            // Mando string a la uart
            uart_puts(UART_ID, tx_buffer);
        }
    }
}

void task_Init(void *params) {

    // Inicialización ENCODER
    // CLK
    gpio_init(PIN_ENC_CLK);
    gpio_set_dir(PIN_ENC_CLK, GPIO_IN);
    gpio_pull_up(PIN_ENC_CLK);
    // DT
    gpio_init(PIN_ENC_DT);
    gpio_set_dir(PIN_ENC_DT, GPIO_IN);
    gpio_pull_up(PIN_ENC_DT);
    // SW
    gpio_init(PIN_ENC_SW);
    gpio_set_dir(PIN_ENC_SW, GPIO_IN);
    gpio_pull_up(PIN_ENC_SW);

    // Inicialización LEDs
    gpio_init(PIN_LED_MAX);
    gpio_set_dir(PIN_LED_MAX, GPIO_OUT);
    gpio_put(PIN_LED_MAX, 0);  // Apagar al inicio

    gpio_init(PIN_LED_MIN);
    gpio_set_dir(PIN_LED_MIN, GPIO_OUT);
    gpio_put(PIN_LED_MIN, 0);  // Apagar al inicio

    // Inicialización BOTON
    gpio_init(PIN_BTN_CONFIG);
    gpio_set_dir(PIN_BTN_CONFIG, GPIO_IN);
    gpio_pull_down(PIN_BTN_CONFIG);

    // IRQs
    gpio_set_irq_enabled_with_callback(PIN_ENC_CLK, GPIO_IRQ_EDGE_RISE, true, &gpio_callback);
    gpio_set_irq_enabled(PIN_ENC_SW, GPIO_IRQ_EDGE_FALL, true);
    gpio_set_irq_enabled(PIN_BTN_CONFIG, GPIO_IRQ_EDGE_RISE, true);

    // Inicialización I2C a 100KHz
    i2c_init(i2c0, 100000);

    // Inicialización ADC
    adc_init();
    adc_gpio_init(26);  // Configura GPIO26 como entrada analógica
    adc_select_input(0);  // Selecciona canal 0 (GPIO26)
    adc_gpio_init(27);  // Configura GPIO27 como entrada analógica
    adc_select_input(1);  // Selecciona canal 1 (GPIO27)


    //pwm_init_channel(0);

    // Inicialización LCD
    gpio_set_function(PIN_LCD_SDA, GPIO_FUNC_I2C);
    gpio_set_function(PIN_LCD_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(PIN_LCD_SDA);
    gpio_pull_up(PIN_LCD_SCL);
    // Inicializa el LCD con el I2C0 y la direccion de 7 bits 0x27
    lcd_init(i2c0, LCD_DIR);

    // Inicialización DAC
    //dac.i2c_port = i2c0;
    //dac.i2c_addr = DAC_DIR;

    mcp4725_begin(&dac, DAC_DIR, i2c0, 100, PIN_LCD_SDA, PIN_LCD_SCL, 1000);

    mcp4725_set_reference_voltage(&dac, 5);
    mcp4725_set_voltage(&dac, 5, MCP4725_RegisterMode, MCP4725_PowerDown_OFF);
    
    // Inicialización RTC */
    //ds3231_init(i2c0, PIN_RTC_SDA, PIN_RTC_SCL, &rtc);
    rtc.i2c_port = i2c0;
    rtc.i2c_addr = DS3231_I2C_ADDRESS;
    ds3231_get_datetime(&dt, &rtc);

    if (dt.year <= 2002){
        dt.seconds = 0;
        dt.minutes = 0;
        dt.hour = 21;
        dt.day = 12;
        dt.month = 8;
        dt.year = 2025;
        dt.dotw = 1;
        ds3231_set_datetime(&dt, &rtc);
    }

    
    // Creación de colas y semaforos
    Queue_EscribirLCD   = xQueueCreate(5, sizeof(lcd_data_t));
    Queue_Setpoints     = xQueueCreate(1, sizeof(setpoint_data_t));
    Queue_SetpointActual    = xQueueCreate(1, sizeof(setpoint_data_t));
    Queue_Alarma        = xQueueCreate (1, sizeof(alarma_info_t));
    //Queue_AlarmaEEPROM  = xQueueCreate (1, sizeof(eeprom_log_t));
    Queue_Resistencia   = xQueueCreate(1, sizeof(uint16_t));
    Queue_EEPROM        = xQueueCreate(1, sizeof(eeprom_log_t));
    Queue_Sensado       = xQueueCreate(8, sizeof(sensado_data_t));
    Queue_DAC           = xQueueCreate(5, sizeof(float));
    Queue_uart_RX       = xQueueCreate(4, UART_BUFFER_SIZE);
    Queue_uart_TX        = xQueueCreate(8, UART_BUFFER_SIZE);
    Sem_Bin_Select_Mas   = xSemaphoreCreateBinary();
    Sem_Bin_Select_Menos = xSemaphoreCreateBinary();
    Sem_Bin_OK           = xSemaphoreCreateBinary();
    Sem_Bin_Config       = xSemaphoreCreateBinary();
    Sem_Bin_FueraDeRango = xSemaphoreCreateBinary();
    Sem_Bin_RangoOK      = xSemaphoreCreateBinary();
    Sem_Bin_ReadyToRead  = xSemaphoreCreateBinary();
    Sem_Bin_Resistencia  = xSemaphoreCreateBinary();
    Sem_Bin_AskAlarma    = xSemaphoreCreateBinary();
    Sem_I2C0_Mutex       = xSemaphoreCreateMutex();
    //Sem_Config_Mutex     = xSemaphoreCreateMutex();

    // Setpoint de ejemplo
  /*   setpoint_data_t setpoint_global;
    setpoint_global.Vmax = 11;
    setpoint_global.Imax = 240;
    setpoint_global.Vmin = 1;
    setpoint_global.Imin = 3;
    setpoint_global.tiempo_ms = 3000;
    setpoint_global.cantidad_resistencias = 10;
    setpoint_global.R_setpoints[0] = 50;
    setpoint_global.R_setpoints[1] = 100;
    setpoint_global.R_setpoints[2] = 150;
    setpoint_global.R_setpoints[3] = 250;
    setpoint_global.R_setpoints[4] = 300;
    setpoint_global.R_setpoints[5] = 450;
    setpoint_global.R_setpoints[6] = 550;
    setpoint_global.R_setpoints[7] = 600;
    setpoint_global.R_setpoints[8] = 800;
    setpoint_global.R_setpoints[9] = 950; */

    // xQueueSend (Queue_Setpoints, &setpoint_global, portMAX_DELAY);
    /* if (xSemaphoreTake(Sem_I2C0_Mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        eeprom_format(i2c0);
        xSemaphoreGive(Sem_I2C0_Mutex);
    } */

    // Ingresa en modo Config
    //xSemaphoreGive(Sem_Bin_Config);
    //xSemaphoreGive(Sem_Bin_ReadyToRead);

    // Elimino la tarea para liberar recursos
    vTaskDelete(NULL);
}


int main()
{
    stdio_init_all();

    // Creacion de tareas
    xTaskCreate(task_Init, "Init", configMINIMAL_STACK_SIZE, NULL, 5, NULL);
    xTaskCreate(task_Resistencia, "Resistencias", 2*configMINIMAL_STACK_SIZE, NULL, 4, NULL);
    xTaskCreate(task_LCD, "LCD", configMINIMAL_STACK_SIZE, NULL, 3, NULL);
    xTaskCreate(task_EEPROM, "Eeprom", 3*configMINIMAL_STACK_SIZE, NULL, 3, NULL);
    xTaskCreate(task_Config, "Config", 2*configMINIMAL_STACK_SIZE, NULL, 2, NULL);
    xTaskCreate(task_Alarma, "Alarma" , 3*configMINIMAL_STACK_SIZE, NULL, 1, NULL);
    xTaskCreate(task_Sensado, "Sensado", configMINIMAL_STACK_SIZE, NULL, 1, NULL);
    xTaskCreate(task_Control, "Control", 512, NULL, 1, NULL);
    xTaskCreate(task_DAC, "DAC", configMINIMAL_STACK_SIZE, NULL, 1, NULL);

    /* xTaskCreate(task_UART_RX, "UART-RX", 512, NULL, 2, NULL);
    xTaskCreate(task_UART_TX, "UART-TX", 128, NULL, 1, NULL); */
    
    // Arranca el scheduler
    vTaskStartScheduler();
    while(1);
}