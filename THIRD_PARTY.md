# Third-party licenses

This project’s **original** source, documentation, and media are licensed under
[CC BY-NC-SA 4.0](LICENSE). That license applies only to this project’s own work.

The firmware is built against, and the prebuilt images in `firmware-bin/` and
GitHub Releases link in, third-party libraries and toolchain components. Those
components are **not** relicensed as CC BY-NC-SA 4.0. They stay under their
own terms. Copies of the license texts are in [`third_party/licenses/`](third_party/licenses/).

This combination is intentional: CC BY-NC-SA 4.0 covers the original “tiny-window”
code and docs (non-commercial, attribution, share-alike for adaptations of that
work). It does not replace or restrict the permissive licenses of the libraries
below, and ShareAlike does not apply to those libraries.

ArduinoJson is not a direct dependency of this firmware.

## Libraries and components

### TFT_eSPI 2.5.43 (bodmer/TFT_eSPI)

- Used for the LCD driver (`lib_deps` in `firmware/platformio.ini`)
- Project license: FreeBSD (copyright Bodmer), plus MIT (Adafruit_ILI9341 heritage) and BSD (Adafruit_GFX heritage)
- Upstream: https://github.com/Bodmer/TFT_eSPI
- Text: [`third_party/licenses/TFT_eSPI-license.txt`](third_party/licenses/TFT_eSPI-license.txt)

### WiFiManager 2.0.17 (tzapu/WiFiManager)

- Used for the captive-portal Wi-Fi setup
- License: MIT, copyright 2015 tzapu
- Upstream: https://github.com/tzapu/WiFiManager
- Text: [`third_party/licenses/WiFiManager-LICENSE.txt`](third_party/licenses/WiFiManager-LICENSE.txt)

### Arduino-ESP32 2.0.17 / ESP32 platform (espressif32 6.8.1)

- Framework, bootloader, `boot_app0.bin`, and related SDK components used to build and to produce the prebuilt flash images
- License: Apache License 2.0
- Upstream: https://github.com/espressif/arduino-esp32
- Text: [`third_party/licenses/Apache-2.0.txt`](third_party/licenses/Apache-2.0.txt)

### Open-Meteo

- The firmware calls the public Open-Meteo HTTP API at runtime. It is a service, not a library vendored in this repository
- https://open-meteo.com/

## Prebuilt binaries

Redistributing `firmware.bin`, `bootloader.bin`, `partitions.bin`, `boot_app0.bin`,
and `merged.bin` includes compiled third-party code. The notices and license
texts in this directory are included to meet the attribution requirements of
those licenses.
