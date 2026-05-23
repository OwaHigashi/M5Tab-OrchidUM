# M5Tab-Orchid

Telepathic Instruments の [Orchid](https://telepathicinstruments.com/products/orchid-orc-1) にインスパイアされた M5Stack Tab5 用コードコントローラです。

Tab5 は **MIDI コントローラ専用** で、本体では一切音を生成しません。
すべての音は PortA に接続した外部 M5 Unit MIDI / Unit Synth (SAM2695)
が鳴らします。Tab5 内蔵スピーカーは使用しません。

## ハードウェア

- **ボード**: M5Stack Tab5 (ESP32-P4)
- **画面**: 1280×720 マルチタッチ
- **音源**: M5 Unit MIDI または M5 Unit Synth を **PortA** に接続
  - UART, 31250 baud
  - RX = GPIO54, TX = GPIO53
- **任意**: Unit MIDI の 5-pin DIN 経由で外部 MIDI 機器も接続可能
  (MIDI IN のパースは既定で無効。実 MIDI IN 源を繋ぐ場合は
  `processMIDI()` を再有効化してください。RX フローティング時のノイズが
  Program Change としてフレーム化される事故を防ぐためのデフォルトです)

## コードシステム (Top キー / Bottom キー)

Orchid マニュアル準拠:

- **Top 行 (ラジオ)** — コード BASE
  - Major (R, M3, P5)
  - Minor (R, m3, P5)
  - Sus  (R, P4, P5)
  - Dim  (R, m3, ♭5)
- **Bottom 行 (トグル・併用可)** — モディファイア
  - `6`  → M6 を追加
  - `M7` → M7 を追加
  - `m7` → m7 を追加
  - `9`  → M9 を追加

コード名パネルには Orchid と同じ表記が出ます
(`C`, `Cm7`, `CM79`, `Csusm7`, `Cdim` など)。マニュアル指定の
特殊ラベル(JAZZ / WTF)も再現:

| Base  | Modifiers       | 表示          |
|-------|-----------------|---------------|
| Minor | M7 + m7 + 9     | `CmJAZZ`      |
| Sus   | 6 + m7 + 9      | `CsusJAZZ`    |
| Dim   | 6 + m7 + 9      | `CdimJAZZ`    |
| Dim   | M7 + m7 + 6 + 9 | `CdimWTF`     |

## パフォーマンスモード

**PERF** ボタンで Orchid の 7 種類のパフォーマンスモードを巡回します。
全モードを聴感上明確に区別できるように実装してあります:

| モード             | 種類         | 動作                                            | インターバル        |
|--------------------|--------------|-------------------------------------------------|---------------------|
| Strum              | 一度きり     | 昇順ストラム                                    | 5–200 ms            |
| Strum 2 oct        | 一度きり     | Strum + +12 半音複製で 2 オクターブ範囲         | 5–160 ms            |
| Slop               | 一度きり     | Strum + **±60% タイミングジッタ** + ベロシティ乱数 | 10–220 ms ±jitter |
| Arpeggiator        | ループ       | 押し続けている間、昇順サイクル                  | BPM × Perf          |
| Arpeggiator 2 Oct  | ループ       | 同上を +12 半音範囲も含めて                     | BPM × Perf          |
| Pattern            | ループ       | 固定ステップ列 `{0,2,1,2,0,3,2,1}` を巡回       | BPM × Perf          |
| Harp               | 一度きり     | 4 オクターブ展開 (-1〜+2) を超高速で掃き        | 6–28 ms             |

**Perf スライダ**(スライダ一番上)はモード別の速度/深さを制御します。
手を離しても中央へ戻らず、物理的なツマミと同じ挙動です。

## MIDI ルーティング

| 機能                | 出力                                       |
|---------------------|--------------------------------------------|
| コード音            | Ch 1, 現在の GM プログラム                 |
| ベース音            | Ch 2, GM 38 (Synth Bass 1)                 |
| **PRG- / PRG+**     | Ch 1 への Program Change (GM 0–127)        |
| **Reverb / Chorus** | Ch 1 & Ch 2 への CC 91 / CC 93             |
| **Bend スライダ**   | 14-bit Pitch Bend (`0xE0`)。双極性、離すと中央へ復帰 |
| **BPM**             | 内部 — Arp / Pattern のレート計算          |

GM プログラム名表は M5Tab-MIDIXposeFil の PLAY/SRC モードと同一のものを
インライン化しているので、同じ Program Change 値で機器間の音色が一致します。

## 画面レイアウト

```
+-----------------------------+-----------------------------------------+
|                             |  BPM-  [BPM 120]  BPM+   [プリセット名] |
|  上段: BASE ボタン          |  PRG-  [003  Grand Piano 1]      PRG+   |
|  (Major / Minor / Sus/Dim)  |  [BASS]              [PERF: Strum]      |
|                             +-----------------------------------------+
|                             |  [コード名]            [Perf モード]    |
|  下段: MOD ボタン           +-----------------------------------------+
|  (6 / M7 / m7 / 9)          |                                         |
|                             |          1 オクターブ鍵盤              |
|  ---- スライダ ----         |       (タッチ — 黒鍵が白鍵の上)         |
|  Perf                       |                                         |
|  Bend (離すと中央)          |                                         |
|  Reverb                     |                                         |
|  Chorus                     |                                         |
+-----------------------------+-----------------------------------------+
```

- 本物の Orchid と同じく、左=コードパッド、右=鍵盤の配置。
- コード名パネルと隣の Perf モードパネルが現在の状態を常時表示。
- マルチタッチ: 鍵盤を押し続けたまま、コードボタン・PERF・BPM±・PRG±
  への入力も全て同時に反応します。
- 鍵盤は初期試作より縦幅を縮めて配置(上の方は普通使わないので)。

## ビルド方法

Arduino CLI + **M5Stack** ESP32 コア:

```sh
arduino-cli compile --fqbn m5stack:esp32:m5stack_tab5 M5Tab-Orchid.ino
arduino-cli upload  --fqbn m5stack:esp32:m5stack_tab5 -p COM15 M5Tab-Orchid.ino
```

必須ライブラリ: **M5Unified** (M5GFX を含むので別途インストール不要)。

## ライセンス

MIT
