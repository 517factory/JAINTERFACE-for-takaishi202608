#include "BatteryChecker.h"

/*
バッテリー監視

2024/01/24
2024/11/04  ADC_CH1対応
517Factory
*/

BatChecker::BatChecker(int pinEnable, adc_channel_t ADC_CH)
    : pinBatCheck(pinEnable),
      pinVoltReadChannel(ADC_CH),
      adcHandle(nullptr),
      caliHandle(nullptr),
      doCali(false),
      VrefCalib(0)
{
    // Enable Pinmode設定
    pinMode(pinBatCheck, OUTPUT); // BAT CHK ENABLE信号
    digitalWrite(pinBatCheck, BATCHK_DISABLE);

    // 1. ADC Oneshot ユニット初期化
    adc_oneshot_unit_init_cfg_t init_config = {};
    init_config.unit_id = ADC_UNIT_1;
    init_config.clk_src = ADC_RTC_CLK_SRC_DEFAULT;
    adc_oneshot_new_unit(&init_config, &adcHandle);

    // 2. チャネル設定 (12bit / 12dB Atten)
    adc_oneshot_chan_cfg_t config = {};
    config.atten = ADC_ATTEN_DB_12;
    config.bitwidth = ADC_BITWIDTH_DEFAULT;
    adc_oneshot_config_channel(adcHandle, pinVoltReadChannel, &config);

    // 3. キャリブレーション初期化
#if CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32C3
    adc_cali_curve_fitting_config_t cali_config = {};
    cali_config.unit_id = ADC_UNIT_1;
    cali_config.chan = pinVoltReadChannel;
    cali_config.atten = ADC_ATTEN_DB_12;
    cali_config.bitwidth = ADC_BITWIDTH_DEFAULT;
    if (adc_cali_create_scheme_curve_fitting(&cali_config, &caliHandle) == ESP_OK)
    {
        doCali = true;
    }
#else
    adc_cali_line_fitting_config_t cali_config = {};
    cali_config.unit_id = ADC_UNIT_1;
    cali_config.atten = ADC_ATTEN_DB_12;
    cali_config.bitwidth = ADC_BITWIDTH_DEFAULT;
    if (adc_cali_create_scheme_line_fitting(&cali_config, &caliHandle) == ESP_OK)
    {
        doCali = true;
    }
#endif
}

BatChecker::~BatChecker()
{
    if (caliHandle)
    {
#if CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32C3
        adc_cali_delete_scheme_curve_fitting(caliHandle);
#else
        adc_cali_delete_scheme_line_fitting(caliHandle);
#endif
    }
    if (adcHandle)
    {
        adc_oneshot_del_unit(adcHandle);
    }
}

float BatChecker::milliVoltRead()
{
    uint32_t raw_data = ReadRawData();
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

    for (int i = 0; i < numSamples; i++)
    {
        int raw_value = 0;
        if (adcHandle)
        {
            adc_oneshot_read(adcHandle, pinVoltReadChannel, &raw_value);
        }
        rawValues[i] = (uint32_t)raw_value;
        total += (uint32_t)raw_value;

        if ((uint32_t)raw_value > max)
        {
            max = (uint32_t)raw_value;
        }
        if ((uint32_t)raw_value < min)
        {
            min = (uint32_t)raw_value;
        }

        cbx_wait(10); // サンプル間の安定化待機（10ミリ秒）
    }

    cbx_wait(READ_POST_DELAY);
    digitalWrite(pinBatCheck, BATCHK_DISABLE); // アナログ読み取りのDisable

    ave = total / numSamples;

    std::sort(rawValues, rawValues + numSamples);
    if (numSamples % 2 == 0)
    {
        median = (rawValues[numSamples / 2 - 1] + rawValues[numSamples / 2]) / 2;
    }
    else
    {
        median = rawValues[numSamples / 2];
    }

    return median;
}

uint32_t BatChecker::Raw2milliVolt(uint32_t rd)
{
    int voltage_mv = 0;
    if (doCali && caliHandle)
    {
        adc_cali_raw_to_voltage(caliHandle, (int)rd, &voltage_mv);
    }
    else
    {
        voltage_mv = (int)((rd * 3300) / 4095);
    }
    return (uint32_t)voltage_mv;
}

uint32_t BatChecker::ConvertMilliVolt(uint32_t milliVoltRaw) // 抵抗分圧分の補正
{
    float coef = (float)(R4) / (float)(R3 + R4); // 抵抗分圧係数
    uint32_t vcorrect = milliVoltRaw / coef;
    return vcorrect;
}
