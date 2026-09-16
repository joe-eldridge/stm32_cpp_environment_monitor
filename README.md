# STM32 Low-Power Environment Monitor

[![CI](https://github.com/joe-eldridge/stm32_cpp_environment_monitor/actions/workflows/ci.yml/badge.svg)](https://github.com/joe-eldridge/stm32_cpp_environment_monitor/actions/workflows/ci.yml)

A battery-oriented environment monitor and data logger for industrial settings, built on an STM32L073RZ. It spends almost all of its time in Stop mode. An external RTC wakes it on a schedule to sample temperature, pressure, humidity and light, append a timestamped row to an SD card, and go back to sleep.

The application code is written in C++17, without heap allocation, exceptions or RTTI. The sensor drivers are hardware-independent, so they are unit-tested on a host PC, and CI checks every push.

## Hardware

| Part | Role | Interface |
|---|---|---|
| NUCLEO-L073RZ (STM32L073RZ, Cortex-M0+, 192 KB flash, 20 KB RAM) | MCU | — |
| DS3231 RTC (HW-084 board) | Timekeeping and wake-up alarm | I2C `0x68`, alarm on SQW/INT |
| Bosch BME280 (Adafruit board) | Temperature, pressure, humidity | I2C `0x77` |
| Vishay VEML7700 (Adafruit board) | Ambient light | I2C `0x10` |
| Adafruit 1.54" eInk (SSD1681, 200×200, original revision) with microSD slot and SPI SRAM | Display *(in progress)* and log storage | SPI |

### Pin assignments

| Signal | Pin | Notes |
|---|---|---|
| I2C1 SCL / SDA | PB8 / PB9 | Shared by the RTC and both sensors |
| DS3231 SQW/INT | PC0 | Falling-edge EXTI wakes the MCU from Stop mode; internal pull-up |
| SPI1 SCK / MISO / MOSI | PB3 / PA6 / PA7 | Shared by the SD card, eInk display and SRAM |
| SD card CS | PA10 | |
| eInk CS / D/C / RST / BUSY | PA8 / PB10 / PB4 / PB5 | |
| SRAM CS | PC7 | |
| USART2 TX / RX | PA2 / PA3 | ST-LINK virtual COM port, 9600 baud |
| User button B1 | PC13 | Held at reset: I2C fault-injection test (see below) |

## How it works

```mermaid
%%{init: {"sequence": {"mirrorActors": false}}}%%
sequenceDiagram
    participant RTC as DS3231
    participant MCU as STM32L073
    participant S as BME280 / VEML7700
    participant SD as SD card
    MCU->>RTC: Arm alarm for next interval
    Note over MCU: Stop mode (low-power regulator)
    RTC-->>MCU: SQW/INT falls → EXTI wake
    MCU->>MCU: Restore clocks, clear alarm flag
    MCU->>S: Power up, take one measurement, power down
    MCU->>SD: Append CSV row (timestamped from the RTC)
    MCU->>RTC: Arm next alarm
```

- **Wake source:** DS3231 Alarm 1, matched on minutes and seconds, re-armed after every wake. The interval must divide an hour evenly, which is checked at compile time.
- **Sensors sleep between samples.** The BME280 runs in forced mode, taking one conversion on request (about 10 ms) and otherwise sleeping. The VEML7700 is held in shutdown except for the roughly 115 ms needed for one reading. The datasheets give about 0.1 µA and 0.5 µA for those sleep states, against hundreds of µA and 45 µA left running.
- **Log format:** `LOG.CSV` holds one row per wake. Values are fixed-point integers, since the Cortex-M0+ has no FPU. For example:

  ```
  Timestamp,TemperatureCentiC,PressureCentiHpa,HumidityCentiPct,LuxCenti,LuxSaturated
  2026-09-16 19:45:00,2352,101695,5054,37645,0
  ```

  A failed sensor read leaves its fields empty rather than stopping the log.
- **Setting the time:** if the DS3231 reports that its oscillator has stopped (for example, a flat backup battery), the firmware prompts over the serial port for `YYYY-MM-DD HH:MM:SS` before starting.

## Design notes

**Driver architecture.** Sensor drivers depend on a small `I2cDevice` interface ([i2c_device.hpp](Drivers/BSP/Components/util/i2c_device.hpp)) rather than on the STM32 peripheral. On the target, `I2cBusDevice` binds a slave address to an `I2cBus` driven through ST's LL API. In tests, a fake device stands in. `HAL_GetTick` and `HAL_Delay` are linked in as seams, so timeouts and waits are deterministic on the host.

**HAL for setup, LL for the busy paths.** CubeMX-generated HAL code configures the system, while the I2C and SPI transfers use the LL API directly to keep awake time short.

**Robustness.**
- **Bounded waits:** every busy-wait is limited by a `Timeout` that stays correct when the 32-bit tick counter wraps (after about 49 days).
- **I2C bus recovery:** on a timeout, the I2C peripheral is reset. If a slave is still holding SDA low, for example after an MCU reset mid-read, the driver clocks it free and sends a STOP. The same recovery runs at every boot.
- **SD card swaps:** the card can be removed and reinserted while running. A failed transfer marks the card uninitialised, and the next write re-initialises and re-mounts it. This needed a workaround for ST's FatFs glue, which caches `disk_initialize()` even when it fails; see [user_diskio.c](FATFS/Target/user_diskio.c).
- **Shared SPI bus:** scope guards (RAII) always restore the bus speed and release chip-select, even on error paths.

**Sensor accuracy.**
- **BME280:** uses Bosch's integer compensation formulas, with signed calibration values handled as in Bosch's reference driver.
- **VEML7700:** uses the 0.0672 lx/count resolution from Vishay's current application note ([84323, rev. 06-Mar-2025](https://www.vishay.com/docs/84323/designingveml7700.pdf)); the original datasheet value reads about 14% low. Readings at full scale are flagged as saturated.

## Testing

### Host unit tests

The same driver sources the firmware uses are compiled for the host and tested with GoogleTest, with AddressSanitizer and UndefinedBehaviorSanitizer enabled:

```bash
cmake -S tests -B build/tests
cmake --build build/tests
ctest --test-dir build/tests --output-on-failure
```

Coverage includes:
- the BME280 against Bosch's worked example, and against the datasheet's floating-point formulas across several calibrations,
- DS3231 alarm scheduling at interval boundaries, including the window where an alarm write could land too late,
- VEML7700 power sequencing, scaling and saturation,
- SD card capacity parsing from the CSD register,
- FatFs timestamp packing,
- timeout behaviour across tick wraparound.

### On-target fault injection

Hold **B1** while pressing reset. The firmware bit-bangs the start of a BME280 read and stops mid-byte, leaving the sensor holding SDA low. It then resets itself, and the next boot must recover the bus:

```
B1 held - wedging I2C bus: SDA held low, resetting
I2C: SDA held low at boot - recovery succeeded
```

See [i2c_fault_injection.cpp](Core/Src/i2c_fault_injection.cpp).

### CI

[GitHub Actions](.github/workflows/ci.yml) runs the unit tests with GCC and Clang, and builds the Debug and Release firmware with the Arm GNU toolchain. The resulting `.elf` and `.map` files are saved with each run.

## Building and flashing

The project was generated with STM32CubeMX and builds with CMake and Ninja.

**VS Code:** open the folder with the STM32Cube for Visual Studio Code extension installed, select the `Debug` or `Release` preset, then build and flash through the extension.

**Command line:** with `arm-none-eabi-gcc` on your `PATH`:

```bash
cmake --preset Release
cmake --build --preset Release
```

The output is `build/Release/stm32_cpp_environment_monitor.elf`. Open the ST-LINK virtual COM port at 9600 baud to see diagnostics.

## Project layout

```
Core/                     Application (app.cpp), CubeMX init code, fault-injection hook
Drivers/BSP/Components/   Project drivers: bme280, ds3231, veml7700, sdcard, util (buses, timing)
Drivers/CMSIS, Drivers/STM32L0xx_HAL_Driver   ST vendor code
FATFS/, Middlewares/      FatFs and its glue to the SD card driver
tests/                    Host unit tests and fakes
.github/workflows/        CI
```

## Status and roadmap

- [x] Stop-mode sleep with RTC alarm wake
- [x] BME280, VEML7700 and DS3231 drivers with host unit tests
- [x] SD card logging with removal and reinsertion recovery
- [x] I2C bus recovery with on-target fault injection
- [x] CI: unit tests and firmware builds
- [ ] SSD1681 eInk driver: hourly averages, trend plot, button menu (set time, eject and format the SD card)
- [ ] Current-consumption measurements and battery-life estimate
- [ ] Production wake interval: currently 1 minute for testing, with 5 minutes planned
- [ ] Hardware changes to the HW-084 RTC board (power LED, charging circuit)

## License

Project code is released under the [MIT License](LICENSE). Third-party code keeps its own licence: ST's HAL and CMSIS files include theirs in their folders, and FatFs's is in its source file headers.
