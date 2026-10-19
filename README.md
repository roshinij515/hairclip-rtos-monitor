# Animated Hairclip + RTOS Task Monitor

A wearable animated display built on the **LilyGO T-Display-S3** (ESP32-S3), plus a live **FreeRTOS task monitor** that runs on the same board. It can run on a LiPo battery.

<!-- Add a photo or GIF here. Upload it to an /images folder and use: -->
<!-- ![Demo](images/demo.gif) -->

## What it does

**Hairclip animations**
- Three animated display patterns: ribbons, mandala and neon heart
- Small enough to mount on a hair clip

**RTOS task monitor**
- Task table showing the running FreeRTOS tasks
- Live CPU usage graph
- Heap memory screen
- Button navigation between screens
- Tested with deliberate faults (heap leak, CPU hog, stack burst) to confirm the monitor catches them

**Combined version**
- Patterns and monitor in one sketch
- Brightness control, dim/sleep mode and a battery readout
- Runs on a LiPo battery

## Project structure

```
animations/
    hairclip_ribbons/     Ribbons pattern
    hairclip_mandala/     Mandala pattern
    hairclip_heart/       Neon heart pattern
rtos_monitor/
    hairclip_combined/    Combined sketch: patterns + task monitor + battery readout
```

Open any `.ino` file in Arduino IDE. Each sketch lives in a folder with the same name, as Arduino requires.

## Hardware

- LilyGO T-Display-S3
- USB-C cable
- LiPo battery (optional, for wearable use)

## Software

- Arduino IDE 2.x
- ESP32 Arduino core 2.0.17
- Libraries: _list the libraries you installed here (for example the display library)_

## Getting started

1. Install Arduino IDE 2.x and the ESP32 board package (version 2.0.17).
2. Select the board settings for the T-Display-S3: _add the exact board name and settings you use_.
3. Open one of the sketches from the folders above.
4. Connect the board with USB-C and click Upload.

## Project status

- [x] Animation running on the display
- [x] Three display patterns
- [x] RTOS task monitor (task table, CPU graph, heap screen)
- [x] Fault tests (heap leak, CPU hog, stack burst)
- [x] Patterns and monitor combined in one sketch
- [x] Battery power, brightness control, dim/sleep and battery readout
- [ ] Mount on the clip
- [ ] Full run-time stats (possibly moving to PlatformIO)

## Credits

- Inspired by a microcontroller hairclip video from zoeylix
- Built on [FreeRTOS](https://github.com/FreeRTOS/FreeRTOS-Kernel)

## License

MIT (see the LICENSE file)
