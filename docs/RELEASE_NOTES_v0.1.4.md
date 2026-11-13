# tiny-window 0.1.4

CYD（ESP32-2432S028）向け「小さな窓」ファームの初回公開です。

## 日本語

- 時間帯は NTP（日本標準時、`ntp.nict.jp` / `pool.ntp.org`）の現地時刻で更新します
- 天気の種類だけ Open-Meteo（API キー不要、約 10 分ごと）から取得します
- 初回は設定用アクセスポイント `TinyWindow-Setup` が出ます。http://192.168.4.1 で家庭の Wi-Fi を入れてください。SSID とパスワードはファームに含まれません
- 書き込みは esptool を使います。ポートは環境に合わせて `/dev/cu.usbserial-XXXX` や `COMx` を指定してください

### 添付ファイル

| ファイル | 用途 |
|---|---|
| `firmware.bin` | アプリ本体。更新時は **0x10000** だけに書くと、保存済みの Wi-Fi 設定が残ります。SHA256 `af32dd660ebc8e9604ed3c0167631d7849539d0c5b8abc7928172d444bca7086` |
| `bootloader.bin` | 0x1000（新品の CYD やパーティション変更時のフル書き込み） |
| `partitions.bin` | 0x8000 |
| `boot_app0.bin` | 0xe000 |
| `merged.bin` | 0x0 からの結合イメージ（フル書き込み。NVS が消えることがあります） |
| `SHA256SUMS` | 上記のチェックサム |

フル書き込みの例と注意（USB 接続中は電源スイッチ OFF、P1 は VIN と GND のみ）はリポジトリの README を見てください。

https://github.com/naotochan/cyd-tiny-window

## English

First public release of the “tiny-window” firmware for the CYD (ESP32-2432S028).

- Time of day is taken from NTP (JST, `ntp.nict.jp` / `pool.ntp.org`)
- Weather type comes from Open-Meteo only (no API key, about every 10 minutes)
- On first setup the board raises a `TinyWindow-Setup` access point. Open http://192.168.4.1 and enter your Wi-Fi credentials. They are not stored in the firmware images
- Flash with esptool. Use `/dev/cu.usbserial-XXXX` on macOS or `COMx` on Windows

### Assets

| File | Use |
|---|---|
| `firmware.bin` | Application image. For updates, write it only at **0x10000** so saved Wi-Fi settings survive. SHA256 `af32dd660ebc8e9604ed3c0167631d7849539d0c5b8abc7928172d444bca7086` |
| `bootloader.bin` | 0x1000 (full flash for a new CYD or partition changes) |
| `partitions.bin` | 0x8000 |
| `boot_app0.bin` | 0xe000 |
| `merged.bin` | Combined image at 0x0 (full flash; may erase NVS) |
| `SHA256SUMS` | Checksums |

See the repository README for full-flash examples and wiring notes (keep the power switch OFF while USB is connected; use only VIN and GND on P1).

https://github.com/naotochan/cyd-tiny-window
