#pragma once

/*
 * Timecode for ESP32-S3
 *
 * 517Factory
 */

#include "Arduino.h"
#include "Debug.h"
#include "header.h"
#include <CRCx.h>
#include <time.h>

// #define SERVER_TIME_STR 'S'
// #define GPS_TIME_STR 'G'
#define LTE_TIME_STR 'L'
#define ELPS_TIME_STR 'E'

// タイムゾーンのオフセット（秒単位）
// 例: 日本標準時（JST）はUTC+9なので9時間分（9 * 3600）を指定
#define TIMEZONE_OFFSET_SECONDS (9 * 3600)

enum TIMECODEMODE
{
  SERVER_TIME = 0,
  LTE_TIME = 1,
  ELPS_TIME = 2,
};

struct Time
{
  int year;
  int month;
  int day;
  int hour;
  int minute;
  int second;
  int timezone;
};

class TimeCode
{
public:
  TimeCode(void);
  bool setLteTimeCode(const char *buff);
  bool setServerTimeCode(const char *buff);
  String getTimeCode();
  String time2string(uint8_t hour, uint8_t min);
  uint8_t timeMode = ELPS_TIME; // 使用TimeCode識別
  void updateTime();
  Time currentTime;
  int timeDifference = 0;

private:
  Time initialTime;
  unsigned long startMillis; // 計測起点のミリ秒
};
