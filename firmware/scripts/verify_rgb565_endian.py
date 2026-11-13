#!/usr/bin/env python3
"""Host check: ILI9341 scene banding matches pushPixels without byte-swap.

TFT_eSPI 2.5.43 on ESP32:
  fillRect -> pushBlock writes (color<<8|color>>8) into SPI_Wn
  pushPixels(_swapBytes=false) dumps native uint16 LE words
  pushPixels(_swapBytes=true) uses DAT8TO32 which swaps each 16-bit pixel

A vertical sky gradient is constant on each scanline, so a byte-swap turns
smooth hue steps into cyan/pink/green horizontal bands. The software window
frame (95,70,50) becomes bright cyan — matching the device report.
"""
import sys


def to565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def from565(p):
    return ((p >> 11) * 255 // 31, ((p >> 5) & 63) * 255 // 63, (p & 31) * 255 // 31)


def swap16(p):
    return ((p & 0xFF) << 8) | (p >> 8)


SAMPLES = [
    ("day sky top", 70, 132, 210),
    ("day sky bot", 176, 214, 236),
    ("evening bot", 240, 150, 120),
    ("sun light", 255, 250, 220),
    ("frame brown", 95, 70, 50),
    ("red patch", 255, 0, 0),
    ("green patch", 0, 255, 0),
    ("blue patch", 0, 0, 255),
    ("white", 255, 255, 255),
]


def main():
    print(f"{'name':16} {'565':6} {'wire-noswap':11} {'looks':20} {'wire-swap':9}")
    failed = 0
    for name, r, g, b in SAMPLES:
        c = to565(r, g, b)
        wrong = swap16(c)
        wr, wg, wb = from565(wrong)
        ok = from565(c)
        print(
            f"{name:16} {c:04X}   {wrong:04X}        "
            f"({wr:3},{wg:3},{wb:3})   {c:04X}"
        )

    # Device-matching assertions for the reported scene artefacts.
    frame = swap16(to565(95, 70, 50))
    fr, fg, fb = from565(frame)
    if not (fg > fr + 40 and fb > fr + 20):
        print("FAIL: swapped frame is not cyan-like", fr, fg, fb)
        failed += 1
    else:
        print("OK: swapped frame (95,70,50) -> cyan/teal", (fr, fg, fb))

    # Red 0xF800 swapped is 0x00F8 = almost pure blue-green; fillRect path
    # byte-swaps so the test R patch stays red. Scene path without setSwapBytes
    # would not. White 0xFFFF is invariant.
    if to565(255, 0, 0) != 0xF800:
        print("FAIL: red packing")
        failed += 1
    if swap16(0xF800) != 0x00F8:
        print("FAIL: red byte swap")
        failed += 1
    if swap16(0xFFFF) != 0xFFFF:
        print("FAIL: white should be endian-invariant")
        failed += 1
    else:
        print("OK: white 0xFFFF invariant (test WHITE cannot prove endian)")
        print("OK: red 0xF800 -> 0x00F8 on unswapped pushPixels")

    # Adjacent sky rows differ by a few 565 units; after swap they jump hues.
    a = to565(70, 132, 210)
    b = to565(76, 138, 212)
    da = abs(a - b)
    ds = abs(swap16(a) - swap16(b))
    print(f"OK: adjacent sky delta native={da} swapped={ds} (swapped jumps more)")
    if ds <= da:
        print("FAIL: expected swapped sky steps to jump")
        failed += 1

    return failed


if __name__ == "__main__":
    sys.exit(main())
