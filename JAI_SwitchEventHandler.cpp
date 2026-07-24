/*
スイッチ制御 (RTOS版)

517Factory
*/
#include "JAI_SwitchEventHandler.hpp"

SwitchEventHandler::SwitchEventHandler(int pinNumber)
    : pin(pinNumber),
      currentState(false),
      lastState(false),
      lastEventTime(0),
      taskHandle(nullptr),
      enableLongPush(false),
      longPushLogic(true),
      callback(nullptr)
{
}

void SwitchEventHandler::begin()
{
    pinMode(pin, INPUT_PULLUP);
    BaseType_t result = xTaskCreateUniversal(
        [](void *param)
        {
            static_cast<SwitchEventHandler *>(param)->handleSwitchTask();
        },
        "Switch Task",
        8192,
        this,
        tskIDLE_PRIORITY + 1,
        &taskHandle,
        tskNO_AFFINITY);

    if (result != pdPASS)
    {
        cbx3_log(LOG_ERR, "Switch Task creation failed!");
    }
    currentState = getState();
    lastState = currentState;
}

void SwitchEventHandler::handleSwitchTask()
{
    while (true)
    {
        currentState = getState();

        if (currentState != lastState)
        {
            lastState = currentState; // 状態変化があった場合に lastState を更新
            if (currentState == HIGH)
            {
                sendEvent(SWITCH_HIGH);
                // cbx3_log(LOG_INF, "SWITCH ON");
                lastEventTime = xTaskGetTickCount();
            }
            else
            {
                sendEvent(SWITCH_LOW);
                // cbx3_log(LOG_INF, "SWITCH OFF");
                lastEventTime = xTaskGetTickCount();
            }
            // 指定されたlongPushLogicによってHIGH/LOWどちらで検知するか分岐
            checkLongPush = (currentState == longPushLogic);
        }
        else if (enableLongPush && checkLongPush)
        {
            getLongPush();
        }
        vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_DELAY_MS));
    }
}

bool SwitchEventHandler::getState()
{
    bool pinState = digitalRead(pin);
    return pinState;
}

void SwitchEventHandler::sendEvent(SwitchEvent e)
{
    if (callback)
    {
        callback(e, pin);
    }
}

// 長押し検知の設定（Public）　※これを呼び出さないとLongPush判断は実行されない
void SwitchEventHandler::setLongPush(bool enable, bool logic)
{
    enableLongPush = enable;
    longPushLogic = logic;
    checkLongPush = false; // 初期値をfalseに設定
}

// 長押しの検知
void SwitchEventHandler::getLongPush()
{
    cbx3_log(LOG_DBG, "CheckLongPush");
    if ((xTaskGetTickCount() - lastEventTime >= pdMS_TO_TICKS(LONG_PRESS_DURATION_MS)))
    {
        cbx3_log(LOG_INF, "LongPushDetected.");
        sendEvent(LONG_PUSH);
        checkLongPush = false;
    }
}

void SwitchEventHandler::setCallback(CallbackFunction cb)
{
    callback = cb;
}