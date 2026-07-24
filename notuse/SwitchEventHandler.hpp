/*
スイッチ制御 (RTOS版)

517Factory

2025/02/01    -SWITCH_OFF_STABLE/SWITCH_ON_STABLEを追加。状態がSTABLE_TIME継続時に通知する機能
              -CODE整理
              -混乱するのでNO/NC設定を削除　#defineフラグで整理する
*/
#pragma once

#include <Arduino.h>
#include <FreeRTOS.h>
#include "FreeRTOS_cbx.hpp"
#include "Debug.h"

// イベントタイプ
enum SwitchEvent
{
  SWITCH_OFF,
  SWITCH_ON,
  LONG_PUSH
};

// スイッチイベントハンドラークラスの定義
class SwitchEventHandler
{
public:
  using CallbackFunction = void (*)(SwitchEvent);

  SwitchEventHandler(int pinNumber);
  void begin();
  void handleSwitchTask();
  SwitchEvent getEvent();
  bool getState();
  void setCallback(CallbackFunction callback);
  void setLongPush(bool enable, bool logic);

private:
  // bool isChanged();
  void getLongPush();
  const int pin;                           // スイッチが接続されているピン番号
  bool currentState;                       // 現在のスイッチ状態
  bool lastState;                          // 前回のスイッチ状態
  TickType_t lastEventTime;                // 前回のイベント発生時刻
  TaskHandle_t taskHandle;                 // タスクハンドル
  CallbackFunction callback;               // コールバック関数
  void sendEvent(SwitchEvent event);       // イベントを送信
  const int DEBOUNCE_DELAY_MS = 50;        // デバウンス確認時間
  const int LONG_PRESS_DURATION_MS = 2000; // 長押し検知時間
  bool enableLongPush;
  bool longPushLogic;
  bool checkLongPush;
};
