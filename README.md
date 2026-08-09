# M5StopWatch UserDemo Arduino

Arduino/PlatformIO rewrite of M5Stack's official
[M5StopWatch UserDemo](https://github.com/m5stack/M5StopWatch-UserDemo), based on commit V0.5 `6b4aa125288b6fe9dca661f10159f6e1e5ee785c`.

It retains the official applications, LVGL interface, animations and assets. Hardware access, storage, audio,
FFT and Badge AP are implemented for Arduino with M5Unified and ESP32 Arduino.

Build and upload:

```sh
pio run -e m5stack-stopwatch
pio run -e m5stack-stopwatch -t upload
```

The source code is licensed under the MIT License.
