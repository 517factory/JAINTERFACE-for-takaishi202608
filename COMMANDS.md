# JA-INTERFACE コマンド仕様・使い方ガイド

本ドキュメントでは、`sketch_JAINTERFACE_v111a` ファームウェアで利用可能なシリアルデバッグコマンドおよび制御・設定コマンド（SETコマンド）の使い方を解説します。

---

## 1. シリアルデバッグコマンド (Serial Commands)

USBシリアルコンソール（SerialDebug / 115200bps）から文字列を入力して実行するログ管理コマンドです。
入力時は末尾に `CR` / `LF` を付けて送信します。

| コマンド | 説明 | 使用例 |
| :--- | :--- | :--- |
| `logls` | LittleFS内に保存されているログファイルの一覧を表示します | `logls` |
| `logcat <ファイル名>` | 指定したログファイルの内容をシリアル出力します | `logcat /Log_20260724.log` |
| `logdel <ファイル名>` | 指定したログファイルを削除します | `logdel /Log_20260724.log` |
| `logdelall` | 保存されているすべてのログファイルを一括削除します | `logdelall` |
| `loghelp` | ログ関連コマンドのヘルプを表示します | `loghelp` |

---

## 2. システム設定コマンド (SET Commands)

シリアルコンソールまたはLTE/MQTTサーバー経由で送信可能な設定変更コマンドです。
フォーマットは **`SET_<項目キー>_<設定値>XX`** です。末尾の `XX` は必須の終了識別子です。

### 2.1 SIM・モデム設定関連

| コマンド電文 | 説明 | 設定値の範囲 |
| :--- | :--- | :--- |
| `SET_SIMMODE_<値>XX` | SIMキャリア切り替え動作モード | `0`: MANUAL(DIPスイッチ連動)<br>`1`: FIX(キャリア固定)<br>`2`: AUTO(自動キャリア切り替え)<br>`3`: FULL_AUTO(完全自動) |
| `SET_SIMSEL_<値>XX` | FIXモード時の使用キャリア選択 | `0`: DOCOMO<br>`1`: SOFTBANK |
| `SET_BANDSEL_<値>XX` | LTE Bandの優先順位選択 | `0`: MB_PRI(メインバンド優先)<br>`1`: PB_PRI(プライベートバンド優先) |

> [!NOTE]
> **DIPスイッチ非搭載基板における挙動 (`ENABLE_DIP_SWITCH = false`)**
> `SET_SIMMODE_0XX` (MANUALモード) を設定した場合、物理スイッチが存在しないため、自動的に **`MODE 1` (FIXモード)** へフォールバックし、`SIMSEL` で指定されたキャリア（Docomo / Softbank）に固定接続されます。その際、シリアルログに `(SIMMODE=0->1)` のフォールバックログが出力されます。

---

### 2.2 システム・タイマー設定関連

| コマンド電文 | 説明 | 設定単位・範囲 |
| :--- | :--- | :--- |
| `SET_POLL_<分>XX` | 定期POLL送出インターバル | `0` 〜 `1440` [分] (`0`でPOLL停止) |
| `SET_TCUPDATE_<日>XX` | タイムコード自動更新周期 | `1` 〜 `7` [日] |
| `SET_AUTOLOCK_<秒>XX` | オートロック発動までの遅延時間 | `0` 〜 `300` [秒] (`0`で無効) |
| `SET_HEXMODE_<値>XX` | 電文処理のHEXモード切替 | `0`: OFF / `1`: ON |

---

### 2.3 電圧キャリブレーション設定関連

| コマンド電文 | 説明 | 設定範囲 |
| :--- | :--- | :--- |
| `SET_VCALP_<mV>XX` | 電圧読み取りキャリブレーション (+側補正) | `0` 〜 `1000` [mV] |
| `SET_VCALM_<mV>XX` | 電圧読み取りキャリブレーション (-側補正) | `0` 〜 `1000` [mV] |

---

### 2.4 地震検知 (EQ) 関連設定

| コマンド電文 | 説明 | デフォルト値 / 範囲 |
| :--- | :--- | :--- |
| `SET_EQTYPE_<値>XX` | 地震検知アルゴリズムタイプ | `0` 〜 `2` |
| `SET_EQBT_<ms>XX` | EQ検知ブロックサイズ | `200` 〜 `5000` [ms] |
| `SET_EQCT_<個>XX` | EQ検知必要ブロック数 | `2` 〜 `50` [個] |
| `SET_EQINT_<ms>XX` | EQ検知インターバル | `10` 〜 `2000` [ms] |
| `SET_EQARST_<値>XX` | EQ自動リセット機能 | `0`: DISABLE / `1`: ENABLE |

---

## 3. 制御・状態確認コマンド (Control / Status Commands)

システムの状態確認やリセット、動作起動を行うコマンドです。

| コマンド | 説明 |
| :--- | :--- |
| `CHK` / `COM_CHECK` | システムの状態チェックを要求し、ステータスを返信します |
| `JRST` / `COM_RESET` | ESP32マイコンをソフトリセット（再起動）します |
| `JAON` | JA発動モードを起動します |
| `MODEMSTATE` | LTEモデムの接続状態・電波強度(RSSI)を確認します |

---

## 4. 実行ログの例

### SIMモードをAUTO (2) に設定した場合のログ出力
```text
Received command: COM_SET
RECEIVE CONFIG SETTING [SIMMODE][2]
Config Change Success : SIMMODE
Set Config Change SIMMODE = 2
Modem carrier setup updated for SIMMODE (AUTO-MODE). Reconnecting network...
```

### SIMモードに 0 (MANUAL) を設定した場合のログ出力（DIPスイッチ無効時）
```text
Received command: COM_SET
RECEIVE CONFIG SETTING [SIMMODE][0]
Set Config Change SIMMODE = 0
[WAR] U128 : MODE 0 selected but DIP switch is disabled. Falling back to FIX mode (SIMSEL).
[INF] U128 : Set Carrier [FIX-DOCOMO] (SIMMODE=0->1, SIMSEL=0)
```
