MEJORAS
1. Empezar con la ultima parametrizacion cargada (lectura de EEPROM)
    -Se agrego cantidad de R y guardado unico de Setpoints en TareaConfig -> OK
    -Falta vincular la tarea EEPROM y la segregacion de la memoria -> OK
    -Falta hacer una lectura de memoria al inicializar el dispositivo -> OK
    -Falta hacer que lea los setpoints de la memoria (TareaResistencia y TareaControl) -> OK
    -Si no hay nada en EEPROM, arrancar en Config -> OK

2. Gestion de resistencias y PID (independizar tarea)
3. Bloqueo de funcionamiento al sobrepasar setpoints MAX y MIN
4. Setear cantidad de resistencias en configuracion -> OK
5. Eliminar setpoint global y mandar por cola --> Linea 640
    Guardar en memoria y que lo tome de ahi directamente
        1. Cuando se prende
        2. Cuando se modifica el config



Memoria
1.      Vmax, Imax, Vmin, Imin          (x4 FLOAT)
2.      Tiempo, Cantidad Resistencia    (x2 INT)
3.      SET RESISTENCIAS                (x10 xxxx)



EEPROM
    1. Inicializacion -> Leer EEPROM y mandar por cola -> OK
    2. Escribir Setpoint guardado en Config -> OK
    3. Escribir Alarmas
    4. Escribir cada X tiempo los parametros de Vin y Vshunt
    5. Parametros Kp, Ki y Kd (Lectura y escritura)








1-------Tarea PID (nueva)

void task_Control (void *pvParameters) {
    
    //pid_params_t pid = { .Kp = 1.85f, .Ki = 0.90f, .Kd = 0.0f, .Ts = 0.07f };
    pid_params_t pid = { .Kp = 1.85f, .Ki = 3.22f, .Kd = 0.24f, .Ts = 0.1f };
    pid_state_t pid_state = {0};
    setpoint_data_t setpoint;
    sensado_data_t measurement, alarma_measurement;
    eeprom_data_t alarma_eeprom;
    lcd_data_t buffer_lcd;
    alarma_flag_t alarma_flags;
    float dac_out, error, error_anterior;

    TickType_t last_lcd_update = 0;
    TickType_t last_res_update = 0;   // Control de tiempo para Sem_Resistencia
    TickType_t last_auxdac_update = 0;
    TickType_t last_alarmaeeprom_update = 0;
    TickType_t alarma_timer = 0;
    
    bool alerta = false;
    float valorM=0;

    while(1) {
        //  ----------- Ajustes PID --------------
        
        /* if(xSemaphoreTake(Sem_Bin_Select_Mas, 0) == pdTRUE)
            pid.Kp = pid.Kp + 0.01;
        if(xSemaphoreTake(Sem_Bin_Select_Menos, 0) == pdTRUE)
            pid.Kp = pid.Kp - 0.01; */
 
        if(xSemaphoreTake(Sem_Bin_Select_Mas, 0) == pdTRUE)
            pid.Kd = pid.Kd + 0.01;
        if(xSemaphoreTake(Sem_Bin_Select_Menos, 0) == pdTRUE)
            pid.Kd = pid.Kd - 0.01; 
 
        // Esperar nueva medición
        if(xQueueReceive(Queue_Sensado, &measurement, 0) == pdTRUE) {
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
            // Control sin saturar
            //float u_unsat = pid.Kp*error + pid.Ki*pid_state.integral; // previo a saturar
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

            // Calculo sistema PID
            //output =  pid.Kp * error + pid.Ki * pid_state.integral;
            
            pid_state.prev_error = error;                
            
            // Enviar valor DAC a cola
            dac_out = output;
            xQueueSend(Queue_DAC, &dac_out, portMAX_DELAY);
            
            
            TickType_t now = xTaskGetTickCount();

            // Dar semáforo a task_Resistencia según tiempo configurado
            if ((now - last_res_update) >= pdMS_TO_TICKS(setpoint_global.tiempo_ms)) {
                xSemaphoreGive(Sem_Bin_Resistencia);
                last_res_update = now;
                pid_state.integral = 0.0f; // Resetear integral al cambiar setpoint
                pid_state.prev_error = 0.0f;
            }
            
            // ------------- ALARMAS - Si hay alerta guarda en EEPROM --------------
            alarma_flags = check_limits(&setpoint_global, &measurement, &alarma_measurement);

            // Encendido LEDs
            gpio_put(PIN_LED_MAX, (alarma_flags & (ALARMA_VMAX | ALARMA_IMAX)) != 0);
            gpio_put(PIN_LED_MIN, (alarma_flags & (ALARMA_VMIN | ALARMA_IMIN)) != 0);

            if (alarma_flags) {
                if (alarma_timer == 0) {
                    alarma_timer = now; // arranca el conteo
                } else if ((now - alarma_timer) >= pdMS_TO_TICKS(2000)) { // 1 seg mínimo de alarma para activar
                    if ((R_actual > 0) && (measurement.Vin_v > 1) && ((now - last_alarmaeeprom_update) >= pdMS_TO_TICKS(5000))) {
                    // Solo manda alarma a EEPROM si R>0 y Vin>1
                        alarma_eeprom.tipo_dato = EEPROM_DATA_ALARMA;

                        if (xSemaphoreTake(Sem_I2C0_Mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                            ds3231_get_datetime(&dt, &rtc);
                            alarma_eeprom.timestamp = dt;
                            xSemaphoreGive(Sem_I2C0_Mutex);
                        }
                    
                        // Manda todas las alarmas
                        if (alarma_flags & ALARMA_VMAX) { 
                            alarma_eeprom.id = ID_VMAX; 
                            alarma_eeprom.valor = alarma_measurement.Vin_v; 
                            xQueueSend(Queue_EEPROM, &alarma_eeprom, 0); }
                        if (alarma_flags & ALARMA_IMAX) { 
                            alarma_eeprom.id = ID_IMAX; 
                            alarma_eeprom.valor = alarma_measurement.Iload_ma; 
                            xQueueSend(Queue_EEPROM, &alarma_eeprom, 0); }
                        if (alarma_flags & ALARMA_VMIN) { 
                            alarma_eeprom.id = ID_VMIN; 
                            alarma_eeprom.valor = alarma_measurement.Vin_v; 
                            xQueueSend(Queue_EEPROM, &alarma_eeprom, 0); }
                        if (alarma_flags & ALARMA_IMIN) { 
                            alarma_eeprom.id = ID_IMIN; 
                            alarma_eeprom.valor = alarma_measurement.Iload_ma; 
                            xQueueSend(Queue_EEPROM, &alarma_eeprom, 0); }

                        last_alarmaeeprom_update = now;
                    }
                }
            } else {
                alarma_timer = 0; // resetea el timer si no hay alarma
            }

            // -------- LCD - Medicion en curso -----------
            if (((now - last_lcd_update)  >= pdMS_TO_TICKS(TIEMPO_REFRESH_LCD_MS)) && ((now - last_alarmaeeprom_update)  >= pdMS_TO_TICKS(1000))) {
                lcd_show_cursor(false,false);
                snprintf(buffer_lcd.textoLCD[0], 21, "%-20s", "Medicion en curso");
                //snprintf(buffer_lcd.textoLCD[1], 21, "R: %-4d OHM P:%1.2f", R_actual, pid.Kp);
                snprintf(buffer_lcd.textoLCD[1], 21, "R: %-4d OHM D:%1.2f", R_actual, pid.Kd);
                //snprintf(buffer_lcd.textoLCD[1], 21, "R: %-4d OHM   ", R_actual);
                snprintf(buffer_lcd.textoLCD[2], 21, "V entrada: %-5.2f V", measurement.Vin_v);
                snprintf(buffer_lcd.textoLCD[3], 21, "Corriente: %-4d mA", (int)measurement.Iload_ma);
                //lcd_clear();
                xQueueSend(Queue_EscribirLCD, &buffer_lcd, 0);
                last_lcd_update = now;
            }

        }
        
        // Frecuencia de calculo -> 1ms -> 1KHz
        vTaskDelay(pdMS_TO_TICKS(10)); 
    }
}





2-------Tarea PID (vieja)

void task_Control (void *pvParameters) {
    
    //pid_params_t pid = { .Kp = 1.85f, .Ki = 0.90f, .Kd = 0.0f, .Ts = 0.07f };
    pid_params_t pid = { .Kp = 1.85f, .Ki = 3.22f, .Kd = 0.0f, .Ts = 0.1f };
    pid_state_t pid_state = {0};
    setpoint_data_t setpoint;
    sensado_data_t measurement, alarma_measurement;
    eeprom_data_t alarma_eeprom;
    lcd_data_t buffer_lcd;
    alarma_flag_t alarma_flags;
    float dac_out, error, error_anterior;

    TickType_t last_lcd_update = 0;
    TickType_t last_res_update = 0;   // Control de tiempo para Sem_Resistencia
    TickType_t last_auxdac_update = 0;
    TickType_t last_alarmaeeprom_update = 0;
    TickType_t alarma_timer = 0;
    
    bool alerta = false;
    float valorM=0;

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
        // Esperar nueva medición
        if(xQueueReceive(Queue_Sensado, &measurement, 0) == pdTRUE) {
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

            // Integración con anti-windup
            float u_unsat = pid.Kp*error + pid.Ki*pid_state.integral; // previo a saturar
            float output = u_unsat;  // se saturará más abajo

            // Anti-windup condicional
            bool saturado_arriba = (u_unsat > 5.0f);
            bool saturado_abajo  = (u_unsat < 0.0f);

            // Integro solo si no esta saturado o error lleva hacia adentro
            if (!( (saturado_arriba && error > 0.0f) || (saturado_abajo && error < 0.0f) )) {
                pid_state.integral += error * pid.Ts;
            }

            // Limitante integral
            if (pid_state.integral > 5.00f) pid_state.integral = 5.00f;
            if (pid_state.integral < -5.00f) pid_state.integral = -5.00f;

            // Calculo sistema PI
            output =  pid.Kp * error + pid.Ki * pid_state.integral;
            
            pid_state.prev_error = error;                
            // Limito señal DAC
            if (output < 0.0f) output = 0.0f;
            if (output > 5.0f) output = 5.0f;
            // Enviar valor DAC a cola
            dac_out = output;
            xQueueSend(Queue_DAC, &dac_out, portMAX_DELAY);
            
            
            TickType_t now = xTaskGetTickCount();

            // Dar semáforo a task_Resistencia según tiempo configurado
            if ((now - last_res_update) >= pdMS_TO_TICKS(setpoint_global.tiempo_ms)) {
                xSemaphoreGive(Sem_Bin_Resistencia);
                last_res_update = now;
                pid_state.integral = 0.0f; // Resetear integral al cambiar setpoint
                pid_state.prev_error = 0.0f;
            }
            
            // ------------- ALARMAS - Si hay alerta guarda en EEPROM --------------
            alarma_flags = check_limits(&setpoint_global, &measurement, &alarma_measurement);

            // Encendido LEDs
            gpio_put(PIN_LED_MAX, (alarma_flags & (ALARMA_VMAX | ALARMA_IMAX)) != 0);
            gpio_put(PIN_LED_MIN, (alarma_flags & (ALARMA_VMIN | ALARMA_IMIN)) != 0);

            if (alarma_flags) {
                if (alarma_timer == 0) {
                    alarma_timer = now; // arranca el conteo
                } else if ((now - alarma_timer) >= pdMS_TO_TICKS(2000)) { // 1 seg mínimo de alarma para activar
                    if ((R_actual > 0) && (measurement.Vin_v > 1) && ((now - last_alarmaeeprom_update) >= pdMS_TO_TICKS(5000))) {
                    // Solo manda alarma a EEPROM si R>0 y Vin>1
                        alarma_eeprom.tipo_dato = EEPROM_DATA_ALARMA;

                        if (xSemaphoreTake(Sem_I2C0_Mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                            ds3231_get_datetime(&dt, &rtc);
                            alarma_eeprom.timestamp = dt;
                            xSemaphoreGive(Sem_I2C0_Mutex);
                        }
                        

                        // Manda todas las alarmas
                        if (alarma_flags & ALARMA_VMAX) { 
                            alarma_eeprom.id = ID_VMAX; 
                            alarma_eeprom.valor = alarma_measurement.Vin_v; 
                            xQueueSend(Queue_EEPROM, &alarma_eeprom, 0); }
                        if (alarma_flags & ALARMA_IMAX) { 
                            alarma_eeprom.id = ID_IMAX; 
                            alarma_eeprom.valor = alarma_measurement.Iload_ma; 
                            xQueueSend(Queue_EEPROM, &alarma_eeprom, 0); }
                        if (alarma_flags & ALARMA_VMIN) { 
                            alarma_eeprom.id = ID_VMIN; 
                            alarma_eeprom.valor = alarma_measurement.Vin_v; 
                            xQueueSend(Queue_EEPROM, &alarma_eeprom, 0); }
                        if (alarma_flags & ALARMA_IMIN) { 
                            alarma_eeprom.id = ID_IMIN; 
                            alarma_eeprom.valor = alarma_measurement.Iload_ma; 
                            xQueueSend(Queue_EEPROM, &alarma_eeprom, 0); }

                        // PARAR FUNCIONAMIENTO DE LA CARGA (PUNTO 3)
                        /*
                            Codigo
                        */

                        last_alarmaeeprom_update = now;
                    }
                }
            } else {
                alarma_timer = 0; // resetea el timer si no hay alarma
            }

            // -------- LCD - Medicion en curso -----------
            if (((now - last_lcd_update)  >= pdMS_TO_TICKS(TIEMPO_REFRESH_LCD_MS)) && ((now - last_alarmaeeprom_update)  >= pdMS_TO_TICKS(1000))) {
                lcd_show_cursor(false,false);
                snprintf(buffer_lcd.textoLCD[0], 21, "%-20s", "Medicion en curso");
                //snprintf(buffer_lcd.textoLCD[1], 21, "R: %-4d OHM I:%1.2f", R_actual, pid.Ki);
                snprintf(buffer_lcd.textoLCD[1], 21, "R: %-4d OHM   ", R_actual);
                snprintf(buffer_lcd.textoLCD[2], 21, "V entrada: %-5.2f V", measurement.Vin_v);
                snprintf(buffer_lcd.textoLCD[3], 21, "Corriente: %-4d mA", (int)measurement.Iload_ma);
                //lcd_clear();
                xQueueSend(Queue_EscribirLCD, &buffer_lcd, 0);
                last_lcd_update = now;
            }

        }
        
        // Frecuencia de calculo -> 1ms -> 1KHz
        vTaskDelay(pdMS_TO_TICKS(10)); 
    }
}
