MEJORAS
1. Empezar con la ultima parametrizacion cargada (lectura de EEPROM)
    -Se agrego cantidad de R y guardado unico de Setpoints en TareaConfig
    -Falta vincular la tarea EEPROM y la segregacion de la memoria
    -Falta hacer una lectura de memoria al inicializar el dispositivo
    -Falta hacer que lea los setpoints de la memoria (TareaResistencia y TareaControl)

2. Gestion de resistencias y PID (independizar tarea)
3. Bloqueo de funcionamiento al sobrepasar setpoints MAX y MIN
4. Setear cantidad de resistencias en configuracion
5. Eliminar setpoint global y mandar por cola --> Linea 640
    Guardar en memoria y que lo tome de ahi directamente
        1. Cuando se prende
        2. Cuando se modifica el config



Memoria
1.      Vmax, Imax, Vmin, Imin          (x4 FLOAT)
2.      Tiempo, Cantidad Resistencia    (x2 INT)
3.      SET RESISTENCIAS                (x10 xxxx)
