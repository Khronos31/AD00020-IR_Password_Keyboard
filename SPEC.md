# AD00020 IR Password Keyboard 仕様 v0.2.0

## Objective

AD00020（PIC18F14K50）を、NEC赤外線リモコンを入力装置とするUSB HIDキーボードとして使う。パスワードマネージャーや生体認証を使えないWindows、WSL、macOS、Linux等へ、登録済みパスワードを直接入力できるようにする。

想定する主な脅威は、リモコンを伴わないボード単体の紛失・盗難時に、ファームウェアやフラッシュの内容を見られてもパスワードが平文で露出しないことである。リモコンも同時に取得された場合の、既知の15ボタンを使う1〜32フレーム列の総当たりは許容する残存リスクとする。別リモコンの標準NEC値を使う場合は候補空間が広がる。

## Fixed behavior

### IR commands

All listed values are the decoded NEC value in the existing scanner's `NEC-XXXXXXXX` notation.

| Command | Value | Behavior |
|---|---:|---|
| Mode | `01FE7887` | どの状態でも出力を中断し、現在の鍵・解除入力・暗号状態を消去してマスターキー入力モードへ移行する |
| ON | `01FE48B7` | マスターキー入力モードだけで入力列を確定して解除を試行する。入力列0フレーム（Mode→ON）も有効。その他の状態では無視する |
| Password slot 01 | `01FE20DF` | Unlock済みならslot 01を入力してEnter |
| Password slot 02 | `01FEA05F` | Unlock済みならslot 02を入力してEnter |
| Password slot 03 | `01FE609F` | Unlock済みならslot 03を入力してEnter |
| Password slot 04 | `01FEE01F` | Unlock済みならslot 04を入力してEnter |
| Password slot 05 | `01FE10EF` | 将来追加。初期ファームウェアでは無効 |
| Password slot 06 | `01FE906F` | 将来追加。初期ファームウェアでは無効 |
| Password slot 07 | `01FE50AF` | 将来追加。初期ファームウェアでは無効 |
| Password slot 08 | `01FED827` | 将来追加。初期ファームウェアでは無効 |
| Password slot 09 | `01FEF807` | 将来追加。初期ファームウェアでは無効 |
| Password slot 10 | `01FE30CF` | 将来追加。初期ファームウェアでは無効 |
| Password slot 11 | `01FEB04F` | 将来追加。初期ファームウェアでは無効 |
| Password slot 12 | `01FE708F` | 将来追加。初期ファームウェアでは無効 |
| OFF | `01FE58A7` | どの状態でも即時ロックし、RAM上の鍵・平文・入力列・暗号状態を消去してLEDを消す |

- Mode受信時点で、現在のマスターキー、解除入力、保留中のパスワード出力を破棄してから入力モードへ移行する。入力モード中は赤LED（RC5）を点滅させる。
- 入力モード中は、Mode・ON・OFF以外の標準NECフレームをすべて、内蔵ボタン一覧やリモコンの種類に関係なく最大32フレームまで順番に受け付ける。33個目のフレームはロックして列を破棄し、バッファへ書き込まない。
- 標準NECのアドレス／反転アドレスおよびコマンド／反転コマンドの検証に失敗したフレームは無視する。
- 入力モードのON受信では、0〜32フレームの列を確定して解除を試行する。入力列0フレームでも特別扱いの拒否はしない。復号検証失敗ではロック状態へ戻り、出力しない。
- 解除成功後はデータベースをRAM上の鍵で利用可能にする。
- ロック中のslotコマンドは無視する。
- 無効なslot（05〜12）は初期ファームウェアでは何も出力しない。
- パスワード出力完了時にEnterを1回送信し、ロックせずに最終操作時刻を更新する。
- 入力モードまたは解除成功後の無操作時間が180秒に達したら自動ロックする。Mode、受け付けた入力フレーム、ON、パスワードslot操作および出力完了は活動としてタイマーを更新する。
- USB HIDは現在フォーカスされている入力先へ送信する。接続先やパスワード欄は判定しない。

### Credential slots

- データベース形式は12スロット固定とする。
- 初期版はslot 01〜04のみ設定し、slot 05〜12は長さ0の未設定スロットとする。
- slot 01〜04は既存のローカル認証情報ヘッダーのWindows、WSL、macOS、Linuxに対応する。
- パスワードはNULを含まない、US配列の印字可能ASCII（`0x20`〜`0x7e`）に限定する。最大長は63バイトとする。
- HID出力のキーボード配列はUS配列を前提とする。配列が異なる環境では記号の対応を実機確認する。

## Storage and cryptography

- パスワードの平文はコミット対象、生成ファームウェア、PICフラッシュへ配置しない。
- マスターキーは不揮発領域（PICフラッシュ、EEPROM、生成ヘッダー、ログ）へ配置しない。NECフレーム列を受信してONを受けた後にRAM上でKDFから生成し、電源断で自然に消失する。
- ローカルの`include/credentials.h`はビルド時の入力であり、`.gitignore`対象。これはユーザーが管理する作業用平文である。
- ローカルの`include/master_key.h`はプロビジョニング時の入力であり、`.gitignore`対象。生成DBやファームウェアにはコピーしない。
- `master_key.h`の入力形式は`AD00020_MASTER_KEY_SEQUENCE`一つの文字列で、8桁の標準NEC値を0〜32個並べる。値は連結または区切り文字付きで記述できる。
- `tools/provision_db.py`が`credentials.h`を読み、暗号化済みの`include/generated_database.h`を生成する。生成物も`.gitignore`対象とする。
- データベース暗号化はAES-128-CTRとする。暗号化ごとにランダムな16バイトIVを生成し、IVは暗号文とともに保存する。
- AES鍵は次の決定的KDFで生成する。`AES-CMAC(KDF_KEY, ASCII("ADPK-MASTER-KEY-V2") || count_byte || frame_1 || ... || frame_n)`の16バイトをAES-128鍵とする。`KDF_KEY`はASCII(`ADPK-KDF-V2`)にNUL 5バイトを加えた公開固定値、`count_byte`は0〜32のフレーム数、各frameはデコーダーが得た4バイトの標準NEC値を順番どおりに使う。空列はドメイン文字列とcount=0だけを入力する。これは鍵長を固定し、旧版の固定4フレーム連結との曖昧さを避けるドメイン分離KDFであるが、リモコン同時盗難時の強い総当たり耐性を提供するものではない。
- PIC側には動的メモリを使わない小型のAES-CMAC実装を含め、Pythonプロビジョニングと同じKDFを実装する。入力列および導出鍵は生成ヘッダーやファームウェアへ書き込まない。
- 暗号化平文には固定マジック、バージョン、スロット数、各スロット長、パスワード領域、CRC32を含める。復号後の検証に失敗したキーでは解除状態に遷移せず、HID出力もしない。
- AES-CTRは改ざん検出を提供しない。ファームウェア改変・暗号文改変への耐性は今回の非目標とする。
- マスターキーと解除シーケンスの一時バッファは、Modeによる再入力、OFF、タイムアウト、導出後、電源リセット時にゼロ化する。復号したパスワードのバッファは、各出力完了時と同じロック経路でゼロ化する。

## Prior-art assessment

| Candidate | Classification | Reason |
|---|---|---|
| `/config/GitHub/AD00020-USB_IR_Scanner` | adapt/reference | 同じPIC18F14K50、NEC受信、USB HIDキーボード、XC8ビルドがあり、IR/USBの土台として適合する |
| `/config/GitHub/HIDBootloader-CLI` | adapt/reference | 生成HEXの書き込み・検証・リセットに使えるが、ファームウェア本体ではない |
| Microchip USB HID reference | adapt/reference | 既存スキャナが既に利用しているUSBスタックを継続利用する |
| ESP32系オフライン password manager repositories | build/reference only | 暗号化HIDという部分目的は一致するが、MCU、入力方式、USBスタック、リソースが異なるため採用不可 |
| 市販FIDO2/生体認証 | not adopted | Moonlight越しのWSL等を含む今回の入力経路では、全対象へ一貫して利用できない |

## Non-goals

- リモコンの真正性確認、IRリプレイ防止、盗聴対策。
- ボードとリモコンの同時盗難に対する強固な総当たり耐性。
- HID入力先の自動判定。
- パスワードマネージャーのようなサイト名・ユーザー名・TOTP・同期機能。
- AES-CTRによる暗号文改ざん防止。
- 初期版でのslot 05〜12の実パスワード登録。

## Executable acceptance criteria

1. `python3 -m unittest discover -s tests -v` exits 0 and verifies the database format, AES round-trip, wrong-key rejection, four active slots, eight empty slots, and v0.2.0 KDF/parser vectors.
2. `python3 tools/provision_db.py --help` exits 0 without printing credential values.
3. A provisioning run with test credentials emits a C header containing IV/ciphertext/CRC data but none of the test plaintext strings.
4. `XC8=... DFP=... ./build.sh` exits 0 and produces a PIC18F14K50 HEX.
5. The generated HEX and tracked source tree contain no configured password literals. The local ignored `include/credentials.h` is excluded from this check.
6. Static inspection verifies that locked state cannot call the password output path, the lock path zeroizes key/plaintext buffers, and the 180-second timeout transitions to locked state.
7. `git diff --check` exits 0.
8. IR reception, USB enumeration, keyboard-layout behavior, timing, and lock/output behavior on physical AD00020 hardware are `unverified` until a device test is performed.
9. Static inspection of the generated database header and HEX finds no master-key sequence or derived-key bytes; the only runtime master-key storage is a writable RAM buffer that is zeroized on every lock path.

## Constraints and rollback

- Do not display, log, commit, or copy the contents of `include/credentials.h`.
- Do not display, log, commit, or copy the contents of `include/master_key.h`.
- Do not modify `/config/secrets.yaml`, `/config/.ssh/`, or `/config/.storage/`.
- Do not flash the device or invoke external deployment without explicit approval.
- Before any implementation edit, preserve the current uncommitted scaffold changes.
- Rollback: remove only new tracked files/changes in this repository after reviewing `git diff`; leave the ignored local credential header untouched unless the user explicitly asks for its removal.
