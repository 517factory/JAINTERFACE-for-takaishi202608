/*
LED制御 (RTOS版)

517Factory
*/

#include "LEDCont.h"

void LEDCont::ledTask(void *parameter)
{
    LEDCont *led = (LEDCont *)parameter;
    while (true)
    {
        cbx_wait(1);
        switch (led->mode)
        {
        case LEDMode::OFF:
            if (digitalRead(led->pin) != LED_OFF)
            { // 状態が異なる場合のみ変更
                led->setPinState(LED_OFF);
            }
            break;
        case LEDMode::ON:
            if (digitalRead(led->pin) != LED_ON)
            { // 状態が異なる場合のみ変更
                led->setPinState(LED_ON);
            }
            break;
        case LEDMode::ON_Delay: // 一定時間つけたい場合
            led->setPinState(LED_ON);
            cbx_wait(ON_D_TIME);
            led->setMode(LEDMode::OFF);
            break;
        case LEDMode::BLINK_SLOW:
            digitalWrite(led->pin, LED_ON);
            vTaskDelay(pdMS_TO_TICKS(blinkSlowSet.cycle * blinkSlowSet.duty / 100));
            digitalWrite(led->pin, LED_OFF);
            vTaskDelay(pdMS_TO_TICKS(blinkSlowSet.cycle - (blinkSlowSet.cycle * blinkSlowSet.duty / 100)));
            break;
        case LEDMode::BLINK_MEDIUM:
            digitalWrite(led->pin, LED_ON);
            vTaskDelay(pdMS_TO_TICKS(blinkMediumSet.cycle * blinkMediumSet.duty / 100));
            digitalWrite(led->pin, LED_OFF);
            vTaskDelay(pdMS_TO_TICKS(blinkMediumSet.cycle - (blinkMediumSet.cycle * blinkMediumSet.duty / 100)));
            break;
        case LEDMode::BLINK_FAST:
            digitalWrite(led->pin, LED_ON);
            vTaskDelay(pdMS_TO_TICKS(blinkFastSet.cycle * blinkFastSet.duty / 100));
            digitalWrite(led->pin, LED_OFF);
            vTaskDelay(pdMS_TO_TICKS(blinkFastSet.cycle - (blinkFastSet.cycle * blinkFastSet.duty / 100)));
            break;
        case LEDMode::BEGIN:
            break;
        }
    }
}

LEDCont::LEDCont(int pinNumber)
{
    Serial.begin(115200);
    pin = pinNumber;
    mode = LEDMode::OFF;
    pinMode(pin, OUTPUT);
    xTaskCreateUniversal(ledTask, "LedTask", 2048, this, LED_PRIORITY, &taskHandle, tskNO_AFFINITY);
}

void LEDCont::setMode(LEDMode modeValue)
{
    if (mode != modeValue) // 現在の設定と同じ場合スキップ
    {
        mode = modeValue;
    }
}

void LEDCont::setPinState(int state)
{ // privateメンバへのアクセス用関数
    digitalWrite(pin, state);
}
