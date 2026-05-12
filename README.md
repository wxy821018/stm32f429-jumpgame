# STM32F429 Jump Game

A 2D side-scrolling jump game for the **STM32F429I-DISC1** (Discovery Kit, MB1075).
Bare-metal C with ST's BSP for LCD/SDRAM bring-up, runs directly on the 2.4″ TFT.

![placeholder](docs/screenshot.jpg)

## Hardware

- Board: **STM32F429I-DISC1** (MCU: STM32F429ZIT6 — Cortex-M4F, 180 MHz, 2 MB flash, 192 KB SRAM + 64 KB CCM)
- Display: 2.4″ TFT, 240×320, ILI9341 panel driven via parallel RGB through the STM32's LTDC
- Framebuffer: external 8 MB SDRAM (IS42S16400J) connected over FMC, addressed at `0xD0000000`
- Input: blue **USER (B1)** push-button on PA0
- Status LEDs: green LD3 (alive heartbeat) and red LD4 (game over)

## Gameplay

- Hold the board **with the USB connector on the right** for landscape orientation (320×240).
- Press **B1** to jump.
- **Double-jump** is enabled: tap B1 again in mid-air to jump a second time (resets vertical velocity, useful for recovery).
- Obstacles vary in both width and height. Speed and spawn rate ramp up with your score.
- A red flashing bar = game over. Press B1 to retry. High score persists until power-cycle.

## Building from source

### Prerequisites (Windows)

```powershell
winget install Kitware.CMake
winget install Ninja-build.Ninja
winget install xpack-dev-tools.openocd-xpack
winget install Arm.ArmGnuToolchain
# After the Arm toolchain install, manually add its bin/ to PATH —
# the silent installer doesn't do it for you.
```

Also install **STM32CubeProgrammer** from https://www.st.com/en/development-tools/stm32cubeprog.html
(or just install the standalone ST-Link USB driver) so the OS recognises the on-board ST-Link/V2.

### Fetch the ST BSP

The `vendor/` directory is gitignored. Pull the needed pieces of STM32CubeF4 once:

```powershell
.\scripts\setup-vendor.ps1
```

This sparse-clones `Drivers/CMSIS/...`, `Drivers/STM32F4xx_HAL_Driver`, the F429I-Discovery BSP,
the ili9341 component driver, and the bitmap fonts — about 110 MB on disk.

### Configure + build

```powershell
cmake -S . -B build -G Ninja '-DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake'
cmake --build build
```

> Note: the `-D` argument **must be single-quoted** in PowerShell or it splits at the `.cmake` extension.

### Flash

```powershell
openocd -f board/stm32f4discovery.cfg -c 'program build/blink.elf verify reset exit'
```

The OpenOCD config name says "stm32f4discovery" but it auto-detects the chip over SWD, so it
works for any F4 Discovery board including the F429I-DISC1.

## Project layout

```
.
├── app/
│   ├── main.c                  # game state, physics, render, SystemClock_Config
│   ├── stm32f4xx_it.c          # SysTick + fault handlers + _init/_fini stubs
│   └── stm32f4xx_hal_conf.h    # HAL config (vendor template, all modules enabled)
├── cmake/arm-none-eabi.cmake   # cortex-m4 hard-float toolchain definitions
├── linker/stm32f429zi.ld       # FLASH 2 MB, RAM 192 KB, CCM 64 KB, SDRAM 8 MB
├── scripts/setup-vendor.ps1    # one-shot fetch of the vendored ST code
├── vendor/                     # populated by setup-vendor.ps1 (gitignored)
└── CMakeLists.txt
```

## How it works

- **Software rotation.** The LTDC drives the panel in its native 240×320 portrait. The game
  treats the screen as 320×240 landscape by rotating 90° CCW in software: landscape
  `(lx, ly)` maps to framebuffer offset `lx * 240 + ly`. All drawing goes through `px()`/
  `fill_rect()` helpers in [`app/main.c`](app/main.c) that encode this mapping.
- **Pixel format.** The BSP layer defaults to **ARGB8888** (4 bytes/pixel). The framebuffer
  is `volatile uint32_t *` at `SDRAM_DEVICE_ADDR (0xD0000000)`.
- **Frame rate.** ~60 Hz via `HAL_Delay(16)` between frames. Each frame clears the screen
  (~300 KB write to SDRAM, ≈1.5 ms) then re-draws the parallax mountains, ground, obstacles,
  player, and HUD digits.
- **No vendor fonts.** ST's `BSP_LCD_DisplayString` renders in native portrait, which would
  appear sideways in landscape. The HUD uses a tiny inline 3×5 bitmap font baked into `main.c`.
- **Clock.** Configured for 180 MHz SYSCLK with overdrive (PLLM=8, PLLN=360, PLLP=2 from
  the 8 MHz HSE). Flash latency 5 wait states.

## License

Game code (everything in `app/`, `cmake/`, `linker/`, `scripts/`, `CMakeLists.txt`) is
MIT-licensed. The vendored ST code under `vendor/` is licensed by STMicroelectronics under
their own terms — see the LICENSE files in each ST submodule.
