#include "BatteryChecker.h"

/*
バッテリー監視

2024/01/24
2024/11/04  ADC_CH1対応
517Factory
*/

BatChecker::BatChecker(int pinEnable, adc1_channel_t ADC_CH, uint32_t lowVolTh)
{
    // Enable Pinmode設定
    pinBatCheck = pinEnable;
    pinVoltReadChannel = ADC_CH;
    adc1_config_width(ADC_WIDTH_BIT_12); // 12bit (0-4095)
    adc1_config_channel_atten(pinVoltReadChannel, ADC_ATT);

    // pinMode(pinBatCheck, OUTPUT_OPEN_DRAIN); // BAT CHK ENABLE信号
    pinMode(pinBatCheck, OUTPUT); // BAT CHK ENABLE信号
    digitalWrite(pinBatCheck, BATCHK_DISABLE);

    // キャリブレーション情報を取得
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATT, ADC_WIDTH_BIT_12, VREF, &adcChar);
}

float BatChecker::milliVoltRead()
{
    uint32_t raw_data = ReadRawData();
    cbx3_log(LOG_DBG, "RAW DATA = %d", raw_data);
    uint32_t volt = ConvertMilliVolt(Raw2milliVolt(raw_data));
    volt += VrefCalib;
    return volt;
}

uint32_t BatChecker::ReadRawData()
{
    uint32_t total = 0;
    uint32_t ave;
    uint32_t max = 0;
    uint32_t min = 4095;
    uint32_t median;
    uint32_t rawValues[numSamples];

    digitalWrite(pinBatCheck, BATCHK_ENABLE); // アナログ読み取りのEnable
    cbx_wait(READ_PRE_DELAY);
    cbx3_log(LOG_DBG, "PIN=%d", pinBatCheck);

    for (int i = 0; i < numSamples; i++)
    {

        uint32_t raw_value = adc1_get_raw(pinVoltReadChannel); // 生のADC値を取得
        rawValues[i] = raw_value;                              // 配列に格納
        total += raw_value;
        cbx3_log(LOG_DBG, "RAW[%d] = %d", i, raw_value);

        // 最大値と最小値を更新
        if (raw_value > max)
        {
            max = raw_value;
        }
        if (raw_value < min)
        {
            min = raw_value;
        }

        // サンプリング間の遅延（必要に応じて調整）
        cbx_wait(10); // サンプル間の安定化待機（10ミリ秒）
    }

    cbx_wait(READ_POST_DELAY);
    digitalWrite(pinBatCheck, BATCHK_DISABLE); // アナログ読み取りのDisable
    // 平均値を計算して返す
    ave = total / numSamples;

    // 中央値の計算
    std::sort(rawValues, rawValues + numSamples);
    if (numSamples % 2 == 0)
    {
        median = (rawValues[numSamples / 2 - 1] + rawValues[numSamples / 2]) / 2;
    }
    else
    {
        median = rawValues[numSamples / 2];
    }

    // cbx3_log(LOG_INF, "AVE = %d max= %d, min= %d, MEDIAN = %d", ave, max, min, median);
    return median;
}

uint32_t BatChecker::Raw2milliVolt(uint32_t rd)
{
    // float voltage = VREF * (1.0f / ATT_CONST_0DB) * (float(rd) / RESOLUTION);
    uint32_t milliVolt = esp_adc_cal_raw_to_voltage(rd, &adcChar);
    cbx3_log(LOG_DBG, "RawVoltage = %d[mV]", milliVolt);
    return milliVolt;
}

uint32_t BatChecker::ConvertMilliVolt(uint32_t milliVoltRaw) // 抵抗分圧分の補正
{
    float coef = (float)(R4) / (float)(R3 + R4); // 抵抗分圧係数
    cbx3_log(LOG_DBG, "coef = %f", coef);
    uint32_t vcorrect = milliVoltRaw / coef;
    cbx3_log(LOG_DBG, "Voltage = %f[V]", vcorrect);
    return vcorrect;
}