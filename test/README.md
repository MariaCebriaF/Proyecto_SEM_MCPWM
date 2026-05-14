# TB6612FNG MCPWM forward test

Proyecto ESP-IDF minimo para probar el TB6612FNG con MCPWM en ESP32-S3.

## Que hace

- Inicializa MCPWM a 20 kHz.
- Activa `STBY`.
- Mueve primero el motor A al 70% durante 3 segundos.
- Para 2 segundos.
- Mueve despues el motor B al 70% durante 3 segundos.
- Para 2 segundos.
- Repite en bucle.

Prueba siempre con las ruedas levantadas.

## Pinout actual

| TB6612FNG | ESP32-S3 |
| --- | --- |
| `STBY` | `GPIO17` |
| `PWMA` | `GPIO4` |
| `AIN1` | `GPIO5` |
| `AIN2` | `GPIO6` |
| `PWMB` | `GPIO7` |
| `BIN1` | `GPIO15` |
| `BIN2` | `GPIO16` |
| `VCC` | `3V3` |
| `VM` | bateria motores |
| `GND` | GND comun con ESP32-S3 y bateria |

## Compilar y flashear

Si `export.sh` funciona en tu terminal:

```bash
cd test
source /Users/mariacebria/esp/v5.5.2/esp-idf/export.sh
idf.py set-target esp32s3
idf.py -p /dev/tty.usbmodem5B140695561 flash monitor
```

Si `export.sh` falla por el entorno Python, usa:

```bash
cd test
export PATH="/Users/mariacebria/.espressif/tools/ninja/1.12.1:$PATH"
export IDF_PYTHON_ENV_PATH="/Users/mariacebria/.espressif/python_env/idf5.5_py3.13_env"

/Users/mariacebria/.espressif/python_env/idf5.5_py3.13_env/bin/python /Users/mariacebria/esp/v5.5.2/esp-idf/tools/idf.py -p /dev/tty.usbmodem5B140695561 flash monitor
```

Para salir del monitor: `Ctrl+]`.
