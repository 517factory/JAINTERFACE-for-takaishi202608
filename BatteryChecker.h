#pragma once

#include "Arduino.h"
#include "header.h"
#include "Debug.h"
#include <algorithm> // std::sort()を使用するために必要

/**
 *  BatteryChecker
 *
 *  2024/01/24
 *  517Factory
 * */

#include "driver/adc.h"
#include "esp_adc_cal.h"

#define READ_PRE_DELAY 10  // ENABLE ONから測定までのディレイ
#define READ_POST_DELAY 10 // 測定からENABLE OFFまでのディレイ

// バッテリー監視用フラグ ※ESP32版の基板は反転バッファ入っているためロジック反転
#define BATCHK_ENABLE LOW
#define BATCHK_DISABLE HIGH

// const float REF_VOLT = 3.3f;           // リファレンス電圧
// const float RESOLUTION = 4095.0f;      // AI分解能 ※4069ではなく4095なので注意
const float FULL_SCALE_VOLTAGE = 2.2f; // 6 dB attenuation (ADC_ATTEN_DB_6) gives full-scale voltage 2.2 V
const uint32_t VREF = 1100;

const uint32_t R3 = 3300; // フェアR14
const uint32_t R4 = 1000; // フェアR17

// Attenuator
#define ADC_ATT ADC_ATTEN_DB_12

class BatChecker
{
public:
  BatChecker(int pinEnable, adc1_channel_t ADC_CH, uint32_t lowVolTh);
  float milliVoltRead();
  void PrintData();
  int VrefCalib;

private:
  int pinBatCheck;
  const int numSamples = 10; // 読み取りのサンプル数（平均するため）
  adc1_channel_t pinVoltReadChannel;
  esp_adc_cal_characteristics_t adcChar;
  uint32_t ReadRawData();                           // 生データ取得（0-4095）
  uint32_t Raw2milliVolt(uint32_t rd);              // 生データからミリボルトへ
  uint32_t ConvertMilliVolt(uint32_t milliVoltRaw); // 抵抗分圧分の補正
};
