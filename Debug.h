/**
 *  for Debug
 *
 *  517Factory
 * */

#pragma once
#include "Arduino.h"
#include "esp_check.h"

#define CBX3_LOG_LEVEL MDBG1
// #define CBX3_LOG_LEVEL MDBG2
// #define CBX3_LOG_LEVEL LOG_DBG
// #define CBX3_LOG_LEVEL LOG_INF

#define LOGBUFSIZE 256
#define MAX_ROTATION_LOGS 48     // 通常稼働ログの最大保存数（時間）
#define MAX_START_LOGS 3         // 起動時ログの最大保存数
#define MAX_LOG_FILE_LIST_SIZE 150 // 一覧・一括削除用配列の最大バッファサイズ（古いゾンビファイル対策で拡張）
#define FILE_LOG_INTERVAL_MIN 60 // ログファイルの切り替え間隔（分）。10 = 10分
#define MAX_SINGLE_LOG_SIZE (50 * 1024) // 1ファイルあたりの最大ログサイズ（50KB）。エラー急増時のディスクフル防止
#define MAX_LOG_TOTAL_SIZE (800 * 1024) // ログ全体の最大合計サイズ（800KB）

#define STACK_MEM_MIN 100  // スタックメモリ残量警告しきい値
#define HEAP_MEM_MIN 15000 // ヒープメモリ残量警告しきい値（ログ多発防止のため30000から引き下げ）

#define MONITOR_TX 4 // Serial2 Monitor TX
#define MONITOR_RX 5 // Serial2 Monitor RX

#define SerialDebug Serial
// #define SerialDebug Serial2

enum LogLevel
{
    LOG_ERR = 0, // エラー
    LOG_WAR = 1, // 警告
    LOG_INF = 2, // 情報
    LOG_DBG = 3, // デバッグ
    LOG_TST = 4, // テスト
    MDBG1 = 5,   // モデムデバッグ
    MDBG2 = 6,   // モデムデバッグ
    MDBG3 = 7,   // モデムデバッグ
};

void spDBG(char *fmt, ...);
void spDBGln(char *fmt, ...);
void cbx3_log(LogLevel level, const char *fmt, ...);

// Execute parsed serial command string
bool executeSerialCommand(const String &cmd);
bool cbx3_errchk(LogLevel level, esp_err_t e, const char *txt);
void cbx3_memory_check(void);
void cbx3_memory_check_always(void);
void cbx3_memory_print(void);
String replaceData4Disp(const char *buf);
void setString2Char(char *dest, const String &src, size_t destSize);
#include "header.h"
void cbx_restart(BootReason NewReason);
void cbx_sleep(const char *reason);
void cbx3_file_log_init(void);
void cbx3_file_log_start_loop(void);
void cbx3_file_log_flush(void);
void setMqttConnectedFlag(bool connected);

