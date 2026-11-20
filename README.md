# STM32 firmware bootloader with fail-safe updates

I started this project with a simple question. What happens if a board loses power halfway through an update, or if the new firmware starts and then gets stuck? This bootloader keeps the last confirmed application in flash while it installs a new one. The new application gets up to three tries to start and confirm itself. If it keeps failing, the bootloader goes back to the confirmed one.

The target is an **NUCLEO-F767ZI** with an **STM32F767ZI** Cortex-M7. The code includes a bootloader in C, a Python serial updater, and small LED applications for showing a successful update and a rollback.

## How it works

1. At reset, the bootloader checks the flash layout and reads the latest complete boot record.
2. The Python tool sends a packaged image over USART3 through the ST-LINK virtual COM port. The bootloader erases and programs only the destination application slot.
3. The bootloader checks the image header, the application vectors and the SHA-256 digest of the flashed payload. It marks the image pending only after the checks pass.
4. On the next reset, it saves the attempt count, starts the independent watchdog and hands control to the pending image.
5. A healthy demo records its confirmation after setting up its LED. The failure demo hangs before confirmation. If a pending image is invalid or fails three starts, the bootloader tries the confirmed slot. It stays in UART recovery when there is no valid boot candidate.

Both slots are regions in **one flash bank**. This build uses the reset 16 MHz HSI clock and performs updates before starting any application. The chip supports a faster clock and a dual-bank configuration, which would require revisiting timing and erase geometry.

| Flash region | Range | Size |
| --- | --- | ---: |
| Bootloader | `0x08000000–0x0801FFFF` | 128 KiB |
| Primary journal | `0x08020000–0x0803FFFF` | 128 KiB |
| Slot A | `0x08040000–0x080FFFFF` | 768 KiB |
| Slot B | `0x08100000–0x081BFFFF` | 768 KiB |
| Backup journal | `0x081C0000–0x081FFFFF` | 256 KiB |

The first 512 bytes in each slot hold the image header. The application vector table starts at `0x08040200` in A and `0x08100200` in B. Build a separate binary for each address. Each slot can hold up to 785,920 bytes of application data.

## Build and package

Install GNU Make, the Arm GNU Toolchain with `arm-none-eabi-gcc` and binutils, and Python 3. Install pyserial in a virtual environment for the serial commands.

```sh
make
python3 -m venv .venv
. .venv/bin/activate
python -m pip install -r host/requirements.txt

python host/fw_tool.py package --slot a --version 1 build/app-a.bin build/app-a.img
python host/fw_tool.py package --slot b --version 2 build/app-b.bin build/app-b.img
python host/fw_tool.py package --slot b --version 3 build/app-b-fail.bin build/app-b-fail.img
```

## Run on a Nucleo board

1. Connect the ST-LINK USB port and open STM32CubeProgrammer. Confirm the option bytes show single-bank flash (`nDBANK=1`) and software-controlled IWDG (`IWDG_SW=1`). The layout depends on those settings. The walkthrough assumes a development board with empty journal sectors. Back up any existing firmware and data before overwriting their sectors.
2. Program `build/boot.bin` at `0x08000000` through ST-LINK/SWD. Check that the board boots from internal flash, then reset. A fresh board enters recovery because there is no confirmed application.
3. Find the ST-LINK virtual COM port. Replace `/dev/ttyACM0` below with your port, for example `COM5` on Windows. The serial settings are 115200 baud, 8 data bits, no parity and 1 stop bit.
4. Install A. When its green LED turns on, hold the blue user button during a reset to enter recovery. Release it before issuing an update that reboots the board.

```sh
python host/fw_tool.py info --port /dev/ttyACM0
python host/fw_tool.py update --port /dev/ttyACM0 --slot a build/app-a.img
```

5. With A confirmed, install the deliberately failing B build. Expect the red LED during each trial and a return to the green LED after rollback. The watchdog interval depends on the board’s low-speed oscillator.

```sh
python host/fw_tool.py update --port /dev/ttyACM0 --slot b build/app-b-fail.img
```

6. Enter recovery again and install the healthy B image. A steady red LED is expected after startup. Use `info` in recovery to check that B is confirmed and the pending slot is empty. `log` shows the journal generation and current trial count.

```sh
python host/fw_tool.py update --port /dev/ttyACM0 --slot b build/app-b.img
```

## Check the code

```sh
make test          # Python packaging and serial protocol tests
make test-native   # Linux C tests with a simulated flash region
```

The Arm build produced the bootloader and all three demo binaries. The Python tests and native C checks passed. The native checks exercise hashes, image validation, journal rotation, corrupted records and an interrupted record commit. Flash erase timing, USART3 on the board, watchdog resets and physical power-loss recovery still need bench testing.

- [Architecture, flash layout and boot state](docs/architecture.md)
- [UART protocol and image format](docs/protocol.md)
- [Bench test plan and measurements](docs/test-plan.md)

CRC32 and SHA-256 check for corruption. This build accepts unsigned updates and assumes a trusted serial connection and host. Firmware signing and a FreeRTOS health-check demo are follow-up work.

[Portfolio project page](https://adityavmvs.github.io/projects/stm32-bootloader.html)
