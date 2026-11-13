#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${1:-$ROOT/dist}"
BOOTAPP="${BOOTAPP:-$HOME/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin}"
ESPTOOL="${ESPTOOL:-$(command -v esptool)}"
PROFILES=(cyd_ili9341 cyd_ili9341_inv cyd_ili9342 cyd_st7789_bgr cyd_st7789_rgb cyd_st7789_bgr_inv)

test -f "$BOOTAPP"
"$ESPTOOL" version

rm -rf "$OUT/firmware" "$OUT/source"
mkdir -p "$OUT/firmware" "$OUT/source"

for env in "${PROFILES[@]}"; do
  src="$ROOT/.pio/build/$env"
  dst="$OUT/firmware/$env"
  mkdir -p "$dst"
  cp -a "$src/bootloader.bin" "$src/partitions.bin" "$src/firmware.bin" "$dst/"
  cp -a "$BOOTAPP" "$dst/boot_app0.bin"
  "$ESPTOOL" --chip esp32 merge-bin \
    --output "$dst/merged.bin" \
    --format raw \
    --flash-mode dio \
    --flash-freq 40m \
    --flash-size 4MB \
    --target-offset 0 \
    0x1000 "$dst/bootloader.bin" \
    0x8000 "$dst/partitions.bin" \
    0xe000 "$dst/boot_app0.bin" \
    0x10000 "$dst/firmware.bin"
  cat > "$dst/flash-esptool-5.6.0.txt" <<EOF
# esptool v5.6.0 — ESP32 classic offsets (Arduino-ESP32 2.0.x)
# Port example (macOS): /dev/cu.usbserial-XXXX
# Port example (Windows): COMx
# Chip: ESP32, 4MB flash, DIO 40MHz
#
# Preferred: four files at the usual offsets
esptool --chip esp32 --port /dev/cu.usbserial-XXXX --baud 460800 \\
  --before default-reset --after hard-reset \\
  write-flash --flash-mode dio --flash-freq 40m --flash-size 4MB \\
  0x1000 bootloader.bin \\
  0x8000 partitions.bin \\
  0xe000 boot_app0.bin \\
  0x10000 firmware.bin

# Recommended update (keeps NVS Wi-Fi / last weather): app only
esptool --chip esp32 --port /dev/cu.usbserial-XXXX --baud 460800 \\
  --before default-reset --after hard-reset \\
  write-flash --flash-mode dio --flash-freq 40m --flash-size 4MB \\
  0x10000 firmware.bin

# Alternative: merged image starts at 0x0 (0xFF padded through 0x0FFF). May wipe NVS.
esptool --chip esp32 --port /dev/cu.usbserial-XXXX --baud 460800 \\
  --before default-reset --after hard-reset \\
  write-flash --flash-mode dio --flash-freq 40m --flash-size 4MB \\
  0x0 merged.bin
EOF
  (
    cd "$dst"
    sha256sum bootloader.bin partitions.bin boot_app0.bin firmware.bin merged.bin > SHA256SUMS
  )
done

cp -a "$ROOT/README.md" "$OUT/README.md"
cp -a "$ROOT/partitions/esp32_4mb.csv" "$OUT/esp32_4mb.csv"
cp -a "$ROOT/requirements-host.txt" "$OUT/requirements-host.txt"

ZIP="$OUT/source/tiny-window-0.1.4-src.zip"
rm -f "$ZIP"
(
  cd "$ROOT"
  zip -r "$ZIP" \
    platformio.ini requirements-host.txt partitions include src scripts reference \
    README.md .gitignore \
    -x '*.DS_Store'
)

(
  cd "$OUT"
  find firmware source README.md esp32_4mb.csv requirements-host.txt -type f | sort | xargs sha256sum > SHA256SUMS
)

echo "packaged -> $OUT"
find "$OUT" -type f | sort
