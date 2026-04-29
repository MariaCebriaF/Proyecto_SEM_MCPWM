# Pinout pendiente

Rellena esta tabla antes de implementar drivers reales. De momento el codigo usa stubs y no toca pines.

## ESP32-S3 Vehicle

| Funcion | GPIO | Notas |
| --- | --- | --- |
| Motor izquierdo PWM | TBD | MCPWM |
| Motor izquierdo direccion | TBD | GPIO/H-bridge |
| Motor derecho PWM | TBD | MCPWM |
| Motor derecho direccion | TBD | GPIO/H-bridge |
| Servo direccion | TBD | PWM/LEDC o MCPWM |
| Ultrasonidos TRIG | TBD | HC-SR04 o equivalente |
| Ultrasonidos ECHO | TBD | comprobar nivel 3.3 V |

## ESP32-S3 Remote

| Funcion | GPIO | Notas |
| --- | --- | --- |
| Joystick X | TBD | ADC |
| Joystick Y | TBD | ADC |
| Boton joystick | TBD | GPIO con pull-up |
| Boton extra | TBD | opcional |
