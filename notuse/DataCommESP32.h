#pragma once

/**
 *  Data Communication for ACM5102
 *  for ESP32-S3
 *
 *  517Factory
 * */
#include "Arduino.h"
#include "Debug.h"
#include "header.h"

// SendData構造体の定義
struct SendDatas
{
  CommandType cmd_type;
  bool st_isKeyLocked;
  bool st_isDoorClosed;
  bool st_isEqOn;
  String DHT22msg;
  bool st_isPwrAC;
  float vbat;
  bool st_isWiFiOn;
  String counter;
  const char *rcvd_cmd; // Wifi用のコマンド
  String UIDs;          // SetSndDataKeyState用
  String AT_cmd_main;   // ATコマンド送信用
  String LogMsg;
  uint32_t deviceID;
};

enum NSI_Type
{
  SRV_NONE,
  NO_SRV,
  LIMITED,
  IN_SRV,
  TIMEOUT,
  UNKNOWN,
};

const int MAX_DEVIDE_LINES = 5; // 1行を分割する際の最大分割数

class DataCommESP32
{
public:
  void EncodeSndData(const SendDatas &dataSet);
  std::vector<CocoBoxControlCommands> ChkRcvData(const char *buff);
  char DataBuff[DATA_SIZE];
  configSetting decodeConfigSetting(const char *buff);
  NSI_Type decodeNSI(const String &input);
  String NSI_buff;

private:
  void EncodeSndDataMain(const SendDatas &dataSet);
  void EncodeSndDataWifiMsg(const SendDatas &dataSet);
  void EncodeSndDataKeyState(const SendDatas &dataSet);
  void EncodeSndDataATCommand(const SendDatas &dataSet);
  void EncodeSndDataLogMsg(const SendDatas &dataSet);
  void EncodeSndDataWhatTheTime(const SendDatas &dataSet);
  void AsciiToHex(char *strSrc);
  void AsciiToHexFromString(const String &strSrc);
  char charHex[DATA_SIZE];
  int splitString(const String &str, String result[]);
  NSI_Type parseNsiToken(const String &token);
};
