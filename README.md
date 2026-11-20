## RP2350-LCD-0.85 C++ Demo

Demo of [Spotpear RP2350-LCD-0.85](https://spotpear.com/wiki/Raspberry-Pi-Pico-2-RP2350A-LCD-0.85-inch-Display-Screen.html) with a C++ driver instead of the slow micropython example it comes with, to see how much faster it can actually run.

Written by Claude Opus 5.5, though I wrote this README

### Features / TODO
- [X] GC9107 LCD driver with DMA and max frame rate
- [X] .bmp file decoder, both 24 bit and 16 bit (RGB565). 
    - Uses cmake and clever .incbin directive in assembly to directly include bitmap file specified in [CMakeLists.txt](CMakeLists.txt)
    - A "top down" RGB565 image can be blitted to the display directly without any conversion.
- [X] WS2812 driver
- [X] IMU driver - live updates a spinning cube / starfield
    - tap the board to show this immediately (or keep showing it)
- [ ] SD card driver

### Credits
- [flash_img.bmp](flash_img.bmp) is from Spotpear's example code.