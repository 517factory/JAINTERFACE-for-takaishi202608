/*
 * 鍵ユニット制御（RTOS版）
 * 使う時は先にI2Cが動いていること。
 *
 * 517Factory
 */

#include "KeyUnitCont.hpp"

KeyUnitCont::KeyUnitCont(uint8_t address)
    : _address(address), _taskHandle(nullptr), _callback(nullptr), sameDataCount(0)
{
    previousUIDs = "DUMMY"; // 初回出すために仮データをいれておく
    currentUIDs = "";
    // Wire.setPins(_sdaPin, _sclPin);
    // Wire.begin();
}

bool KeyUnitCont::begin()
{
    // 鍵ユニットのモードをTagIDに設定
    Wire.beginTransmission(_address);
    Wire.write((const uint8_t *)"id", 2);   // 応答モードを TagID に設定
    uint8_t error = Wire.endTransmission(); // 戻り値をエラーチェックに使用

    // エラーハンドリング
    if (error == 0)
    {
        deviceExist = true; // デバイスが存在する場合
        cbx3_log(LOG_DBG, "Key Unit communication successful");
        return true;
    }
    else
    {
        deviceExist = false; // デバイスが存在しない場合
        cbx3_log(LOG_ERR, "I2C communication failed, error code: %d", error);
        return false;
    }
}

void KeyUnitCont::startTask()
{
    if (deviceExist)
    {
        xTaskCreateUniversal(
            communicationTask,   // タスク関数
            "CommunicationTask", // タスクの名前
            10000,               // スタックサイズ
            this,                // 引数としてオブジェクトのポインタを渡す
            I2C_PRIORITY,        // 優先度
            &_taskHandle,        // タスクハンドル
            tskNO_AFFINITY);
    }
}

void KeyUnitCont::suspendTask()
{
    if (_taskHandle != nullptr)
    {
        vTaskSuspend(_taskHandle);
    }
}

void KeyUnitCont::resumeTask()
{
    if (_taskHandle != nullptr)
    {
        vTaskResume(_taskHandle);
    }
}

void KeyUnitCont::setTagChangeCallback(TagChangeCallback callback)
{
    if (deviceExist)
    {
        _callback = callback;
    }
}

String KeyUnitCont::getCurrentTagIDs()
{
    return currentUIDs;
}

void KeyUnitCont::communicationTask(void *pvParameters)
{
    KeyUnitCont *self = static_cast<KeyUnitCont *>(pvParameters);
    while (1)
    {
        self->getTagInfo();

        if (self->hasTagInfoChanged())
        {
            self->sameDataCount++;

            if (self->sameDataCount == REQUIRED_SAME_COUNT)
            {
                if (self->_callback)
                {
                    self->sameDataCount = 0; // 同じデータのカウンタをリセット
                    self->_callback(self->currentUIDs);
                }
                self->previousUIDs = self->currentUIDs; // `sameDataCount`をリセットした後に`previousUIDs`を更新
            }
        }
        else
        {
            self->sameDataCount = 0;
        }

        // 待機
        vTaskDelay(I2C_INTERVAL / portTICK_PERIOD_MS);
    }
}

void KeyUnitCont::getTagInfo()
{
    Wire.requestFrom(static_cast<uint8_t>(_address), static_cast<uint8_t>(128));
    String ids = "";
    while (Wire.available())
    {
        char i2cbuf = Wire.read();
        if (i2cbuf != 0x0d) // 改行コードを無視
        {
            ids.concat(i2cbuf);
        }
    }
    currentUIDs = ids;
}

bool KeyUnitCont::hasTagInfoChanged()
{
    if (currentUIDs != previousUIDs)
    {
        // cbx3_log(LOG_DBG, "UIDs changed: ->%s", previousUIDs.c_str()); // デバッグログ追加
        // cbx3_log(LOG_DBG, "UIDs changed: <-%s", currentUIDs.c_str());  // デバッグログ追加
        return true;
    }
    return false;
}

bool KeyUnitCont::isTaskRunning()
{
    if (_taskHandle != NULL)
    {
        eTaskState state = eTaskGetState(_taskHandle);
        if (state == eRunning)
        {
            return true;
        }
    }
    return false; // タスクが実行中ではない、またはタスクハンドルが無効な場合に false を返す
}
