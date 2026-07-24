/*****
GPIOピンアサイン

for COCOBOX3
2024.05.08 ESP32対応版　ESP32-S3-DevKitC-1
******/

#pragma once

// Pin Assignment//////////////////////////////////////////////

// #define STRAP0　0    //Don't Use GPIO0 (STRAP PIN)

// I2C  RFIDからの信号受信用。UARTが使えれば不要となる
#define I2C_SDA 1
#define I2C_SCL 2

// #define STRAP3 3  //Don't Use GPIO3 (STRAP PIN)

// UART　PIN
#define TX 4 // UART TX
#define RX 5 // UART RX

// DOOR SW
#define DOOR_SW 6    // ドアスイッチ、（DI）
#define AMT5102_EN 7 // AMT5102電源制御フォトリレー動作用出力　（DO）

// SPare
#define SPARE_IO 9 // スペア入出力用。WiFiスイッチに接続
// #define SPARE10 10    // Spare

// LED - 全てのLEDをGPIO経由で出すように変更
#define LED_DS GPIO_NUM_11  // LED(WHITE)   （DO）ドアスイッチ連動（庫内照明）
#define LED_KS GPIO_NUM_12  // LED(GREEN)   （DO）鍵センサー連動
#define LED_COM GPIO_NUM_13 // LED(ORANGE)  （DO）通信状態表示
#define LED_PWR GPIO_NUM_14 // LED(RED)     （DO）電源状態表示

// Power Control
#define VBT ADC1_CHANNEL_7 // バッテリー電圧測定ADC_CHANNEL　（GPIO8）
#define POWERGOOD 15       // POWERGOOD端子
#define VBT_EN 16          // バッテリー電圧監視Enable（DO）
#define ACBT GPIO_NUM_17   // AC/BT切り替え検知（DI）

// DHT22
#define DHTPIN 18 // DHT22データ端子（AI）

#define USB_DM 19 // AMT5102 USB-CDCMA(D-) !!DONT CHANGE!!
#define USB_DP 20 // AMT5102 USB-CDCMA(D+) !!DONT CHANGE!!

// Servo
#define SV_DATA 21 // サーボデータ端子（DO）

// Don't Use GPIO22 - GPIO34
// Don't Use GPIO35 - GPIO37 SPIフラッシュの使用状況によっては使えるかもしれない

// 非推奨IO
//  GPIO38 ESP32仕様変更でRGB-LEDとして使用されると困るので非推奨
//  #define RESERVED 39  // INPUT ONLYなので非推奨

// Devise Switch   WIFIのON/OFFを物理スイッチで行う
#define WIFI_SW 40 // WIFI-ON SWITCH        （DI）

// EarthQuake
#define EQ_RST 41 // WIFI-ON SWITCH        （DI）
#define EQ 42     // 感震リレーからの入力。（DI）

// Don't Use GPIO43 (TX0)
// Don't Use GPIO44 (RX0)
// Don't Use GPIO45 (UART1-TX) STRAP PIN
// Don't Use GPIO46 (UART1-RX) STRAP PIN

// Key SW
#define KEY_SW 47 // キースイッチ、（DI）（フォトセンサーからの入力）

// RGB-LED   48  //ONBOARD RGB-LED !!DONT CHANGE!!  // マニュアルでは38がRGB-LEDだが実際は48だった
///////////////////////////////////////////////////////////////