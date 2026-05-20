# HC SR04 Driver Component

The HC-SR04 is a low-cost ultrasonic ranging module.

The driver uses one GPIO output pin for trigger and one GPIO input pin for echo.

| HC SR04 PIN | ESP driver | Default GPIO |
| ---: | :---: | :---: |
| TRIGGER | CONFIG_TRIGGER_PIN | 33 |
| ECHO | CONFIG_ECHO_PIN | 32 |

These connections are configured with `idf.py menuconfig` under `Component config -> HCSR04 menu`.

## Usage

```c
#include "hcsr04_driver.h"

ESP_ERROR_CHECK(UltrasonicInit());

uint32_t distance_cm = 0;
esp_err_t err = UltrasonicMeasure(400, &distance_cm);
UltrasonicAssert(err);
```

## License

This component is provided under MIT license.

## Contributing

Please check for contribution guidelines.tbd
