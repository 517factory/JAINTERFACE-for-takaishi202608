#pragma once

#include "Arduino.h"
#include "header.h"
#include "Debug.h"
// #include "driver/ledc.h" //ハードウエアタイマードライバー

/**
 *  Lock System Control
 *
 *  517Factory
 * */

// LECD関連
const int LEDC_CHANNEL = 1;                       // LEDC CHANNNEL MAX16chがサポートされている（15まで）
const int LEDC_RESOLUTION = 10;                   // LEDC分解能（MAX 16bit）
const int LEDC_FREQ = 50;                         // LEDCの周波数　サーボ用50Hz
const int LEDC_PERIOD_uSec = 1000000 / LEDC_FREQ; // 50Hzに基づく20ms -> 20,000us

// パルス幅（マイクロ秒）を定義
const int MIN_PULSE_uSec = 500; //-90度時パルス幅                                                             // 0度時のパルス幅 (500us)
const int MAX_PULSE_uSec = 2500;
const int MAX_DUTY_CYCLE = (1 << LEDC_RESOLUTION) - 1;                          // 最大デューティサイクル値 (1023)
const int MIN_PULSE_PWM = (MIN_PULSE_uSec * MAX_DUTY_CYCLE) / LEDC_PERIOD_uSec; // -90°で26
const int MAX_PULSE_PWM = (MAX_PULSE_uSec * MAX_DUTY_CYCLE) / LEDC_PERIOD_uSec; // +90°で123

// LOCK/UNLOCK設定用変数
const bool SET_SV_LK = true;
const bool SET_SV_UL = false;

//  サーボ制御継続時間（msec）
#define SV_CONT_TIME 1000 // サーボ制御持続時間 1秒＝1000
#define CHK_DELAY 50      // LSをチェックするまでのディレイ

// SERVO POSITION
const int SV_MARGIN = 8; // オーバーターン防止用マージン　カム取り付け精度より　7.5°→ 8°で設定
const int SERVO_NEUTRAL = 0;
const int SERVO_LOCK = 90 - SV_MARGIN;
const int SERVO_UNLOCK = -(90 - SV_MARGIN);

// #include "header.h"

class LockSystem
{
public:
  LockSystem(int pinSV, bool interlocksw, bool _isInvert);
  void locksysDriveServo(int angle);
  void KGLock(bool mode, bool allow);
  void KGNtral();

private:
  int pinSV;
  bool EnableDoorIntlk;  // インターロックあり・なしの大元スイッチ　→header.h
  bool isInvert = false; // 反転バッファ使用時はTrueに設定する
  void servoStop();
};
