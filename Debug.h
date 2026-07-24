/**
 *  for Debug
 *
 *  517Factory
 * */

#pragma once
#include "Arduino.h"
#include "esp_check.h"

// #define CBX3_LOG_LEVEL LOG_DBG
#define CBX3_LOG_LEVEL LOG_INF
#define LOGBUFSIZE 256
#define LOG_SAVE_SIZE 20 // 20ログ分を記録

#define STACK_MEM_MIN 100   // スタックメモリ残量警告しきい値
#define HEAP_MEM_MIN 100000 // ヒープメモリ残量警告しきい値

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
bool cbx3_errchk(LogLevel level, esp_err_t e, const char *txt);
void cbx3_memory_check(void);
void cbx3_memory_print(void);
char *replaceData4Disp(const char *buf);
void cbx_sleep(const char *reason);
void setString2Char(char *dest, const String &src, size_t destSize);
// void readFromCircularBuffer();