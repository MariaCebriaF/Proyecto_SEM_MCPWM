# Control Adaptativo de Vehiculo con MCPWM

Proyecto SEM basado en dos placas ESP32-S3 y un kit PiCar-X:

- `firmware/vehicle`: ESP32-S3 montado en el vehiculo. Controla motores con MCPWM, sirve una interfaz web por WiFi, aplica logica de seguridad y publica telemetria.
- `firmware/remote`: ESP32-S3 del mando. Lee el joystick y envia comandos al vehiculo por WiFi. Queda como alternativa al control web.
- `shared/components/sem_protocol`: componente compartido con las estructuras de comandos/telemetria para que ambos proyectos hablen el mismo idioma.

La separacion en dos proyectos ESP-IDF es intencionada: el mando y el vehiculo tienen hardware, tareas y configuracion distinta, aunque compartan el protocolo.

## Estructura

```text
.
├── firmware/
│   ├── remote/        # Proyecto ESP-IDF de las comunicaciones
│   └── vehicle/       # Proyecto ESP-IDF del coche PiCar-X
├── shared/
│   └── components/
│       └── sem_protocol/
├── docs/
│   └── architecture.md
├── hardware/
│   └── pinout.md
└── hcsr04/            # Driver HC-SR04, pendiente de integracion real
```

## Arquitectura

```text
[Ordenador con navegador]
        |
        | WiFi AP + HTTP: comandos WASD/flechas
        v
[ESP32-S3 vehicle]
        |-- MCPWM -> Driver TB6612FNG -> motores DC
        |-- GPIO -> sensor distancia
        |-- HTTP/telemetria -> velocidad, distancia, estado
```

El vehiculo crea la red `SEM-PICAR` y sirve el panel de control en `http://192.168.4.1`.

## Logica del vehiculo

| Distancia | Comportamiento |
| --- | --- |
| `> 50 cm` | Velocidad normal segun control |
| `20-50 cm` | Reduccion progresiva |
| `< 20 cm` | Parada automatica |

Actualmente el firmware web compila y mueve motores con el TB6612FNG. La lectura de distancia en `firmware/vehicle/main/app_main.c` sigue usando un stub de `100 cm` hasta conectar los GPIO reales del HC-SR04 al firmware del vehiculo.

## Compilacion

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

## Siguientes decisiones tecnicas

1. Confirmar los pines reales del PiCar-X y del joystick en `hardware/pinout.md`.
2. Integrar el sensor de distancia real y sustituir el stub de `read_distance_cm_stub`.
3. Decidir si la direccion sera diferencial con dos motores DC o servo delantero del PiCar-X.
4. Mantener los drivers por capas:
   - `vehicle`: motores MCPWM, direccion/servo, sensor distancia, comunicacion, web/telemetria.
   - `remote`: lectura ADC del joystick, botones, comunicacion.

## Equipo

Javier Baena, Adriana Baghdasaryan, Paula Barona y Maria Fatima Cebria.
