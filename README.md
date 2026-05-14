# Control Adaptativo de Vehiculo con MCPWM

## Estructura

```text
.
├── firmware/
│   ├── remote/        # Proyecto ESP-IDF de las comunicaciones (probablemente via web)
│   └── vehicle/       # Proyecto ESP-IDF del coche PiCar-X
├── shared/
│   └── components/
│       └── sem_protocol/
├── docs/
│   └── architecture.md
├── hardware/
│   └── pinout.md
```

## Arquitectura

```text
[ESP32-S3 remote + joystick]
        |
        | WiFi: comando de velocidad/direccion
        v
[ESP32-S3 vehicle]
        |-- MCPWM -> Driver TB6612FNG ->motores DC

```

## Compilacion (solo probada en mi mac por el momento)

Cada firmware se compila desde su propia carpeta:

```bash
cd firmware/vehicle
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/tty.usbmodemXXXX flash monitor
```

```bash
cd firmware/remote
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/tty.usbmodemXXXX flash monitor
```


## Equipo

Javier Baena, Adriana Baghdasaryan, Paula Barona y Maria Fatima Cebria.
