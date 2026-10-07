# MIDI経由の名前変更（v1.2.0）

初期BLE名は **ESP MIDI**。名前を変更するとESP32内のNVS（Flash）へ保存します。電源を切っても保持します。UART MIDI入力（GPIO16）または接続したBLE MIDI Hostから、以下のSysExを送ってください。UART0の書き込み用シリアルポートへASCIIを送っても設定にはなりません。

```text
F0 7D 45 53 50 01 01 <名前のASCIIバイト列> F7
```

名前は1～29バイトの表示可能なASCII（英数字・スペース・記号）。日本語・空の名前・スペースのみ・長すぎる名前・制御文字は不可です。末尾のスペースは除きます。長さ・チェックサム・NULL終端・固定長のスペース埋めは不要です。

例：`Stage KB`

```text
F0 7D 45 53 50 01 01 53 74 61 67 65 20 4B 42 F7
```

例：`ESP MIDI`に戻す

```text
F0 7D 45 53 50 01 01 45 53 50 20 4D 49 44 49 F7
```

1. UARTまたはBLE MIDI経由でSysExを送る。
2. MIDI Clock / Active Sensingを含めて送信を止め、**3秒以上待つ**。
3. 本体を再起動する。保存した名前でAdvertisingする。

名前は保存が終わる前に電源を切ると失われます。Debug版はUART0へ `name saved: ... (active on reboot)` を表示します。Release版のMIDI経路には独自のACKやログを追加していません。Hostが以前の名前をキャッシュしている場合は、Host側で再検索してください。

名称の変更で接続を強制切断することはありません。起動中のGAP名と広告名は一定に保ち、変更は次回起動に反映します。保存は低優先度タスクで行い、MIDIコールバック内ではNVSへアクセスしません。名前変更を明示的に要求したときだけ、接続中でも2秒以上の無通信後にFlash保存します。保存処理が始まった瞬間に演奏を再開した場合は、Flash処理が短時間影響する可能性があります。通常演奏・無操作時は名前のFlash書き込みを行いません。自動Baudの保存は従来どおり切断時だけです。

このSysExは本モジュール用の独自形式です。mi.1専用アプリ・QUICCO SOUNDのメーカーIDを用いるSysExとの互換性はありません。`7D`は開発用IDなので、同じ接頭辞を使う他の開発用機器と共有する経路では衝突しないことを確認してください。本コマンドを認識した場合は本体で消費し、反対側へ転送しません。それ以外のSysExやMIDIは従来どおり転送します。

標準Pythonだけで`.syx`を生成できます。

```powershell
python tests/make_name_sysex.py "Stage KB" --output Stage-KB.syx
```

NVSキーは従来の`tera_uart`内に`name`を追加。既存の`version=1`と`baud`を保持するため、v1.1.1からの更新でBaud設定を消す必要はありません。不正なversion・名前、名前キーが未登録の場合はビルド時の初期名へ戻ります。同じ名前への設定ではNVSを書き換えません。

## 検証結果（2026-10-07）

- Release / Debug：ESP-IDF v6.1でビルド成功、compiler error / warning 0。
- CTest 3/3：既存MIDI/BLE/Baud、名前変更、5種類のBaud波形模擬がPASS。
- NVS mock：保存後の再起動読込、同値の書き込み省略、不正名・versionのデフォルト復帰、Baudと名前の同時要求がPASS。
- SOLO-1 COM7とWROOM-32 COM9：MTU23で分割したBLE SysExにより「ESP MIDI Test」へ変更。NVS保存後にSOLO-1を再起動し、その広告名を確認。続いて「ESP MIDI」へ戻して保存・再起動し、広告名とCI7.5ms / Latency 0での接続を確認。

その後、外部UART入力からのSysExでも保存・再起動後の広告名を確認し、最終名はESP MIDIへ戻しました。公開リポジトリにはローカルの実機ログや実機NVSデータは含みません。v1.2.0では遅延の1,000件試験を再実施していません。
