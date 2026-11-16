# PixelCore - N64

<p align="center">
  <img src="assets/pixelcore-n64.svg" alt="PixelCore - N64 logo" width="220">
</p>

<p align="center">
  A Linux desktop Nintendo 64 emulator, built as part of the PixelCore emulator family.
</p>

---

## About

**PixelCore - N64** is an open-source project to build a Nintendo 64 emulator for Linux with a friendly desktop application interface.

It is part of the **PixelCore** family of emulators, alongside PixelCore - NES.

## Project status

**Early development.** The desktop shell and initial ROM-header recognition are in place. Actual Nintendo 64 game emulation has not been implemented yet.

- [x] Initial Qt desktop application
- [x] ROM file picker
- [x] Recognize common N64 ROM byte orders (.z64, .v64, and .n64)
- [ ] Normalize ROM byte order for the emulation core
- [x] Initial MIPS R4300i CPU core (registers, fetch/decode, starter instruction set)
- [ ] RDRAM and memory bus (the CPU currently uses a small abstract memory interface)
- [ ] Reality Co-Processor (RCP), including RSP and RDP
- [ ] Audio processing
- [ ] Controller input
- [ ] Save states

Features are checked off as they are implemented and tested.

## Dependencies

- C++20 compiler
- CMake 3.20 or newer
- Qt 6 Widgets

On Ubuntu or Debian-based Linux distributions:

```bash
sudo apt update
sudo apt install build-essential cmake qt6-base-dev
```

## Build

From the repository root:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

Run the application:

```bash
./build/pixelcore-n64
```

## ROM files

The application currently recognizes the standard N64 ROM byte-order signatures:

| Format | Byte order |
| --- | --- |
| .z64 | Big-endian |
| .v64 | Byte-swapped |
| .n64 | Little-endian |

At this stage, ROM selection only inspects the header. It does not execute game code or render gameplay.

PixelCore - N64 does not include commercial game ROMs. Use game files that you are legally entitled to use.

## Contributing

The project is in its early stages. Bug reports, ideas, and contributions are welcome as the codebase takes shape.

## License

The project license has not been selected yet.
