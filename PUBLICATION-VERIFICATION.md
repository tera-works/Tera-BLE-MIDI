# 公開ソースの確認（2026-10-07）

ESP32-SOLO-1向けUART MIDI ⇄ BLE MIDI Peripheral、v1.2.0です。

- ESP-IDF v6.1 / NimBLE / C。外部MIDIライブラリ不使用。
- 名前・GPIO・LED設定はKconfig、UART Baud候補は`main/config.h`。
- アプリケーションの機能コードは公開準備で変更していません。
- 新しいホストビルドでCTest **3/3 PASS**：MIDI/BLE/Baud、名前変更、5種類のUART Baud波形模擬。
- 単体テストの波形模擬は実線・実UART oversamplingの保証ではありません。
- 実機v1.2.0では31250 baud・8N1の外部UARTによるUART→BLE / BLE→UARTを各100件確認し、内容と順序が一致。名前はESP MIDI。
- 230400 baudでも同じ方向の各100件を確認しましたが、誤Baud入力がRealtimeに見えて初回探索が始まらない事例があります。自動判定を全入力に対して保証していません。
- v1.1.1の1,000件の遅延測定は`latency-report.md`。v1.2.0で同じ1,000件測定は再実施していません。

ソース、設定初期値、テスト、名前変更の説明・サンプルSysExを公開します。実機のFlash/NVSバックアップ、認証情報、ビルド生成物、RP2040のソースは含みません。SDK本体は同梱せず、ESP-IDFを別途使用します。
