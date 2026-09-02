# AD00020 IR Password Keyboard v0.2.0

AD00020を、赤外線入力をトリガーにした複数環境向けのUSB HIDパスワード入力デバイスとして利用するためのプロジェクトです。

## Status

v0.2.0のプロビジョニングとファームウェア実装です。

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

マスターキーは、`include/master_key.h`の`AD00020_MASTER_KEY_SEQUENCE`一つに、0〜32個の標準NEC値を順番に記述します。8桁値は連結しても、スペース・カンマ・コロン・セミコロンで区切っても構いません。`NEC-`は付けず、Mode／ON／OFFは指定できません。内蔵15ボタン以外や別リモコンの有効な標準NEC値も使用できます。

```sh
cp include/master_key.h.example include/master_key.h
vim include/master_key.h
```

このファイルも`.gitignore`対象です。プロビジョニング時に読み込まれますが、入力列も導出鍵も生成DBやファームウェアへ保存されません。Mode→ONの空列も有効です。

## Provision and build

`credentials.h`と`master_key.h`を用意した状態で、次のコマンドを実行します。

```sh
python3 tools/provision_db.py
./build.sh
```

生成される`include/generated_database.h`には、IVと暗号文だけが入ります。AES鍵は、ASCIIドメイン`ADPK-MASTER-KEY-V2`、フレーム数1バイト、受信した4バイトNEC値列を、公開KDF鍵（`ADPK-KDF-V2`の後ろにNUL 5バイト）によるAES-CMACへ入力し、その16バイトを使って実行時にRAM上で作られます。入力列と導出鍵は生成物へ保存されません。実機へ書き込む前には、必ず自分の認証情報とマスターキーで再プロビジョニングしてからビルドしてください。

Mode受信で入力モードに入り、最大32個の有効な標準NECフレームを記録します。ONで解除を試行し、OFFはどの状態でも即時ロックします。入力モードと解除後の無操作タイムアウトは180秒です。解除後の12個のslotコードは、設定済みパスワードとEnterを出力してもロックせず、次の操作を受け付けます。

## License

このプロジェクト用に作成したファイルはMIT Licenseです。`src/Microchip/`のUSBスタックと`src/crypto/aes.c`／`aes.h`は第三者コードであり、MIT Licenseの対象外です。個別の条件は[LICENSE](LICENSE)と[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)を確認してください。
