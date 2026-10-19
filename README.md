# Animated Hairclip + RTOS Task Monitor

A wearable animated display built on the **LilyGO T-Display-S3** (ESP32-S3), plus a live **FreeRTOS task monitor** that runs on the same board.

<!-- Add a photo or GIF here. Drag the file into the GitHub editor, or save it in an /images folder and use: -->
<!-- ![Demo](images/demo.gif) -->

## What it does

**Hairclip animations**
- Three animated display patterns: ribbons, mandala and neon heart
- Designed to be small enough to mount on a hair clip

**RTOS task monitor**
- Task table showing the running FreeRTOS tasks
- Live CPU usage graph
- Heap memory screen
- Button navigation between screens
- Tested with deliberate faults (heap leak, CPU hog, stack burst) to confirm the monitor catches them

## Hardware

- LilyGO T-Display-S3
- USB-C cable
- (Planned) LiPo battery and a clip mount

## Software

- Arduino IDE 2.x
- ESP32 Arduino core 2.0.17
- Libraries: _list the libraries you installed here (for example the display library)_

## Getting started

1. Install Arduino IDE 2.x and the ESP32 board package (version 2.0.17).
2. Select the board settings for the T-Display-S3: _add the exact board name and settings you use_.
3. Open the sketch folder in Arduino IDE.
4. Connect the board and click Upload.

## Project status

- [x] Animation running on the display
- [x] Three display patterns
- [x] RTOS task monitor (task table, CPU graph, heap screen)
- [x] Fault tests (heap leak, CPU hog, stack burst)
- [ ] Combine the patterns with the monitor
- [ ] Battery power and brightness control
- [ ] Mount on the clip
- [ ] Full run-time stats (possibly moving to PlatformIO)

## Credits

- Inspired by a microcontroller hairclip video from zoeylix
- Built on [FreeRTOS](https://github.com/FreeRTOS/FreeRTOS-Kernel)

## License

MIT (see the LICENSE file)
