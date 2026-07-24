/*
LED制御 (RTOS版)

517Factory
*/

#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "header.h"
#include "Debug.h"
#include "FreeRTOS_cbx.hpp"

// LED ON/OFF定義　※ESP32用基板は反転バッファが入っているので論理反転させている
#define LED_ON false
#define LED_OFF true

enum LEDMode
{
  OFF,
  ON,
  ON_Delay, // ON+ディレイ付き（1定時間つけたい場合）
  BLINK_SLOW,
  BLINK_MEDIUM,
  BLINK_FAST,
  BEGIN,
};

const int ON_D_TIME = 1000; // ディレイ付きの場合の持続時間

struct CycleDuty
{
  int cycle;
  int duty;
};

class LEDCont
{
private:
  int pin;
  LEDMode mode;
  TaskHandle_t taskHandle;
  static void ledTask(void *parameter);

public:
  LEDCont(int pinNumber);
  void setMode(LEDMode modeValue);
  void setPinState(int state); // privateメンバへのアクセス用関数
};

// 定数としてモードセットを定義
constexpr CycleDuty blinkSlowSet = {1000, 50};  // 1000ms周期, 50%デューティ比
constexpr CycleDuty blinkMediumSet = {500, 75}; // 500ms周期, 75%デューティ比
constexpr CycleDuty blinkFastSet = {200, 25};   // 200ms周期, 25%デューティ比
