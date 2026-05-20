# Pinout

Revisa esta tabla antes de conectar motores. La foto del cableado no deja leer todos los GPIO con seguridad, asi que el firmware deja los pines concentrados como macros en `firmware/vehicle/main/app_main.c`.

## ESP32-S3 Vehicle

| Funcion | GPIO | Notas |
| --- | --- | --- |
| TB6612 STBY | GPIO17 | Cambiar `TB6612_STBY_GPIO` si el cableado real usa otro pin |
| TB6612 PWMA | GPIO4 | MCPWM, motor A |
| TB6612 AIN1 | GPIO5 | GPIO direccion motor A |
| TB6612 AIN2 | GPIO6 | GPIO direccion motor A |
| TB6612 PWMB | GPIO7 | MCPWM, motor B |
| TB6612 BIN1 | GPIO15 | GPIO direccion motor B |
| TB6612 BIN2 | GPIO16 | GPIO direccion motor B |
| TB6612 VCC | 3V3 | Alimentacion logica |
| TB6612 VM | Bateria motores | Alimentacion potencia motores |
| TB6612 GND | GND comun | Unir GND de bateria, TB6612 y ESP32-S3 |
| Servo direccion | TBD | PWM/LEDC o MCPWM; no implementado todavia |
| Ultrasonidos TRIG | TBD | HC-SR04 o equivalente |
| Ultrasonidos ECHO | TBD | comprobar nivel 3.3 V |

## ESP32-S3 Remote

| Funcion | GPIO | Notas |
| --- | --- | --- |
| Joystick X | TBD | ADC |
| Joystick Y | TBD | ADC |
| Boton joystick | TBD | GPIO con pull-up |
| Boton extra | TBD | opcional |
