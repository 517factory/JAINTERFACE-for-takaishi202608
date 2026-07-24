#include "LockSystemLEDC.h"

/*
LOCK機構制御

517Factory
*/

LockSystem::LockSystem(int _pinSV, bool interlocksw, bool _isInvert)
{
    pinSV = _pinSV;
    EnableDoorIntlk = interlocksw;
    isInvert = _isInvert; // 反転バッファ使用時用のフラグ　（反転バッファ使用時はTrueにする）

    // ledcのセットアップ
    // digitalWrite(pinSV, HIGH);                            // 念のためLOWに落としているだけ(バッファで反転しているので注意)
    // ledcSetup(LEDC_CHANNEL, LEDC_FREQ, LEDC_RESOLUTION); // チャンネル, 50Hz (サーボモーター用), LEDC分解能(bit)
    ledcAttachChannel(pinSV, LEDC_FREQ, LEDC_RESOLUTION, LEDC_CHANNEL);
    servoStop();
}

void LockSystem::KGLock(bool mode, bool allow)
{
    if (allow || EnableDoorIntlk == false) // ドアインターロック
    {
        if (mode == SET_SV_UL) // KG_UNLOCK
        {
            locksysDriveServo(SERVO_UNLOCK);  // サーボをUNLOCK位置に動かす
            vTaskDelay(pdMS_TO_TICKS(1000));  // 1秒待機                      // 1秒待機
            locksysDriveServo(SERVO_NEUTRAL); // サーボを基準位置に戻す
            servoStop();
        }
        else if (mode == SET_SV_LK)
        {
            locksysDriveServo(SERVO_LOCK);    // サーボをLOCK位置に動かす
            vTaskDelay(pdMS_TO_TICKS(1000));  // 1秒待機                     // 1秒待機
            locksysDriveServo(SERVO_NEUTRAL); // サーボを基準位置に戻す
            servoStop();
        }
    }
    else
    {
        cbx3_log(LOG_WAR, "LOCK Canceled(Interlock)");
    }
}

void LockSystem::KGNtral()
{
    servoStop();                      // 入れないと起動直後のサーボ動作おかしくなる。
    locksysDriveServo(SERVO_NEUTRAL); // サーボを90度位置に動かす
    vTaskDelay(pdMS_TO_TICKS(SV_CONT_TIME));
    locksysDriveServo(SERVO_NEUTRAL); // サーボを90度位置に動かす
    vTaskDelay(pdMS_TO_TICKS(SV_CONT_TIME));
    servoStop();
}

void LockSystem::servoStop()
{
    ledcWrite(pinSV, MAX_DUTY_CYCLE);
    vTaskDelay(pdMS_TO_TICKS(1000));
}

// ledcを使ったサーボモーター制御
void LockSystem::locksysDriveServo(int angle)
{
    // -90°から+90°を26から123の範囲にマッピング
    int pwmValue = map(angle, -90, 90, MIN_PULSE_PWM, MAX_PULSE_PWM);

    cbx3_log(LOG_DBG, "min=%d max=%d,angle=%d pulese=%d", MIN_PULSE_PWM, MAX_PULSE_PWM, angle, pwmValue);

    if (isInvert)
    {
        // PWM値を反転 (反転バッファ用)
        pwmValue = MAX_DUTY_CYCLE - pwmValue;
    }

    // PWM出力を設定
    ledcWrite(pinSV, pwmValue);

    // サーボをその位置に一定時間保つ
    vTaskDelay(pdMS_TO_TICKS(SV_CONT_TIME));
}
