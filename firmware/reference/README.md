# Pixel Window v0.2.0（ソフト）

霞む山並みと湖を、時間帯と天気で移ろわせるアンビエントなドット絵（家なし）。すべてコード描画。
内部 160×120 → 整数倍拡大（ニアレスト）、既定 15fps。背景・雲層は時間帯×天気ごとに一度だけ描いてキャッシュ（初代 Zero WH を意識）。

## 天気の見分け（色だけに頼らない）
- **晴れ**: 太陽/月の円盤と光輪、斜めの光の筋、水面の光の道ときらめき、薄い筋雲
- **曇り**: 空を覆う厚い雲の帯（上縁が明るい拡散光）、円盤なし、静かな水面
- **雨**: 暗い低い雲、斜めに降る雨筋、水面に広がる波紋
- 星や流れ星など、雨と紛らわしい点の動きは入れていません

**天気は手動デモのみ。実際の天気・現在地・ネットAPIとは連動しません（未実装）。** オフラインで動作。

## 操作
| キー | 動作 |
|---|---|
| 1/2/3/4 | 朝/昼/夕/夜に固定 |
| A | 時刻連動（5–9朝/9–16昼/16–19夕/他 夜） |
| W | 天気を 晴→曇→雨 と切替 |
| S / C / R | 晴れ / 曇り / 雨⇔晴れ |
| F | 全画面切替 |
| Esc / Q | 終了 |

オプション: `--weather clear|cloudy|rain`（`--rain` は互換）/ `--phase` / `--scale` / `--fps` / `--snapshot out.png` / `--no-frame` / `--fit cover` / `--rotate180`
筐体(hardware v0.2.x)用: `python3 pixel_window.py --fullscreen --no-frame --fit cover --rotate180`

## 導入（Raspberry Pi OS 32bit）
```bash
sudo apt update && sudo apt install -y python3-pygame
python3 pixel_window.py
```
PC でも `pip install -r requirements.txt` で試せます。

## 注意
- 実機（Pi Zero WH）での動作・速度は未検証（開発PCでのヘッドレス描画テストのみ）
- ESP32 などマイコンではこの Python コードはそのまま動きません（移植が必要）

## 変更履歴
- v0.2.0: 景観をアンビエントな湖景に刷新。天気を 晴/曇/雨 の3状態に（W/S/C キー、`--weather`）
- v0.1.2: `--no-frame` / `--fit cover` / `--rotate180`
- v0.1.1: 家を削除
