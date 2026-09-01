# AD00020 IR Password Keyboard

AD00020を、赤外線入力をトリガーにした複数環境向けのUSB HIDパスワード入力デバイスとして利用するためのプロジェクトです。

## Status

仕様固定済み。プロビジョニングとファームウェア実装は進行中です。

## Related projects

- `AD00020-USB_IR_Scanner` — PIC18F14K50、IR受信、USB HIDキーボード出力の既存ファームウェア
- `HIDBootloader-CLI` — LinuxからAD00020へHEXを書き込む既存CLI

## Security note

実パスワードや秘密鍵をソース、設定ファイル、テストデータ、ログへ保存しません。

## Local credentials

認証情報は、追跡対象外の `include/credentials.h` に記述します。

```sh
cp include/credentials.h.example include/credentials.h
vim include/credentials.h
```

`credentials.h` は `.gitignore` 済みです。コミット前に、実値が差分や追跡対象へ混入していないことを確認してください。

パスワードボタンは12スロット固定で、現在はスロット01〜04だけを使用します。現在の4枠は、順にWindows、WSL、macOS、Linuxへ割り当て、スロット05〜12は将来追加します。

マスターキーは、`include/master_key.h`に4つのNEC値を順番に記述します。雛形をコピーしてから、エディタで`REPLACE_ME`を8桁の16進数へ置き換えてください。`NEC-`は付けず、オン／オフ以外の16ボタンだけを指定します。

```sh
cp include/master_key.h.example include/master_key.h
vim include/master_key.h
```

このファイルも`.gitignore`対象です。プロビジョニング時に読み込まれますが、マスターキーは生成DBやファームウェアへ保存されません。

## Provision and build

`credentials.h`と`master_key.h`を用意した状態で、次のコマンドを実行します。

```sh
python3 tools/provision_db.py
./build.sh
```

生成される`include/generated_database.h`には、IVと暗号文だけが入ります。マスターキーは生成物やファームウェアへ保存されず、実行時に4フレームからRAM上で作られます。現在のビルドはテスト用生成DBを使っているため、実際の書き込み前には必ず自分の認証情報とマスターキーで再プロビジョニングしてください。
