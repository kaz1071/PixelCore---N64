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

**Early development.** The desktop shell, initial ROM-header recognition, starter R4300i CPU, and first physical-memory bus are in place. The emulator cannot run commercial games yet.

- [x] Initial Qt desktop application
- [x] ROM file picker
- [x] Recognize common N64 ROM byte orders (.z64, .v64, and .n64)
- [x] Normalize ROM byte order for the emulation core
- [x] Initial MIPS R4300i CPU core and starter instruction set
- [x] CPU arithmetic, shifts, HI/LO operations, and multiply/divide
- [x] Initial COP0 access, ERET, and basic exception handling
- [x] Byte, halfword, word, doubleword, and unaligned merge memory instructions
- [x] Initial LL/SC load-linked and store-conditional support
- [x] Initial 8 MiB RDRAM, read-only cartridge/boot ROM, 64-byte PIF RAM, and 4 KiB SP DMEM/IMEM mappings
- [x] Connect the R4300i CPU to the memory bus through an initial N64 system core
- [x] Connect desktop ROM loading and normalized cartridge images to the system core
- [x] Initial PIF Joybus controller identify/reset/read command processing
- [x] Initial SI 64-byte DMA transfers between RDRAM and PIF RAM
- [x] Initial MI routing and R4300i handling for SI completion interrupts
- [x] Initial PI DMA for cartridge ROM-to-RDRAM and RDRAM-to-save-RAM transfers
- [x] Initial SP DMA between RDRAM and RSP DMEM/IMEM, including block count and skip
- [x] Initial scalar RSP core with integer arithmetic, branches, delay slots, and DMEM access
- [ ] Complete hardware register models (MI, SI, PI, VI, AI, RI)
- [ ] TLB and complete virtual-memory behavior
- [ ] Reality Co-Processor (RCP), including RSP and RDP
- [ ] Audio processing
- [x] Initial keyboard-to-PIF controller input mapping (game execution not active yet)
- [ ] Save states

CPU instruction coverage is still incomplete. Arithmetic overflow exceptions, full TLB behavior, precise cycle timing, full interrupt coverage, and many coprocessor instructions are not implemented yet. The initial N64 system core owns the CPU and memory bus and can execute test instructions fetched from a mapped boot ROM. The memory bus also models the PIF's 64-byte RAM window and an early Joybus controller path supporting controller identification, reset, and button/stick reads. The initial SI DMA engine transfers the 64-byte command block between RDRAM and PIF RAM and invokes Joybus processing. Transfers are currently synchronous, but completion raises the SI interrupt through MI and the CPU can accept the external IP2 interrupt when enabled. Full asynchronous timing and other RCP interrupt sources remain unimplemented. Keyboard input is now mapped to controller 0's PIF state, but game execution/rendering is not active yet. Accessory protocols, full controller-pak behavior, CIC handling, and authentic IPL boot remain unimplemented. The desktop ROM loader now normalizes .z64, .v64, and .n64 images into canonical big-endian bytes and maps the cartridge into the system core. The initial PI path transfers cartridge data into RDRAM and RDRAM data into an in-memory 128 KiB save-RAM window, and raises the PI interrupt through MI. Save data is not yet persisted to disk. The memory bus maps the RSP's 4 KiB data and instruction memories. The initial SP DMA engine moves data between RDRAM and DMEM/IMEM, including multi-block transfers; it is synchronous. The initial scalar RSP processor executes a starter subset of integer instructions from IMEM and can access DMEM, but vector instructions, COP0, full status integration, and accurate RSP timing are not implemented. Only initial SI and MI register paths are modeled; the remaining hardware devices are not yet implemented. LL/SC and COP0 behavior are early simplified implementations. Features are checked off as they are implemented and tested.

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

ROM selection validates the header, normalizes the complete image to canonical big-endian byte order, and maps it into the system's cartridge region. This is not yet sufficient to boot a commercial game: the PIF boot ROM, hardware initialization, TLB, RCP, and graphics pipeline are still missing.

PixelCore - N64 does not include commercial game ROMs. Use game files that you are legally entitled to use.

## Contributing

The project is in its early stages. Bug reports, ideas, and contributions are welcome as the codebase takes shape.

## License

The project license has not been selected yet.
