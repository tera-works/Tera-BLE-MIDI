# SOLO-1 / WROOM-32 実測結果（2026-10-07）

SOLO-1にTera BLE MIDI v1.1.1を、WROOM-32に測定用BLE MIDI受信ファームウェアを書き込みました。

## 測定結果

測定区間は **SOLO-1でUART MIDIメッセージが完成 → WROOM-32のUART TX FIFOへ投入** です。各条件1,000件のNote Offを送信しました。

| 接続条件 | 平均 | 中央値 | 95%点 | 99%点 | 最大 | 欠落 |
|---|---:|---:|---:|---:|---:|---:|
| 従来の接続制御：15ms、接続中modem sleep有効 | 10.877ms | 10.905ms | 17.569ms | 18.512ms | 28.767ms | 0/1,000 |
| 改修版：7.5ms、接続中Controllerを起こした状態に維持 | 6.538ms | 6.554ms | 9.637ms | 10.097ms | 10.149ms | 0/1,000 |

平均は **4.339ms、約39.9%短縮**、95%点は約45.1%短縮しました。両試験ともGPIOパルス1,000、受信1,000、重複0、GPIO対応欠落0、UART FIFO overflow 0、BLE decode error 0でした。改善前の比較用イメージにもUART FIFO修正は適用済みです。最初のUART送信不具合があるイメージからは有効な遅延値を取得できていません。

受信側のBLE callback入口までの平均は、15ms時10.790ms、改修版6.509msでした。

## ESP32内部の段階計測

| 項目 | 観測値 |
|---|---:|
| 改修版：メッセージ完成 → Notify API呼び出し | 平均93µs、最大267µs |
| 改修版：Notify API内部 | 平均414µs、最大601µs |
| 上記を合計したAPI復帰まで | 平均約507µs |
| BLE受信 → SOLO-1 UART送信キュー/FIFO投入 | 215µs（逆方向の確認1件） |

送信側の段階計測は1,000件に加え、逆方向確認の内部UARTループバックによる戻り1件を含む1,001件です。7.5msの接続を維持しつつmodem sleepを有効にした短い比較試験では321件を観測し、要求まで平均104µs、API内部平均558µsでした。

**Notify要求までの目標500µs未満は満たしていますが、API復帰を含めると約507µsで僅かに未達です。** 長時間・混雑時や他Hostでも保証できる数値ではありません。逆方向の1件は動作確認であり、平均・分位値の評価ではありません。

初回の段階計測ログの `complete>notify_return=520` は計数処理の時間も含みます。最終ソースではAPI復帰直後に時刻を捕捉し、集計処理を含めない測定点に修正しました。上表の507µsは、記録済みの要求まで93µsとAPI内部414µsの合算です。この変更はGPIOによる受信側の測定区間を変更しません。

## 修正した不具合・設計

1. `uart_ll_get_txfifo_len()` はFIFOの空き容量を返します。使用量として逆に扱っていた部分を直し、空FIFOに送信できない／TX empty割り込みが継続する不具合を解消しました。
2. 最初に7.5msで接続できても、1秒後に7.5～15msの予備要求を送り15msへ変わっていました。実際のCIが7.5ms・latency 0なら予備要求を送りません。
3. 接続中は標準API `esp_bt_sleep_disable()` を使用してController起床待ちを軽減します。切断時は `esp_bt_sleep_enable()` でAdvertising待機中の省電力動作へ戻します。CPU DFS、tickless idle、FAST 30ms → SLOW 750msは維持します。
4. デバッグ用にNotify要求まで／API内部の段階計測を追加しました。Releaseではこの追加の時刻取得・計数処理を実行しません。
5. WROOM-32は接続開始後・接続中にスキャンを再開しません。測定中の無用なスキャンを避けます。

接続中の消費電力はControllerのmodem sleepを維持する方式より増える可能性があります。**電流は未測定です。** 未接続時の省電力方針は維持し、接続中は最初のMIDIイベントにも起床待ちが乗らないことを優先しています。

## 配線・方法

- SOLO-1：COM7 / CH9102、ESP32-S0WD rev1.0、base MAC `24:0a:c4:85:66:f8`、BLE MAC `24:0a:c4:85:66:fa`。
- WROOM-32：COM9 / CP2102N、ESP32-D0WDQ6 rev1.0、base MAC `30:ae:a4:ee:9e:5c`、BLE MAC `30:ae:a4:ee:9e:5e`。
- 共通GND。SOLO-1 GPIO5出力 → WROOM-32 GPIO5入力。両方出力にはしません。
- 送信側UART1の内部ハードウェアループバック、31250bps / 8N1。実際のTX FIFO・UART受信割り込み・既存MIDI parser・BLE encoder・Notify経路を通します。
- MIDIメッセージ完成時にGPIO5を立ち上げ、受信側がGPIO interruptとBLE callbackの時刻を自分の時計で記録します。PCのUSB送信時刻、両ESP32の時計のずれは使用しません。
- 14bit sequenceをNote Offのdata bytesで識別。20～60msのランダム間隔、購読後2秒待って開始。1,000件の終了後にCSVを出力し、演奏経路で1件ずつログを出しません。
- 実際のCIは7.5ms / latency 0 / supervision timeout 1,000ms、ATT MTU 23、PHY 1M。両MCUのアプリはsingle core構成です。
- 測定後にBLEからchannel 2 Note Offを1件送信。SOLO-1の `B>U=1` と内部UARTループバック後の `U>B=1001` で両方向を確認しました。

GPIO ISRが立ち上がりを観測するまでの遅れが基準時刻に入ります。外部ロジックアナライザによる測定ではありません。また入力UARTの最初のbitからの時間、UART最終RX byteからparser完成までの時間、出力UARTの線上の送信完了までは含みません。31250bpsで3byteを送る時間は片道960µsです。入力と出力を加える場合は約1.92ms＋受信処理の時間が別途必要です。DINの電気回路、外部MCUのUART、iOS/macOS、長いSysEx・高負荷連続送信、他baudの実機試験は未実施です。

## ビルド・テスト

ESP-IDF v6.1。Release / Debug / sender / receiverの `idf.py build` は完了し、最終コンパイルのerror / warningは0です。新しいCI予備要求・FIFO空き容量・省電力遷移のホストテストはPASS。既存のMIDI/BLE codec/Auto BaudのCTest 2/2とLED HIGH/LOW/無効の検証結果も同梱しています。

SOLO-1の通常版では内部UARTループバック、自動テスト送信、GPIO5測定出力を無効にしています。WROOM-32には測定用受信版を残しています。この受信版はテスト用sequenceの記録を目的とするもので、汎用BLE→UART製品用ファームウェアではありません。通常SOLO-1のRX=16、TX=17、任意LED=25です。

両基板の変更前先頭1MBをバックアップしました。WROOMの460800bpsでの追加書き込みは一度MD5不一致となり、115200bpsで再書き込みして一致を確認しました。SOLO-1の最終書き込みもhash一致を確認しています。

## 記録

- `baseline-15ms-valid/`：比較用試験の両側ログ。
- `optimized-7_5ms/`：改修版1,000件の両側ログ。
- `diagnostic-7_5ms-sleep/`：接続中sleep有効、段階計測321件。
- `baseline-result.json/.csv`、`optimized-result.json/.csv`：集計とraw sample。
- `baseline-15ms/`：UART FIFO不具合により無効となった最初の試験。結果には使用していません。
- `sender-firmware/`、`receiver-firmware/`：最終測定用ビルド。
- `sender-baseline-firmware/`：比較試験で使用した履歴イメージ。
- `../Tera-BLE-MIDI/`：通常版v1.1.1、ソース、ビルド・書き込みログ。
