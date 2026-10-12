# HIDRemocon

TVTest用のHID Consumer Controlリモコンプラグインです。VID/PID、製品名、メーカー、Report IDで機種を限定しません。Consumer Page（0x0C）のトップレベルコレクションから、Consumer Pageのボタン入力を受信します。

## 使い方

1. TVTestを終了します。
2. TVTestが64bitなら `bin/x64/Release/HIDRemocon.tvtp`、32bitなら `bin/Win32/Release/HIDRemocon.tvtp` を、TVTestの `Plugins` フォルダーにコピーします。
3. TVTestを起動して「プラグイン」から「HIDコンシューマーリモコン」を有効にします。
4. 「設定」→「リモコン」で `HID Consumer Control` を選び、各ボタンにコマンドを割り当てます。初期状態では全ボタンが未割り当てです。
5. 他のアプリを操作中にTVTestを反応させたくない場合は、リモコン設定の「アクティブ時のみ」を有効にします。

別の追加DLL、WebHID、ブラウザー、デバイスの選択操作は必要ありません。同じUsageを送る複数のデバイスは、同じボタンとして使えます。

今回の例 `02 23 02 00 00 00 00 00 00` は、デバイスのHID定義がConsumer Usageの配列であれば `AC Home`（0x0223）に対応します。レポート番号や9バイトという形式はプラグインに固定していません。

## ボタン一覧

Usage ID `0x0001`〜`0x02FF` の767項目をID順に登録しています。ボタン番号は常に `Usage ID - 1` なので、名称を追加しても既存の割り当て番号は変わりません。

名称はユーザー提供の `hid_consumer_decoder_webhid.html` から397件を取り込みました。`0x0000 Unassigned` は入力なしの値なので登録から除外し、396件をボタン名に使っています。名称が未収録の371項目は `Usage 0xXXXX` と表示します。これはUSB-IFの全Usage定義を収録した表ではありません。名称がある項目にも、コレクションやアナログ値などボタンではない定義が含まれます。デバイスがボタンとして報告する項目だけが動作します。

`ConsumerUsages.h` を編集すると名称を追加・修正できます。`0x0300` 以降のボタンが必要なら `HIDRemocon.cpp` の `LastUsage` を拡張して再ビルドしてください。範囲外の入力はTVTestのログにUsage IDを一度表示します。末尾への追加なので既存の割り当ては維持できます。

## 入力処理

- Raw Inputのpreparsed dataをWindowsのHIDパーサーに渡して解析します。Array形式とVariableのビットフィールド形式に対応します。
- 複数Usageの同時押しと、1つの `WM_INPUT` に入った複数レポートを処理します。
- 押下状態はデバイス別・Report ID別に保持します。押下開始を1回通知し、解除後の再押下を受け付けます。長押しの自動リピートは実装していません。デバイスが押下／解除を繰り返して送る場合は、その都度通知します。
- HIDの絶対値0/1として定義された単一の値もボタンとして処理します。音量の絶対値、チャンネル番号、相対軸、Usage Value Arrayはボタンに変換しません。
- デバイス通知でキャッシュを破棄します。抜き差しのたびにデバイスを指定し直す必要はありません。
- TVTest本体の既存の `0x0C/0x01` 登録を利用し、その他のConsumerコレクション用にページ単位の登録を追加します。無効化時には自分で追加した登録だけを解除します。Consumer入力が別のウィンドウに登録されている場合は、有効化を失敗させてログに理由を表示します。
- 解析したConsumerレポートはプラグインのメッセージコールバックで処理済みにし、TVTest本体のMCE用固定バイト解析による重複実行を防ぎます。

対象はRaw Inputに公開されるConsumer Pageのコレクションです。通常のキーボード（Page 0x07）や独自Usage Pageだけの入力は対象外です。特殊なドライバーが標準HID入力を公開しない機器にも対応しません。メーカーを限定しないことと、あらゆる入力形式への対応は別です。

Windowsの音量変更やブラウザー起動など、OSや他のアプリ側の動作は抑制しません。TVTestの `WM_APPCOMMAND` 割り当ても独立して動作するため、同じ操作が二重に実行される場合はTVTestの「キー割り当て」のマルチメディアキー設定を外してください。同一Usageの押下を複数機器から同時に受け取ると、機器ごとに1回ずつ実行します。

## ビルド

Visual Studio 2022の「C++によるデスクトップ開発」、MSVC v143、Windows SDKを使用します。プロジェクトをVisual Studioで開くか、PowerShellで実行します。

```powershell
.\build.ps1 -Platform x64
.\build.ps1 -Platform Win32
```

CRTは静的リンクです。TVTestプラグインAPI 0.0.14以降が必要です。Debug構成もあります。PDB生成は無効にしています。

## 検証

```powershell
cmd /c tests\run-tests.cmd x64
cmd /c tests\run-tests.cmd x86
```

- ボタン状態：押下、解除、同時押し、重複レポート、異なるReport ID、複数デバイスを14項目で検証。
- DLLを模擬TVTestホストにロード：エクスポート、767項目の登録と名称、有効化／無効化、失敗時の後片付け、登録競合、終了時の解除を検証。
- 接続済みHIDの定義を取得し、メモリー上で押下／解除レポートを生成してHIDパーサーを検証。デバイスへの書き込みやOSへの入力送信は行いません。適切なデバイスがない環境ではこの検証はスキップします。

実際のリモコンのボタン操作からTVTestのコマンド実行までの確認は、TVTestに導入した後に行ってください。

## 参考とライセンス

TVTest SDKの `TVTestPlugin.h` と `TVTestSDK.txt` を `sdk` にコピーしています。SDKの利用・改変・再配布条件は `sdk/TVTestSDK.txt` に記載されています。SDKのHDUSRemoconとGamePadサンプルをAPI利用方法の参考にしました。プラグイン独自コードはPublic Domain（CC0）です。

- [HidP_GetUsages（Microsoft）](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/hidpi/nf-hidpi-hidp_getusages)
- [RegisterRawInputDevices（Microsoft）](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerrawinputdevices)
- [USB-IF HID Usage Tables](https://www.usb.org/sites/default/files/hut1_3_0.pdf)
