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