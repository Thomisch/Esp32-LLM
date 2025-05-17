# Running a LLM on the ESP32

## Optimizing Llama2.c for the ESP32

With the following changes to `llama2.c`, I am able to achieve **19.13 tok/s**:

1. Utilizing both cores of the ESP32 during math heavy operations.
2. Utilizing some special [dot product functions](https://github.com/espressif/esp-dsp/tree/master/modules/dotprod/float) from the [ESP-DSP library](https://github.com/espressif/esp-dsp) that are designed for the ESP32-S3. These functions utilize some of the [few SIMD instructions](https://bitbanksoftware.blogspot.com/2024/01/surprise-esp32-s3-has-few-simd.html) the ESP32-S3 has.
3. Maxing out CPU speed to 240 MHz and PSRAM speed to 80MHZ and increasing the instruction cache size.


## Setup
This requires the [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/index.html#installation) toolchain to be installed

```
idf.py build
idf.py -p /dev/{DEVICE_PORT} flash
```


