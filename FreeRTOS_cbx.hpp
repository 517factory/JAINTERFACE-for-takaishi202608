#pragma once

typedef enum // タスク優先度の列挙
{
    ASYNC_TASK_PRIORITY = 18,
    COMM_TASK_PRIORITY_RCV = 15, // 受信タスク
    COMM_TASK_PRIORITY_SND = 14, // 送信タスク
    CBX3CONTROL_PRIORITY = 13,   // コントロールタスク
    JARECEIVE_PRIORITY = 12,     // JA受信キュー用
    I2C_PRIORITY = 10,           // I2Cタスク
    WIFI_PRIORITY = 9,           // WIFIメッセージ受信タスク
    EQ_PRIORITY = 8,             // EQ検知タスク
    TC_PRIORITY = 7,             // Timdecode受信タスク
    SWITCH_PRIORITY = 5,         // SWITCH監視
    LED_PRIORITY = 4,            // LED制御
    StartUpAMT5102_PRIORITY = 3, // StartUpAMT5102タスク
    StartUpMain_PRIORITY = 2     // StartUpMainタスク
} task_priority_t;

/*コア選択
ESP32での優先度
-PRO_CPU_NUM
    19-23   無線系で使用されている
-APP_CPU_NUM
    1       loop()

Core 0	PRO_CPU	WDT有効
Core 1	APP_CPU	WDT無効

参考）
loop()関数の中身にもdelay(1)を本来は入れたほうがいい。入れるのと入れないのでは消費電力の違いがでる。
loop()関数のタスク優先度は1が設定されているので注意
APP_CPUなのでWDTは無効だが、delay(1)がないとWDTが発動する条件になる。
※WDT（ウォッチドッグタイマー）
　報告が３秒以上ないと再起動する（ハングアップ検知）
　delay関数を１以上で呼び出すとWDTはリセットされる
*/
