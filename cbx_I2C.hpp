/*
I2C制御

517Factory
*/
#pragma once

#include <Wire.h>
#include "Debug.h"
#include "cbx_I2C.hpp"

bool I2C_start(uint8_t _sdaPin, uint8_t _sclPin);
bool I2C_checkDeviceExists(uint8_t _address);

// #define I2C_INTERVAL 1000     // タスクの待機時間（ミリ秒）
// #define REQUIRED_SAME_COUNT 5 // 同じデータが何回連続して受信されたら変更と見なすか

// typedef void (*TagChangeCallback)(const String &);

// class I2C_Cont
// {
// public:
//   KeyUnitCont(uint8_t address, uint8_t sdaPin, uint8_t sclPin);
//   void begin();
//   void startTask();
//   void suspendTask();
//   void resumeTask();
//   void setTagChangeCallback(TagChangeCallback callback);
//   String getCurrentTagIDs(); // 修正: 文字列を返す
//   bool isTaskRunning();
//   bool deviceExist = false;

// private:
//   uint8_t _address;
//   uint8_t _sdaPin;
//   uint8_t _sclPin;
//   TaskHandle_t _taskHandle;
//   TagChangeCallback _callback;
//   int sameDataCount;

//   String previousUIDs;
//   String currentUIDs;

//   static void communicationTask(void *pvParameters);
//   void getTagInfo();
//   bool hasTagInfoChanged();
//   bool checkDeviceExists();
// };
