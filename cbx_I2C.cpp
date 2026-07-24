/*
I2C制御

517Factory
*/

#include "cbx_I2C.hpp"

bool I2C_start(uint8_t _sdaPin, uint8_t _sclPin)
{
    // SDAとSCLピンを設定
    Wire.setPins(_sdaPin, _sclPin);

    // I2C通信を開始、成功すればtrueを返す
    if (Wire.begin())
    {
        return true;
    }
    else
    {
        // 初期化に失敗した場合のエラーハンドリング
        cbx3_log(LOG_ERR, "Failed to start I2C");
        return false;
    }
}

bool I2C_checkDeviceExists(uint8_t _address)
{
    {
        Wire.beginTransmission(_address);
        uint8_t error = Wire.endTransmission();
        return (error == 0);
    }
}
