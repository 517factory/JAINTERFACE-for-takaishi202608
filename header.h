/*****
Define header

for COCOBOX3
2024.6.5 ESP32対応版　ESP32-S3-DevKitC-1    全体見直し、不要なもの削除
******/

#pragma once

#include "JAI_gpio.h"
#include "JAI_version.h"
#include <Arduino.h>
#include <map>
#include <string>
#include <vector>

#define cbx_wait(time)                                                         \
  vTaskDelay(pdMS_TO_TICKS(                                                    \
      time)) // delayをマクロ定義(delayとvTaskDelayは同じという説もあるが実際変わるようにみえる)

// I2Cアドレス
#define I2C_KEYUNIT_ADDR 0x08

// 機能の有効・無効化
const bool ENDULANCE_MODE =
    false; // trueなら生存確認時にPOLではなく施錠→解錠を実施
const bool DOOR_INTLK_ENABLE =
    true; // ドアインターロックのオンオフ trueでインターロックあり
const bool DHT22_ENABLE = true; // DHT22の有効化 trueで有効
const bool SV_DEMO_ENABLE =
    false; // サーボデモ動作を行うときはtrue、通常はfalseにすること
const bool SV_INVERT_MODE =
    true;                      // サーボの出力に反転バッファを入れるときはTrue
const bool VBAT_ENABLE = true; // バッテリー電圧監視の有効化 trueで有効
const bool WIFI_ENABLE = true; // WLANの有効化 trueで有効
const bool DID_ENABLE = true;  // deviceID情報付加の有効化 trueで有効
const bool WIFI_ENABLE_ONSTART = false; // 開始時にWLANを有効化するか trueで有効
const bool I2C_ENABLE = true;           // I2C通信有効化　trueで有効
const bool I2C_PWR_SAVING =
    true; // ドアクローズ時はI2C通信を行わない。trueで有効
const bool ENABLE_GPS_TIME = true;  // GPS時刻を取得
const bool ENABLE_DEEPSLEEP = true; // DEEPSLEEPの許可

// ハイバネーション用
const uint32_t LOW_VOLTAGE_THRESHOLD_DEFAULT =
    6000; // ハイバネーション開始閾値電圧(mV)　※起動時のみチェック。BTモードでバッテリー電圧がこれ以下のときは起動しない（DEEPSLEEP)
// const uint32_t LOW_VOLTAGE_CHECK_WAIT = 10;          // LOW
// VOLTAGEチェック間隔（秒）

#define WAKE_UP_PIN ACBT
const uint32_t VOLTAGE_CALIBRATION_DEFAULT = 0;

// POL用タイマー値を分単位で定義    ※POLL_TIMER値はconfig.jsonに移動
const int POLL_TIMER_VALUE_DEFAULT = 10;

// 時刻取得用タイマー値
const uint8_t SERVERTIME_UPDATE_PERIOD =
    144; // 24時間に何回取得するか。24ｘ60の約数にしておくこと
const uint8_t TC_UPDATE_DAYS_DEFAULT =
    1; // 何日に1回アップデートするかのデフォルト値。config.jsonで設定
const unsigned int TC_SERVER_TIMEOUT_VALUE = 30; // TimecodeServerのタイムアウト
const unsigned int TC_GPS_TIMEOUT_VALUE = 10; // サーバー時刻タイムアウト（秒）
const unsigned int TIMECODE_MASK_TIME = 1 * 60 * 1000; // 1分
// const unsigned int TIMECODE_MASK_TIME = 10 * 1000; // 10秒
// const unsigned long TC_TIMER_VALUE_UPDATE = 120 * 60 * 1000; // 120分
// const unsigned long TC_TIMER_VALUE_RETRY = 1 * 60 * 1000;

// NSI
const unsigned int CHECK_NSI_TIMEOUT = 10 * 1000; // NSI受信TIMEOUT
const unsigned int CHECK_NSI_INTERVAL = 1000;     // NSIリクエスト間隔
const unsigned int CHECK_NSI_MAXCOUNT = 20;       // NSIリクエスト最大回数

const unsigned int MODEM_POWER_ON_WAIT = 20; // MODEM起動待ちWAIT

const unsigned int JA_SEND_DELAY =
    5000; // JA関連コマンドの連続送信を防ぐためのWAIT

// DRCL/DROPコマンドのSEND
// DELAY（短時間にドアが開閉されたときにSENDQUEがたまってしまう問題の対策
const unsigned long DRCOMMAND_SEND_DELAY = 1 * 1000;

// 解錠・施錠確認用タイマー
const unsigned long LOCK_TO_TIMER =
    3 * 1000; // ロックが規定時間以内に完了するかのタイマー値（ミリ秒）

// EQ用タイマー
const unsigned long EQ_RESET_TIMER = 2 * 1000; // ミリ秒

// DoorError監視用タイマー
const unsigned long DERRTIMER = 5 * 1000; // ミリ秒
// const unsigned long DERRTIMER = 1 * 1000; // ミリ秒

// 自動ロック用タイマー
const unsigned long AUTOLOCK_DELAY_DEFAULT =
    5; // ミリ秒 config.jasonが未設定の場合のみ使われる

// 定数の定義
const unsigned int DATA_SIZE = 256; // 送受信バッファサイズ

// キューサイズ
const UBaseType_t sendQueueSize =
    30; // 送信用キュー（連打でキューあふれすると動作が遅くなるので注意）
const UBaseType_t cbx3ControlQueueSize = 10; // Cocobox動作制御用キュー

// 送受信コマンド定義  Command Definition
// #define SEND_COMMAND_FREE "AT@FREESPACE="

// 設定ファイル用構造体
struct configSetting {
  String command;
  int value;
};

// 受信コマンド
#define RCV_COMMAND_JRST "JRST"   // JA信号のリセット
#define RCV_COMMAND_CHECK "CHECK" // 状態チェック
#define RCV_COMMAND_RESET "RESET" // ソフトウエアRESET
// #define RCV_COMMAND_WIFION "@FREESPACE:WIFI_ON"     // WiFi起動
// #define RCV_COMMAND_WIFIOFF "@FREESPACE:WIFI_OFF"   // WiFi停止
#define RCV_COMMAND_SET "SET" // 鍵状態問い合わせ
#define RCV_COMMAND_MODEMSTATE "MODEMSTATE" // MODEM状態の確認
#define RCV_GPSTIME "+CCLK:"  // MODEMからの時刻情報(CCLK)
#define RCV_SERVERTIME "UT_"  // サーバー時刻の受信
#define RCV_ANSWERBACK "AT"   // アンサーバック識別用
#define RCV_OK "OK"           // モデムからのアンサー；OK
#define RCV_ERROR "ERROR"     // モデムからのアンサー；ERROR

// 送信コマンド
#define SEND_COMMAND_DR_OPEN "DR=OP"     // ドア＝開
#define SEND_COMMAND_DR_CLOSE "DR=CL"    // ドア＝閉
#define SEND_COMMAND_KEY_UNLOCKED "UL=T" // ロック＝解錠
#define SEND_COMMAND_KEY_LOCKED "UL=F"   // ロック＝施錠
#define SEND_COMMAND_EQ_ON "EQ=T"        // 地震検知あり
#define SEND_COMMAND_EQ_OF "EQ=F"        // 地震検知なし
#define SEND_COMMAND_PWR_AC "P=A"        // 電源＝AC
#define SEND_COMMAND_PWR_BT "P=B"        // 電源＝バッテリー
#define SEND_COMMAND_WON "WL=T"          // WIFI=ON
#define SEND_COMMAND_WOF "WL=F"          // WIFI=OFF

#define SEND_COMMAND_TMP "TMP=" // 温度
#define SEND_COMMAND_HUM "HMY=" // 湿度
#define SEND_COMMAND_WIFI "QR=" // QRメッセージ用

// lockCommand受信ステータス
enum class LockCommandStatus {
  lockCommand_LTE,    // LTEからの施錠・解錠指示
  lockCommand_WIFI,   // WiFiからの施錠・解錠指示
  lockCommand_MANUAL, // 手動施錠・解錠
  lockerror
};

// LOCK/UNLOCKの動作Reason
enum class LockReason {
  LTE,           // LTEからの施錠・解錠指示
  WIFI,          // WiFiからの施錠・解錠指示
  MANUAL,        // 手動施錠・解錠
  SELF_AUTOLOCK, // 自己自動施錠
  LTE_AUTOLOCK,  // 自動施錠（サーバー指示）
};

struct StatusData // WiFi用ステータスデータ
{
  String FWVersion;
  String timecode;
  bool isKSUexist;
  bool isKeyLocked;
  bool isDoorClosed;
  bool isACPower;
  float temperature;
  float humidity;
  float BatteryVolt;
};

#define SEND_COMMAND_HUM "HMY=" // 湿度
#define SEND_COMMAND_EQ "EQ=F"  // 地震計用
#define SEND_COMMAND_WIFI "QR=" // QRメッセージ用

// CommandTypeの列挙型定義
enum CommandType {
  BGN,
  POLL,
  CHK,
  LOCK,
  UNLOCK,
  ULER,
  PWAC,
  PWBT,
  DRCL,
  DROP,
  DRER,
  KGUL,
  KGLK,
  WFUL,
  WFLK,
  EQON,
  EQOF,
  RST,
  WLON,
  WMSG,
  WLOF,
  KBOX,
  SET,
  ATCOM,
  WHAT_THE_TIME,
  LOG,
  ATLK,
  ALRT,
  // 以下、JA関連の電文発行用
  PB01,
  PB02,
  JAON,
  JRST,
  END,
  // 以下、LOCK/UNLOCKキャンセル時の電文発行用
  LOCK_C,
  UNLOCK_C,
  ALRT_C,
  WFUL_C,
  WFLK_C,
  NODATA,
};

// CommandTypeと文字列のマップ
static const std::map<CommandType, const char *> commandTypeMap = {
    {BGN, "BGN"},
    {POLL, "POLL"},
    {CHK, "CHK"},
    {LOCK, "ULOF"},
    {UNLOCK, "ULON"},
    {ULER, "ULER"},
    {PWAC, "PWAC"},
    {PWBT, "PWBT"},
    {DRCL, "DRCL"},
    {DROP, "DROP"},
    {DRER, "DRER"},
    {KGUL, "KGUL"},
    {KGLK, "KGLK"},
    {WFUL, "KGUL"},
    {WFLK, "KGLK"},
    // {WFUL, "WFUL"},
    // {WFLK, "WFLK"},
    {EQON, "EQON"},
    {EQOF, "EQOF"},
    {RST, "RST"},
    {WLON, "WLON"},
    {WMSG, "WMSG"},
    {WLOF, "WLOF"},
    {KBOX, "KBOX"},
    {ATCOM, "ATCOMMAND"},
    {LOG, "LOG"},
    {ATLK, "ATLK"}, // AUTOLOCK
    {ALRT, "ALRT"}, // AUTOLOCK(サーバー指示)
    {END, "END"},   // ハイバネーション

    // 以下、LOCK/UNLOCKキャンセル時の電文発行用
    {LOCK_C, "ULOF"},
    {UNLOCK_C, "ULON"},
    {ALRT_C, "ALRT"},
    {WFUL_C, "KGUL"},
    {WFLK_C, "KGLK"},

    // 以下、JA用
    {JAON, "JAON"},
    {JRST, "JRST"},
};

// CommandTypeを文字列に変換する関数の定義
inline const char *CommandTypeToString(CommandType cmd) {
  auto it = commandTypeMap.find(cmd);
  if (it != commandTypeMap.end()) {
    return it->second;
  } else {
    return "UNKNOWN";
  }
}

// CocoBox制御コマンドコード
enum CocoBoxControlCode {
  LTE_NODATA = 0,
  LTE_UNKNOWN = 1,
  LTE_LOCK = 2,
  LTE_UNLOCK = 3,
  LTE_CHECK = 4,
  LTE_RESET = 5,
  LTE_WIFION = 7,
  LTE_WIFIOFF = 8,
  LTE_KCHK = 9,
  LTE_OK = 10,
  LTE_ERROR = 11,
  LTE_ANSWERBACK = 12,
  LTE_SET = 13,
  LTE_TIMECODE = 15,
  LTE_GPSTIME = 16,
  LTE_SERVERTIME = 17,
  LTE_JRST = 21,
  LTE_MODEMSTATE = 23,
};

// CocoBox制御コマンド構造体
struct CocoBoxControlCommands {
  CocoBoxControlCode code;
  String message;
};

// legacy enum name mapping for compatibility if needed
typedef CocoBoxControlCode CocoBoxControlCommandsEnum;
