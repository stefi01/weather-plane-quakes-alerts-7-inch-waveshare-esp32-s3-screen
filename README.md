# weather, night sky, plane tracker

Version 1.1 for the Waveshare ESP32-S3-Touch-LCD-7 with 8MB flash and 8MB PSRAM. The picture is 800 by 480.

It shows the weather for one US city or ZIP, planes overhead, earthquakes, and the night sky. Wi-Fi is 2.4 GHz. Power is USB-C.

The manual is [weather-night-sky-plane-tracker-manual.pdf](weather-night-sky-plane-tracker-manual.pdf).

## What is here

- `firmware/meridian/meridian.ino` and the picture headers in that same folder
- `libraries/GFX_Library_for_Arduino` and `libraries/TJpg_Decoder`, the two libraries the sketch includes
- `libraries/ArduinoJson` version 7.4.2
- `ota/version.txt` and `ota/meridian.bin`, the files the About page reads
- the manual PDF

Copy the three library folders into the Arduino libraries folder before compiling. Do not compile a second copy of GFX from inside a sketch folder.

## Updates

The About page checks

https://raw.githubusercontent.com/stefi01/weather-plane-quakes-alerts-7-inch-waveshare-esp32-s3-screen/main/ota/version.txt

and installs

https://raw.githubusercontent.com/stefi01/weather-plane-quakes-alerts-7-inch-waveshare-esp32-s3-screen/main/ota/meridian.bin

`ota/version.txt` is `1.1`. The 8MB partition has one app slot, so the first copy goes on over USB. INSTALL says it needs a second slot on that table.

## Board

- Board: ESP32S3 Dev Module
- PSRAM: OPI PSRAM
- Flash Size: 8MB (64Mb)
- Partition Scheme: 8M with spiffs (3MB APP/1.5MB SPIFFS)
- USB CDC On Boot: Disabled
- USB Mode: Hardware CDC and JTAG
- Upload Mode: UART0 / Hardware CDC
- Upload Speed: 921600
- Flash Mode: QIO 80MHz
- CPU Frequency: 240MHz (WiFi)

Do not choose the 16MB flash size or the 16MB partition. Leave PSRAM execute-in-place off.

The full Tools list is `firmware/meridian/settings to change.txt`.

## Loading the bin

`ota/meridian.bin` is the app bin. Do not pick a bootloader, partitions, or merged file.

Flash Download Tool 3.9.3. WorkMode Develop. LoadMode UART. ChipType ESP32-S3.

SPI SPEED 80MHz. SPI MODE DIO. DoNotChgBin ticked. LockSettings left unticked. BAUD 115200. Address `0x10000`. Leave Erase unticked.

Slide the switch to UART1. Cable in the socket marked UART. Leave BOOT alone. After FINISH, press RESET once.
