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

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

// 旧規格マクロのフォールバック定義
#ifndef ADC1_CHANNEL_7
#define ADC1_CHANNEL_0 ADC_CHANNEL_0
#define ADC1_CHANNEL_1 ADC_CHANNEL_1
#define ADC1_CHANNEL_2 ADC_CHANNEL_2
#define ADC1_CHANNEL_3 ADC_CHANNEL_3
#define ADC1_CHANNEL_4 ADC_CHANNEL_4
#define ADC1_CHANNEL_5 ADC_CHANNEL_5
#define ADC1_CHANNEL_6 ADC_CHANNEL_6
#define ADC1_CHANNEL_7 ADC_CHANNEL_7
#define ADC1_CHANNEL_8 ADC_CHANNEL_8
#define ADC1_CHANNEL_9 ADC_CHANNEL_9
#endif

#ifndef adc1_channel_t
typedef adc_channel_t adc1_channel_t;
#endif

#define READ_PRE_DELAY 10  // ENABLE ONから測定までのディレイ
#define READ_POST_DELAY 10 // 測定からENABLE OFFまでのディレイ

// バッテリー監視用フラグ ※ESP32版の基板は反転バッファ入っているためロジック反転
#define BATCHK_ENABLE LOW
#define BATCHK_DISABLE HIGH

const float FULL_SCALE_VOLTAGE = 2.2f; // 6 dB attenuation (ADC_ATTEN_DB_6) gives full-scale voltage 2.2 V
const uint32_t VREF = 1100;

const uint32_t R3 = 3300; // フェアR14
const uint32_t R4 = 1000; // フェアR17

// Attenuator
#define ADC_ATT ADC_ATTEN_DB_12

class BatChecker
{
public:
  BatChecker(int pinEnable, adc_channel_t ADC_CH);
  ~BatChecker();
  float milliVoltRead();
  void PrintData();
  int VrefCalib;

private:
  int pinBatCheck;
  const int numSamples = 10; // 読み取りのサンプル数（平均するため）
  adc_channel_t pinVoltReadChannel;
  adc_oneshot_unit_handle_t adcHandle;
  adc_cali_handle_t caliHandle;
  bool doCali;

  uint32_t ReadRawData();                           // 生データ取得（0-4095）
  uint32_t Raw2milliVolt(uint32_t rd);              // 生データからミリボルトへ
  uint32_t ConvertMilliVolt(uint32_t milliVoltRaw); // 抵抗分圧分の補正
};
