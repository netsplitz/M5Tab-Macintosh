# Supported Board Configurations

This document describes the pin maps for each board that can be built from this
tree. Pick the PlatformIO env that matches your hardware:

| Board                                           | env                    | Pin map         |
|-------------------------------------------------|------------------------|-----------------|
| M5Stack Tab5                                    | `esp32p4_pioarduino`   | See below       |
| Waveshare ESP32-P4-WIFI6-Touch-LCD-10.1         | `waveshare_p4_101`     | [docs/waveshare/README.md](docs/waveshare/README.md) |
| Waveshare ESP32-P4-WIFI6 + Olimex 2.8" MIPI     | `waveshare_p4_olimex640` | See below      |

## M5Stack Tab5

### Overview

The M5Stack Tab5 (SKU: C145/K145) is a highly expandable, portable smart-IoT terminal development device featuring a **dual-chip architecture**.

## Dual-Chip Architecture

The Tab5 uses two separate ESP32 chips that communicate internally:

| Chip | Role | Key Features |
|------|------|--------------|
| **ESP32-P4** | Main Application Processor | 400MHz dual-core RISC-V, 32MB PSRAM, no native WiFi/Bluetooth |
| **ESP32-C6** | Wireless Co-processor | WiFi 6 (2.4GHz), Bluetooth LE 5.0, Thread/Zigbee capable |

The ESP32-P4 handles all application logic, display rendering, and peripheral control. WiFi and Bluetooth operations are delegated to the ESP32-C6, which communicates with the P4 via an internal bus. **M5Unified abstracts this complexity**, allowing standard `WiFi.h` and `Bluetooth.h` usage in your code.

## Display

- **Size**: 5 inches
- **Resolution**: 1280 x 720 (720p)
- **Type**: IPS TFT
- **Interface**: MIPI-DSI
- **Display / touch revisions**: Legacy ILI9881C + GT911, or integrated
  ST7123 / ST7121; M5GFX auto-detects the installed controller
- **Library**: M5GFX / M5Unified

## Audio System

| Component | Chip | Function |
|-----------|------|----------|
| Audio Codec | ES8388 | Hi-Fi recording/playback |
| AEC Frontend | ES7210 | Acoustic echo cancellation, dual microphones |
| Amplifier | NS4150B | 1W speaker output |
| Headphone | 3.5mm jack | Stereo output |

## Sensors

| Sensor | Chip | Function |
|--------|------|----------|
| IMU | BMI270 | 6-axis accelerometer + gyroscope |
| RTC | RX8130CE | Real-time clock with timed interrupt wake-up |
| Power Monitor | INA226 | Battery voltage/current monitoring |

## Connectivity & Expansion

### USB Ports
- **USB Type-A**: Host mode (for keyboard, mouse, USB devices)
- **USB Type-C**: USB 2.0 OTG (programming, data transfer)

### Industrial Interfaces
- **RS-485**: SIT3088 transceiver with switchable 120Ω terminator

### Expansion
- **HY2.0-4P**: I2C expansion port (Grove compatible)
- **M5-Bus**: Rear expansion connector for M5Stack modules
- **Stamp Expansion Pad**: For M5Stamp modules
- **microSD**: Card slot for storage

### Antenna
- Built-in 3D antenna
- MMCX external antenna port
- Software-switchable via `RF_PTH_L_INT_H_EXT` (LOW = internal, HIGH = external)

## Pin Mappings

### I2C Bus (Shared)
| Signal | GPIO |
|--------|------|
| SDA | GPIO7 |
| SCL | GPIO8 |

**I2C Addresses:**
- BMI270: 0x68
- RX8130CE (RTC): 0x32
- INA226: 0x40
- ES8388: 0x10
- ES7210: 0x41
- Integrated ST712x touch: 0x55 (newer display revisions)
- PI4IOE5V6408 (GPIO Expander): 0x43

### ESP32-C6 Communication
| Signal | GPIO |
|--------|------|
| P4 TX → C6 RX | GPIO25 |
| P4 RX ← C6 TX | GPIO26 |
| WLAN_PWR_EN | Via GPIO Expander |

### RS-485
| Signal | GPIO |
|--------|------|
| TX | GPIO24 |
| RX | GPIO23 |
| DE/RE | GPIO22 |

### HY2.0-4P (Grove Port)
| Signal | GPIO |
|--------|------|
| SDA | GPIO7 |
| SCL | GPIO8 |
| VCC | 5V (controlled by EXT5V_EN) |
| GND | GND |

### Tab5 Keyboard (Ext.Port1)

| Signal | GPIO / address |
|--------|----------------|
| SDA | GPIO0 |
| SCL | GPIO1 |
| INT (active low) | GPIO50 |
| I2C address | 0x6D (factory default) |

M5Tab-Macintosh probes this port automatically. When the official keyboard
is present it is configured for matrix events and routed to the emulated Mac
ADB keyboard; no compile-time option or boot setting is required.

### microSD Card
| Signal | GPIO |
|--------|------|
| CLK | GPIO39 |
| CMD | GPIO44 |
| DATA0 | GPIO40 |
| DATA1 | GPIO41 |
| DATA2 | GPIO42 |
| DATA3 | GPIO43 |

### Camera (SC2356 2MP)
| Signal | GPIO |
|--------|------|
| PCLK | GPIO47 |
| VSYNC | GPIO46 |
| HSYNC | GPIO45 |
| D0-D7 | GPIO13-GPIO20 |
| XCLK | GPIO48 |
| PWDN | Via GPIO Expander |
| RESET | Via GPIO Expander |

## Power System

- **Battery**: Removable NP-F550 Li-ion (2000mAh in Kit version)
- **Charging**: IP2326 charge management IC
- **Power Rail**: MP4560 buck-boost converter
- **Monitoring**: INA226 for voltage/current sensing

### Power Control
- **Power Button**: Short press = on/off, Long press = force off
- **Download Mode**: Hold BOOT button while pressing RESET

## PlatformIO Configuration

```ini
[env:esp32p4_pioarduino]
platform = https://github.com/pioarduino/platform-espressif32.git#54.03.21
upload_speed = 1500000
monitor_speed = 115200
build_type = debug
framework = arduino
board = esp32-p4-evboard
board_build.mcu = esp32p4
board_build.flash_mode = qio
build_flags =
    -DBOARD_HAS_PSRAM
    -DCORE_DEBUG_LEVEL=5
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DARDUINO_USB_MODE=1
lib_deps =
    https://github.com/M5Stack/M5GFX.git#0.2.23
    https://github.com/M5Stack/M5Unified.git#0.2.14
```

## Development Notes

### WiFi Usage
The ESP32-P4 has no native WiFi - it communicates with the ESP32-C6 via SDIO. **You MUST call `WiFi.setPins()` before any WiFi operations!**

```cpp
#include <M5Unified.h>
#include <WiFi.h>

// Tab5 WiFi SDIO pins for ESP32-C6 communication
#define SDIO2_CLK GPIO_NUM_12
#define SDIO2_CMD GPIO_NUM_13
#define SDIO2_D0  GPIO_NUM_11
#define SDIO2_D1  GPIO_NUM_10
#define SDIO2_D2  GPIO_NUM_9
#define SDIO2_D3  GPIO_NUM_8
#define SDIO2_RST GPIO_NUM_15

void setup() {
    M5.begin();
    
    // CRITICAL: Set WiFi pins BEFORE any WiFi operations
    WiFi.setPins(SDIO2_CLK, SDIO2_CMD, SDIO2_D0, SDIO2_D1, SDIO2_D2, SDIO2_D3, SDIO2_RST);
    
    WiFi.begin("SSID", "password");
}
```

Reference: [Tab5 WiFi Documentation](https://docs.m5stack.com/en/arduino/m5tab5/wifi)

### Display Usage
M5Unified provides a unified display API:

```cpp
#include <M5Unified.h>

M5.begin();
M5.Display.setTextSize(2);
M5.Display.println("Hello World");
```

### Important Build Flags
- `BOARD_HAS_PSRAM`: Enables 32MB PSRAM
- `ARDUINO_USB_CDC_ON_BOOT=1`: USB serial via Type-C
- `ARDUINO_USB_MODE=1`: USB device mode

## Resources

- [Official Documentation](https://docs.m5stack.com/en/core/Tab5)
- [Tab5 Arduino Quick Start](https://docs.m5stack.com/en/arduino/m5tab5/program)
- [M5Unified Library](https://github.com/M5Stack/M5Unified)
- [M5GFX Library](https://github.com/M5Stack/M5GFX)
- [Tab5 Schematics PDF](https://docs.m5stack.com/en/core/Tab5#schematic)

---

## Waveshare ESP32-P4-WIFI6-Touch-LCD-10.1

### Overview

The [Waveshare ESP32-P4-WIFI6-Touch-LCD-10.1](https://www.waveshare.com/esp32-p4-wifi6-touch-lcd-7-8-10.1.htm) is a 10.1" HMI development board built around the same ESP32-P4 + ESP32-C6 dual-chip architecture as the Tab5. It is supported by the `waveshare_p4_101` PlatformIO env.

### Display

- **Size**: 10.1 inches
- **Resolution**: 800 x 1280 (portrait native), rotated 90 degrees in software to present 1280 x 800 landscape
- **Driver**: JD9365 over 2-lane MIPI-DSI @ 1500 Mbps per lane
- **Backlight**: GPIO26, driven by LEDC PWM (5 kHz, 10-bit)
- **Reset**: GPIO27
- **Touch**: GT911/GT9271 on I2C (auto-detected at 0x5D or 0x14)

### Audio

- **Codec**: ES8311 (not the ES8388 used on Tab5)
- **AEC frontend**: ES7210 (same chip as Tab5; 4-mic capable but only dual mics populated)
- **Amplifier**: NS4150B (same chip as Tab5)
- **I2S pins**: MCLK=GPIO13, BCLK=GPIO12, WS=GPIO10, DOUT=GPIO9, DSIN=GPIO11
- **Amp enable**: GPIO53 (active high)

### I2C Bus (shared)

| Signal | GPIO |
|--------|------|
| SDA    | GPIO7 |
| SCL    | GPIO8 |

Clients on this bus: ES8311 codec, ES7210 mic frontend, GT911 touch controller.

### microSD Card (SDMMC 4-bit, slot 0)

| Signal | GPIO  |
|--------|-------|
| CLK    | GPIO43 |
| CMD    | GPIO44 |
| D0     | GPIO39 |
| D1     | GPIO40 |
| D2     | GPIO41 |
| D3     | GPIO42 |

Card VDD is supplied by the on-chip LDO channel 4 (configured by the BSP).
Mount point is `/sd` (set via `CONFIG_BSP_SD_MOUNT_POINT` in [sdkconfig.waveshare](sdkconfig.waveshare)).

### WiFi

WiFi 6 is routed through an ESP32-C6 co-processor using the stock
`esp_wifi_remote` + `esp_hosted` path shipped with Arduino-ESP32 3.3.x. No
manual `WiFi.setPins()` call is needed - the SDIO pin map is owned by the
`esp_hosted` component. The boot-time WiFi UX in
[src/basilisk/boot_gui.cpp](src/basilisk/boot_gui.cpp) is unchanged; the
`BoardWifi_Prepare()` hook in
[src/board/waveshare/board_wifi_waveshare.cpp](src/board/waveshare/board_wifi_waveshare.cpp)
is intentionally a no-op.

### Software stack

The Waveshare build uses the Espressif vendored drivers directly (not
M5Unified/M5GFX, which do not support this board):

- [`lib/esp32_p4_wifi6_touch_lcd_x/`](lib/esp32_p4_wifi6_touch_lcd_x/) - Waveshare's own BSP (display + touch + audio + SD + USB host helpers)
- [`lib/esp_lcd_jd9365/`](lib/esp_lcd_jd9365/) - MIPI-DSI panel driver
- [`lib/esp_lcd_touch/`](lib/esp_lcd_touch/) + [`lib/esp_lcd_touch_gt911/`](lib/esp_lcd_touch_gt911/) - GT911 touch driver
- [`lib/esp_codec_dev/`](lib/esp_codec_dev/) - ES8311/ES7210 codec abstraction

A small in-tree RGB565 renderer ([`src/board/waveshare/mini_gfx.cpp`](src/board/waveshare/mini_gfx.cpp))
provides the drawing primitives the boot GUI needs, plus the landscape->portrait
coordinate rotation required to drive the panel at its native portrait resolution.

### Resources

- [Product page](https://www.waveshare.com/esp32-p4-wifi6-touch-lcd-7-8-10.1.htm)
- [Waveshare docs](https://docs.waveshare.com/ESP32-P4-WIFI6-Touch-LCD-X)
- [Waveshare-ESP32-components on GitHub](https://github.com/waveshareteam/Waveshare-ESP32-components)

---

## Waveshare ESP32-P4-WIFI6 + Olimex MIPI-LCD2.8-640x480 (ST7701S)

### Overview

A Waveshare ESP32-P4-WIFI6 board (same ESP32-P4 + ESP32-C6 family as the
10.1" target) paired with an external [Olimex MIPI-LCD2.8-640x480](https://www.olimex.com/Products/Modules/MIPI/MIPI-LCD2.8-640x480)
MIPI-DSI display module. The on-board 10.1" JD9365 panel is replaced by
the Olimex module, which uses an ST7701S controller. Supported by the
`waveshare_p4_olimex640` PlatformIO env.

### Display

- **Resolution**: 640 x 480 (portrait 480 x 640 native, rotated 90 degrees
  in software by the shared surface layer)
- **Driver**: ST7701S over 1-lane MIPI-DSI @ 500 Mbps, 16 MHz DPI clock,
  RGB565 frame buffer (600 KB PSRAM)
- **D-PHY power**: on-chip LDO channel 3 @ 2.5 V (acquired at bring-up)
- **Reset / backlight**: none controllable on this module profile -
  `BoardDisplay_SetBacklight()` is a no-op and the power layer reports a
  fixed backlight
- **Touch**: none - input is USB HID host + on-screen keyboard
- **Panel init sequence**: the Olimex module's vendor sequence lives in
  [src/board/olimex640/board_display_olimex640.cpp](src/board/olimex640/board_display_olimex640.cpp);
  if a different ST7701S module is used, only `lcd_init_cmds` there needs
  to change

### Mac screen

640 x 480 at `PIXEL_SCALE` 1 (native 1:1 on the panel) - this is the first
shipping board that exercises the one-to-one tile path
(`writePanelIndexedTile1x` in [src/board/panel_surface.h](src/board/panel_surface.h)).
Tile grid is 20 x 12 (32 x 40 tiles, 240 total).

### Board (audio / SD / WiFi)

Identical to the Waveshare 10.1" board - ES8311 audio, SDMMC microSD at
`/sd`, ESP32-C6 WiFi 6 co-processor, USB host - so the `src/board/waveshare/`
sources for audio, SD, keyboard, and WiFi are reused unmodified; only the
display, touch, and power hooks come from [src/board/olimex640/](src/board/olimex640/).
The C6 slave firmware blob is fetched and embedded the same way as for
`waveshare_p4_101` (see [scripts/fetch_c6_firmware.py](scripts/fetch_c6_firmware.py)).

### Panel init reference

The video timings, lane rate, LDO channel, and ST7701S vendor init
sequence are taken from the standalone `esp32st7701s` reference project
(ESP32-P4 + Olimex WLK2802MIPI-15P-V2 480x640 profile), ported to RGB565
to match the shared panel surface.
