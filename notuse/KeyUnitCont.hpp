/*
鍵ユニット制御（RTOS版）

517Factory
*/
#pragma once

#include <Wire.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "Debug.h"
#include "FreeRTOS_cbx.hpp"

#define I2C_INTERVAL 1000     // タスクの待機時間（ミリ秒）
#define REQUIRED_SAME_COUNT 5 // 同じデータが何回連続して受信されたら変更と見なすか

typedef void (*TagChangeCallback)(const String &);

class KeyUnitCont
{
public:
  KeyUnitCont(uint8_t address);
  bool begin();
  void startTask();
  void suspendTask();
  void resumeTask();
  void setTagChangeCallback(TagChangeCallback callback);
  String getCurrentTagIDs(); // 修正: 文字列を返す
  bool isTaskRunning();
  bool deviceExist = false;

private:
  uint8_t _address;
  TaskHandle_t _taskHandle;
  TagChangeCallback _callback;
  int sameDataCount;

  String previousUIDs;
  String currentUIDs;

  static void communicationTask(void *pvParameters);
  void getTagInfo();
  bool hasTagInfoChanged();
};
