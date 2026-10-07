# Tera BLE MIDI — ESP32-SOLO-1 UART ⇄ BLE MIDI

ESP-IDF v6.1 / C / BLE Peripheral専用の新規プロジェクトです。既存のBLE-MIDItoDINとは別のファームウェアです。
通常利用は3.3V・GND・RX・TXの4線だけでも動作します。v1.1.0で任意接続のステータスLEDを追加しました。OLED、ボタン、ASCII設定コマンド、Wi-Fi、Central、スキャン、OTAは実装していません。

## v1.2.0：MIDIによる名前変更

初期BLE名を **ESP MIDI** に変更しました。UARTまたはBLE MIDIから専用SysExで名前を変更し、本体NVSへ保存できます。MIDI送信を止めて3秒以上待ってから再起動すると新しい名前を使います。1～29バイトの表示可能なASCIIに対応。同じ名前ではFlashを書き換えません。通常のMIDIと他機器向けSysExは転送します。

[名前変更コマンドと送信例](NAME-SETTING.md)。mi.1のメーカー専用コマンドとの互換性はありません。以下のv1.1.1の遅延値は変更前の測定値で、v1.2.0の遅延を再測定した値ではありません。

## v1.1.1：実機確認・低遅延修正

UART FIFOの空き容量の扱いを修正し、7.5ms接続成功後に予備要求で15msへ変わる不具合を解消しました。接続中はControllerのmodem sleepを止め、切断時に省電力Advertisingへ戻します。未接続時のFAST/SLOW Advertising、DFS、tickless idleは維持します。接続中の電流は未測定です。

SOLO-1とWROOM-32で各1,000件を実測し、メッセージ完成→受信側UART FIFO投入の平均は15ms接続時10.877msから7.5ms版6.538msへ短縮、95%点9.637ms、欠落0でした。Notify要求まで平均93µs、API内部平均414µsです。API復帰までの合計は約507µsで、500µs未満を厳密には達成と判定していません。UART線上の送信時間は別です。[詳細と条件](latency-report.md)を参照してください。通常版には自動試験送信・UARTループバック・GPIO5測定出力はありません。

v1.1.0のZIPは履歴用です。UART送信不具合を修正したv1.1.1以降を使用してください。

## v1.1.0：LED表示

初期設定は **GPIO25 → 1kΩ抵抗 → LEDのアノード（＋）、LEDのカソード（－）→ GND** です。GPIO25はUARTのGPIO16/17とは別です。赤色LEDなら点灯時の電流は概ね1～2mAですが、実際はLEDの順方向電圧によります。LEDなしでも動作します。

| 状態・方向 | LEDのパターン |
|---|---|
| 接続待機（FAST/SLOW Advertising共通） | 2秒周期、100msだけ点灯 |
| BLE接続済み・HostのNotification購読待ち | 600ms周期、100msだけ点灯 |
| 接続・Notification購読完了、通信なし | 常時点灯 |
| UART RX → BLE送信 | 常時点灯から75msだけ消灯、1回 |
| BLE受信 → UART TX | 75ms消灯・75ms点灯を2回 |
| 両方向の通信を同じ表示期間内に検出 | 同パターンを3回 |

連続演奏・SysExでは表示をまとめ、全メッセージを1個ずつ点滅させません。各パターン間に300ms以上の点灯を挟み、連続通信でも1回/2回/3回を識別できるようにします。送信表示はGATT Notify要求が受理されたこと、受信表示はデコードしたMIDIをUART送信キューが受理したことを示します。相手機器への無線到達・実際のUART線上での送信完了を保証する表示ではありません。不正データや送信失敗で転送が発生しなかった場合、未接続時の破棄では通信表示を行いません。パケットの正常な部分が先に転送された場合は、その転送を表示します。

`idf.py menuconfig` → `Tera BLE MIDI` で有効/無効、GPIO、LOW点灯を設定できます。周期は `main/config.h` の `LED_*_MS` で変更できます。Flash用GPIO6～11、UART GPIO16/17、ログUART GPIO1/3との重複は拒否します。起動ストラップに影響しないGPIO25を推奨します。

低優先度の静的タスクが既存状態と通信カウンタを観察してLEDを操作します。UART ISR内のLED処理はありません。BLE/MIDIの経路に追加したのは成功時のカウンタ加算のみで、GPIO操作・delay・ログ・動的メモリ確保はありません。LED応答は通常最大約100ms＋タスク待ち、パターン中は25ms刻みです。表示のための周期起床とLEDの点灯電流が増えるので、従来の消費電力見積もりはLED無効時のものとして扱ってください。ESP32同士の遅延実測を追加しました。LEDの物理表示確認・電流測定は未実施です。

ビルド対象はTera BLE MIDIです。既存のBLE-MIDItoDIN（OLED付き）にはこのLED変更を適用していません。

## 接続

| モジュール端子 | ESP32 GPIO | 接続先 |
|---|---|---|
| 3V3 | 3V3 | 安定した3.3V電源 |
| GND | GND | メインMCUと共通GND |
| RX | GPIO16 | メインMCU TX |
| TX | GPIO17 | メインMCU RX |

3.3V CMOS UART / 8N1。DIN端子への直結用回路ではありません。RX/TXはモジュールから見た名称です。
BLE側の初期名は **ESP MIDI**。NVSに保存された名前があればそちらを使用します。接続はiOS/macOS等のBLE MIDI Host側から行います。
GPIO・名前は `idf.py menuconfig` → Tera BLE MIDI、Baud候補と判定条件は `main/config.h` で変更できます。

4線だけで使用できるように、基板内部にはENのプルアップ・起動用RC回路・電源デカップリングを実装してください。
EN→3.3V 10kΩ、EN→GND 1µF、GPIO0→3.3V 10kΩ、電源近傍に10µF＋0.1µFを基本とします。
GPIO12を起動時HIGHにしないでください。ESP32書き込み用GPIO0/EN/UART0は製造・開発用パッドにでき、製品の外部端子にする必要はありません。
UART0のROM起動メッセージはGPIO1へ出る場合がありますが、製品のMIDI TXはGPIO17なので混入しません。
入力専用GPIO34以上をRXに選ぶ場合は、内部プルアップがないため外付けプルアップが必要です。GPIO6～11はFlash用なので使わないでください。

## 設計と実装

### 1. ファイル構成

`main/`:

| ファイル | 責務 |
|---|---|
| main.c | NVS→UART→BLEの自動起動 |
| config.h / Kconfig.projbuild | コンパイル時設定 |
| midi_parser.c/h | UART MIDIバイトストリーム解析 |
| ble_codec.c/h | BLE MIDIパケットencode/decode |
| baud_detect.c/h | MIDIの成立性とUARTエラーからBaud判定 |
| uart_midi.c/h | UART1設定、RX/TX FIFO割り込み、固定Ring Buffer |
| ble_midi.c/h | 標準GATT Service、Notify、Advertising、接続パラメータ要求 |
| config_nvs.c/h | Baud・名前の読込、変更時の遅延保存 |
| name_command.c/h | 名前変更SysExの識別、通常SysExの透過転送 |
| power_manager.c/h | DFS・CPUの処理中ロック |
| debug.c / runtime.h | 開発ビルドの状態・エラーカウンタ |

`tests/`にホストで動くCテストと、8N1波形のサンプリングを模擬するBaudテストがあります。

### 2. ESP-IDF version

実際にインストールされている **ESP-IDF v6.1** を使用しています。ターゲットはesp32、FreeRTOSはUnicoreです。
ESP32-SOLO-1にNimBLE Peripheralを使用できます。Arduinoは使用していません。
調査資料: [v6.1 release](https://github.com/espressif/esp-idf/releases/tag/v6.1)、[SOLO-1 datasheet](https://documentation.espressif.com/esp32-solo-1_datasheet_en.html)。

### 3. ESP-IDF標準component

明示依存: bt、nvs_flash、esp_driver_uart、esp_driver_gpio、esp_timer、esp_pm、esp_system、esp_hal_uart。
FreeRTOS等はESP-IDFの基盤依存です。UART設定には標準driver、FIFO処理には同梱のESP32 UART LL APIを使っています。
UART driverのISRと自前ISRは併用しません。`uart_driver_install()` は呼んでいません。
btのSDK依存としてesp_wifi/esp_coex/esp_phyや関連componentはビルドへ含まれます。SDKを改変して削除することはせず、Wi-Fiを初期化・起動するコードはありません。
Release ELFのsymbol検査でも `esp_wifi_init` / `esp_wifi_start` は含まれていません。

### 4–5. 外部ライブラリ

追加した外部ライブラリは **0**。NimBLEはESP-IDF同梱スタックです。Pythonの波形テストは開発用で、ファームウェアの依存にはなりません。

### 6. BLE MIDI実装

自作GATT Service:

- Service UUID: `03B80E5A-EDE8-4B33-A751-6CE34EC4C700`
- Characteristic UUID: `7772E5DB-3868-4112-A1A9-F2669D106BF3`
- Read（空payload）/ Write / Write Without Response / Notify
- CCCDはNimBLEが管理。接続は1台、MTUは最大185、PHYはESP32のBLE 4.2で利用できる1Mです。

13bitミリ秒timestamp、packet内のtimestamp Lowのwrap、packet内Running Status、Realtime、packetをまたぐSysExを自前で処理します。
BLEではSystem Common後もRunning Statusが残る規則に対応しています。UARTではSystem CommonでRunning Statusを解除します。
UART送信側timestampはメッセージ完成時に生成するので、途中Realtimeの送信でtimestampが逆行しません。受信timestampによる待機は行いません。
暗号化の要求に応じるJust Works対応、bonding/Host記憶はしません。暗号化必須にはしていません。
参照: [MIDI Association BLE MIDI 1.0](https://midi.org/midi-over-bluetooth-low-energy-ble-midi)。

### 7. MIDI parser

Channel Voice全種、F1/F2/F3/F6、定義済みRealtime、Running Status、長いSysExに対応。
固定16byte SysEx作業領域を使い、UARTから取り出した分を即flushします。SysEx全体をRAMへ保持しません。
Realtimeを途中で検出した場合は独立イベントとして出し、未完成のChannel Messageを保ちます。
SysEx中の別Statusや不正BLE packetでは、既に送信したSysExをEOXで終了させてparserを復旧します。

### Critical path

```text
UART RX FIFO
  → IRAM ISRが固定RX Ringへコピー・タスク通知
  → MIDI parser（メッセージ完成）
  → BLE encoder
  → 固定Notify Queue・NimBLE host event
  → 固定MSYS mbuf poolから取得
  → GATT Notify要求
```

数ms待つbatch timerはありません。FIFO RX閾値は1byte。UART RXタスクの優先度は20、NimBLE hostはSDK既定の21です。
Notify QueueはHost側でのみ消費し、接続世代を照合します。未接続/未購読データと、30msを超えた滞留データは破棄します。
標準スタック内部の同期・イベントキューや無線送信待ちは存在し、ハードリアルタイムを保証する設計ではありません。

```text
GATT Write受信
  → 固定182byte領域へコピー
  → BLE decoder
  → UART TX固定Ringへ投入
  → 空きのあるhardware FIFOへ即投入
  → 残りはTX FIFO割り込みで送り出す
```

UART FIFO待ちのbusy wait、周期polling、callback内NVS、ログ、heap確保はありません。
NimBLEのmbuf APIは事前確保したプールを使います。スタックやドライバの初期化時には通常のメモリ確保があります。
UART TXの混雑時はFIFOへ入るまでの待ち時間が増えます。これはRing→FIFOの送出待ちであり、callbackで待ちません。

### 8. Auto Baud

候補順: **31250 → 115200 → 230400 → 38400 → 57600**。
保存値が有効なら即使用、なければ31250。無通信では変更しません。
現在値でframing/parity/break errorが4件、または128byte以上でMIDI成立性が悪い場合に探索します。
候補ごとにChannel/System Common/完結SysExを6点、Realtimeを1点、不正解析を−5点、UART errorを−12点で評価します。
採用条件: 有効メッセージ12以上、構造のあるメッセージ4以上、score 48以上、有効数が（不正数＋UART error＋1）の8倍以上。
誤りが多ければ即次候補へ、それ以外はactivityのある試行で250msを経過した時に次候補へ進みます。無信号なら探索中も候補変更を止めます。
正常なRunning Statusを評価窓の切り替えで失わないようにしています。
探索中のUART受信データはBLEへ送りません。Baud変更時は古いUART TX/RXを破棄します。

### 9. NVS

namespace `tera_uart`、`version=1`、`baud`、任意の`name`を保存。接続先・演奏データは保存しません。
正常と判断したBaudだけを保存し、同値なら書きません。破損・不正version・候補外の値は31250へ戻ります。
BaudのFlash書き込みは低優先度タスクで、**BLE未接続かつ双方向activityが2秒ない時**まで延期します。明示的に要求した名前変更の保存は、接続中でも2秒の無通信後に行います。新しい名前は再起動後に反映します。
そのため、接続中にBaudが変わってすぐ電源を切ると保存が間に合いません。演奏中のFlash停止を避けるための選択です。
NVSが初期化できない場合もデフォルトで動きますが、保存はできません。

### 10. 省電力

未接続時はBluetooth modem sleep（ORIG）を使用。接続中は初回演奏の遅延を抑えるためController sleepを無効にします。main 40MHz XTAL、DFS 40～240MHz、tickless idleを使用。
MIDIを処理している間はCPU MAX lockを取得し、処理が終われば解除します。
UARTはREF_TICK clockで動かし、DFSでBaudが変わらないようにしています。
**Automatic Light Sleep / Deep Sleepは無効**。外部32kHz発振子なしのESP32 BLE制限と、UART wakeup時の先頭文字消失を避けます。
参照: [Power Management](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32/api-reference/system/power_management.html)、[UART API](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32/api-reference/peripherals/uart.html)、[NVS](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32/api-reference/storage/nvs_flash.html)。

| 状態 | 戦略 |
|---|---|
| ACTIVE | CPU MAX lock、即Notify/FIFO投入、Controller sleep無効 |
| CONNECTED IDLE | 低CI・Latency 0を維持、処理タスクはblock、CPU DFS、Controller sleep無効 |
| FAST ADVERTISING | 30ms Advertising、無通信の処理タスクはblock、DFS |
| SLOW ADVERTISING | 750ms Advertising、DFSとmodem sleep |

### 11. Advertising・接続パラメータ

起動・切断後は30msで15秒、その後750ms。接続中はAdvertising OFF。
接続失敗時・切断時もPeripheral Advertisingへ戻るだけです。Hostへの能動接続・探索はありません。
UART activityだけではFASTへ戻しません。Clock/Active Sensingで広告を頻繁に切り替えることを避けます。
接続時にCI 7.5ms / Latency 0 / Supervision Timeout 1秒を要求。
1秒後、既に7.5ms / Latency 0なら追加要求を省略します。それ以外の場合の1回だけの予備要求では7.5～15msを許容します。Hostが7.5ms固定を拒否する場合に備えています。
実際のCIはHost次第で、受諾は保証しません。Debugログに確定CIを表示します。

### 12. 想定消費電力

**実測していません。以下は3.3V入力での電源設計用の粗い予算値で、達成値・保証値ではありません。**
ESP32のModem-sleep 80MHzの公開値はsingle-coreで20～25mA、BLE RF動作時はTX約130mA/RX約95～100mAです。
40MHz待機・RF duty・CPU MAX lock時間に依存するため、実際にはこの予算を下回る/上回る可能性があります。

| 条件 | 平均電流の暫定予算 |
|---|---|
| A: 未接続・無通信・SLOW | 20～30mA |
| B: 未接続・FAST | 25～45mA |
| C: 接続・MIDIなし | 未測定。Controller sleep無効化後の平均値は未確定 |
| D: 接続・演奏 | 未測定。Controller sleep無効化後の平均値は未確定 |

参考: [ESP32 datasheet](https://documentation.espressif.com/esp32_datasheet_en.html)。USB-UARTや外付け回路の電流を含みません。
µA級待機を想定する部品ではありません。安定した電源と瞬間電流への余裕が必要です。

### 13. 想定レイテンシ

内部平均500µs未満、理想100～200µsが目標です。Notify API要求までの実測平均93µs、API内部414µsです。API復帰を含む合計約507µsは僅かに目標未達です。BLE→UARTの215µsは1件の確認値です。
CI=7.5msの場合、次の接続イベントまでの位相待ちだけで概ね0～7.5ms、平均約3.75msが加わります。
無線再送、Host scheduling、UART送信所要時間は別です。31250baudの3byteは線上で960µsかかります。
Debugにはメッセージ完成→Notify要求、GATT callback開始→TX投入の平均・最大ソフトウェア時間を記録します。
これらはISR→task起床や、TX backlog→FIFOに入るまでの時間をすべて含む計測ではありません。
`CONFIG_TERA_PROBE_GPIO`のpulseもcodec処理範囲の補助測定です。目標の最終判定はUART線とロジックアナライザ、BLE側の記録を合わせて行ってください。

### 14–15. Build / Unit Test

公開ソースには実機のNVS/Flashバックアップ、ビルド済みバイナリ、ローカルの作業ログを含めていません。測定条件は `latency-report.md`、公開時の確認は `PUBLICATION-VERIFICATION.md` を参照してください。
Release/Debugを別のsdkconfig・build directoryで作ります。

| 検証 | 結果 |
|---|---|
| Release / Debug | 両方ビルド成功、自作コードのcompiler warning/error 0 |
| Release app bin | 407184 byte（v1.2.0 LED有効） |
| Debug app bin | 437200 byte（v1.2.0 LED有効） |
| v1.0.0参考：Release静的DRAM / IRAM | 42927 / 85947 byte（起動時に確保するheapは別） |
| CTest | 3/3 PASS（名前変更を追加） |
| NVS mock | 起動読込、保存・再起動、同値の書き込み省略、不正値、Baud同時保存 PASS |
| LED模擬テスト | HIGH/LOW点灯、待機・購読・方向・連続通信・切断・無効設定 PASS |
| Core | 62438 assertion checks PASS（繰り返し試行を含む） |
| Fuzz / 往復 | 不正packet 10000件 / 正常stream 1000件 PASS |
| 8N1波形模擬 | 5Baud × 2種類のStatusパターン＝10条件 PASS |

UARTのISRとFIFO充填関数がIRAMへ配置されていることもELFで確認しています。

```powershell
# ESP-IDF v6.1の環境を有効にしてからリポジトリ直下で実行
idf.py -B build-release -D SDKCONFIG=sdkconfig.release -D SDKCONFIG_DEFAULTS=sdkconfig.defaults build
idf.py -B build-debug -D SDKCONFIG=sdkconfig.debug.generated '-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.debug' build

# 開発時のみ（通常利用には不要）
idf.py -B build-debug -p COM7 flash monitor
```

USBポートは環境に合わせて変更してください。MIDI UARTはUART1、ログはUART0・115200です。
通常版の書き込みは `idf.py -B build-release -D SDKCONFIG=sdkconfig.release -p COM7 flash` で実行できます。既存機器の書き込み前には必要な設定をバックアップしてください。`erase-flash` はNVSに保存した名前・Baudも消します。
Debugは5秒おきにversion、IDF、Baud/source/score、接続・CI・Latency・MTU・PHY・広告状態、送受信数、parse/UART error、overflow、滞留破棄数、ソフトウェア時間を出します。平均時間は累積100000サンプルまでです。
メッセージ単位ログ・ASCII操作コマンドはありません。Releaseはアプリ/bootloaderログを無効化しています。

ホストCテスト:

```powershell
cmake -S tests -B build-tests -G Ninja -DCMAKE_C_COMPILER=gcc
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

波形模擬テストは同梱 `tests/test_baud_wave.py` と `baud_wave_bridge.c` を使用します。
模擬UARTの5Baud × 通常Status/Running Statusの10条件、無通信10000回を確認しています。
これはESP32のUART oversampling、配線ノイズ、発振誤差、ISRの実測試験ではありません。

### 16. 現在の制限

- 任意のMIDIバイト列からのBaud推定は必ず一意になるわけではありません。少量・Realtimeのみ・非常に疎なデータでは採用しません。
- 探索は入力中だけ行い、試行中の演奏データは破棄します。保存Baudを変更した直後の最初の演奏は欠ける可能性があります。
- 現在Baudを信用して通常転送するため、誤Baudが判明する前に偶然成立したMIDIを送る可能性はあります。
- BLEが未接続/未購読の間のUARTデータは捨てます。接続・購読の世代変更では受信途中の状態も捨てます。
- Ring/Notify Queueは有限です。230400baudの高密度データや長いSysExで無線側の容量を超えると破棄します。無限の流量を保証しません。
- overflow/送信失敗後はSysExの終了とparser復旧を試みます。送信リソースが枯渇している場合、EOXも次の送信機会まで遅れる場合があります。
- 過負荷でNote Offが失われた場合まで、音源の発音停止を保証する機能はありません。独自routing・panic挿入機能は追加していません。
- ESP32-SOLO-1自体が18×25.5mmなので、それより小さい完成モジュールにはできません。今回の成果物はファームウェアで、基板CADは含みません。
- ESP-IDF標準component内のCMake/Kconfig通知は同梱ログに残しています。SDK本体を改変していません。

### 17. 実機確認項目

2026-10-07、COM7のSOLO-1とCOM9のWROOM-32で接続・遅延・両方向の確認を実施しました。試験範囲と未確認項目は latency-report.md に記録しています。

1. 単独3.3V電源での起動、BLE名/Service/Characteristic検出。
2. iPhone/iPad/macOS/自作Hostで、Read・CCCD・Write Without Response・暗号化要求・両方向Note On/Off。
3. 実CI、Latency 0、切断→FAST→15秒後SLOW、Hostからの再接続。
4. 5Baudそれぞれの実線、保存Baud起動、入力Baud変更、無通信24時間、NVS破損とversion違い。
5. Running Status、Realtime途中挿入、Clock、Active Sensing、数十KB SysEx、双方向同時通信。
6. FIFO/Ring/Notify pool飽和・無線パケット損失後の復旧、古いNote Onが再接続後に流れないこと。
7. UART message完成→実Notify要求、GATT受信→実FIFO投入の平均/p95/最大。最初のNote OnとDFS復帰も含める。
8. USB-UARTを外した3.3V入力でA/B/C/Dを電流計測。低Duty広告は十分長い時間で平均化する。

上記の遅延測定はESP32同士・31250bpsの内部UARTループバック条件です。追加でv1.2.0の外部UART配線において、31250 baudおよび230400 baudのUSB MIDI経由の両方向各100件を確認しました。消費電力・iOS/macOS等の継続動作・外部DIN回路は未検証です。

## 追加の実機確認（2026-10-07）

既存v1.2.0を変更せず、外部UARTの31250 baud / 8N1でUART→BLE・BLE→UARTを各100件確認し、内容と順序が一致しました。現在の実機保存Baudは31250、BLE名はESP MIDIです。USB MIDIフロントエンドのソースはこのリポジトリには含みません。

230400 baudも両方向各100件を確認しました。ただし初回の31250→230400自動判定では、誤Baudの入力がRealtimeとして成立して探索が始まらない事例があり、保存Baudを明示的に設定して確認しました。任意のデータでの自動判定を保証する結果ではありません。
