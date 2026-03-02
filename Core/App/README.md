# Core/App - FreeRTOS Application Layer

This folder contains the project-level application architecture.
The goal is to keep each hardware/software feature in its own module and keep
`app.c` focused on orchestration.

## 1. High-level architecture

The App layer is split into:

- `app.c` / `app.h`
- feature modules `*_main.c` / `*_main.h`

`app.c` is the coordinator. It:

- creates shared synchronization primitives (for example shared I2C mutex),
- starts feature modules,
- provides the App-level `MX_FREERTOS_Init()` implementation,
- runs a small display aggregation task that combines outputs from sensors.

Feature modules own the logic for one subsystem each:

- BMP280
- ToF (VL53L3CX)
- Radio
- LED array
- LCD

This keeps dependencies local and avoids large monolithic task files.

## 2. File map

### Core orchestrator

- `app.c`
  - App bootstrap (`app_init`)
  - FreeRTOS orchestration (`app_freertos_init`, `MX_FREERTOS_Init`)
  - Shared I2C lock/unlock functions
  - Display aggregation task
- `app.h`
  - Public App-level interfaces for orchestrator and shared locks

### Feature modules

- `bmp280_main.c/.h`
  - Creates BMP280 task
  - Initializes sensor
  - Performs periodic measurements
  - Exposes cached data getter

- `tof_main.c/.h`
  - Creates ToF task
  - Initializes VL53L3CX
  - Performs periodic distance reads
  - Exposes last distance getter

- `radio_main.c/.h`
  - Creates radio task
  - Runs `radio_test_demo_init/process` loop

- `led_array_main.c/.h`
  - Creates LED task
  - Initializes LED driver
  - Starts default rainbow effect
  - Runs periodic LED processing

- `lcd_main.c/.h`
  - **No LCD task and no LCD buffer**
  - Provides thread-safe LCD writes through a mutex
  - LCD writes are immediate in caller context

## 3. Concurrency model

### Shared I2C bus

BMP280 and ToF use the same I2C peripheral. Access is serialized by:

- `app_i2c_lock(timeout_ms)`
- `app_i2c_unlock()`

Any App module touching shared I2C must use this API.

### LCD access

LCD access is protected by an internal LCD mutex in `lcd_main.c`.
This prevents concurrent writes from different tasks.

## 4. LCD design (updated)

Previous approach used an LCD task and internal text buffers.
Current design is intentionally simplified:

- no LCD worker thread,
- no stored framebuffer in `lcd_main`,
- direct write API:
  - `lcd_main_set_line(line, text)`
  - `lcd_main_set_lines(line0, line1)`

Advantages:

- less state,
- easier debugging,
- lower RAM usage,
- no refresh loop and no display-lag caused by queue/buffer handoff.

Tradeoff:

- caller task performs LCD write directly (still mutex-protected).

## 5. Startup flow

1. `main.c` initializes MCU/HAL/peripherals.
2. `main.c` calls `app_init()`.
3. `osKernelInitialize()`.
4. `MX_FREERTOS_Init()` is called.
   - In this project it is implemented in `Core/App/app.c`.
   - It starts App/feature tasks and shared resources.
5. `osKernelStart()`.
6. Scheduler runs tasks.

## 6. Task responsibilities (current)

- Display aggregation task (in `app.c`)
  - Reads latest BMP280 and ToF values
  - Formats compact 2-line status
  - Pushes it to LCD through `lcd_main_set_lines`

- BMP280 task
  - Periodic measurement update

- ToF task
  - Periodic distance update

- Radio task
  - Poll/process radio demo logic

- LED task
  - Maintains LED effects timing

## 7. Coding guidelines for this folder

- Keep each new subsystem in its own `*_main.c/.h`.
- Keep direct hardware calls inside the owning module.
- Keep `app.c` as orchestrator, not as feature-logic dump.
- Guard all shared resources with explicit synchronization.
- Prefer small APIs between modules (getters/setters/commands), not globals.

## 8. Extending with a new feature module

Recommended steps:

1. Add `new_feature_main.h/.c` in `Core/App`.
2. Implement:
   - `new_feature_main_create_task()`
   - internal task function
3. Register task start in `app_freertos_init()`.
4. Add getters/setters needed by aggregator task (if required).
5. Keep shared bus access under existing mutex policy or add dedicated mutex.

## 9. Build / IDE notes

After adding new source files in `Core/App`, STM32CubeIDE may require:

1. Project Refresh
2. Clean
3. Full rebuild

If a source file is not compiled, verify generated build metadata is refreshed.

