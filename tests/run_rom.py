#!/usr/bin/env python3
"""Run a ROM image (at $A000) plus its RAM image (at $2000) in kim1emu with
an SD card image, until the program finishes or waits for input, and print
its output.  The card image is written back afterwards.

usage: run_rom.py ROM.bin RAM.bin CARD.img [MAX_STEPS]
"""
import sys
from pathlib import Path

WORKSPACE = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(WORKSPACE))
from kim1emu.kim1 import KIM1Emulator  # noqa: E402

rom, ram, card = sys.argv[1:4]
max_steps = int(sys.argv[4]) if len(sys.argv) > 4 else 300_000_000
emu = KIM1Emulator(rom_002=(WORKSPACE / "kim1emu/6530-002.bin").read_bytes(),
                   rom_003=(WORKSPACE / "kim1emu/6530-003.bin").read_bytes())
emu.set_sd_interface("sdcard6502")
emu.io_bridge_mode = "high"
emu.load_sd_image(card)
emu.reset()
emu.load_binary(rom, 0xA000)
if Path(ram).stat().st_size:
    emu.load_binary(ram, 0x2000)
emu.set_pc(0xA000)
steps = emu.run(max_steps=max_steps, stop_on_input_wait=True)
print(emu.drain_output_text().replace("\x00", "").strip())
print(f"[{steps} instructions, pc=${emu.cpu.pc:04X}]")
emu.save_sd_image(card)
