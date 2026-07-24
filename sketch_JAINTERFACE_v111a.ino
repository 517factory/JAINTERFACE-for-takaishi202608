#include "ConfigManager.hpp"
#include "Debug.h"
#include "JAI_DataCommESP32.h"
#include "JAI_SwitchEventHandler.hpp"
#include "LEDCont.h"
#include "NVSManager.hpp"
#include "UartModemU128.hpp"
#include "driver/rtc_io.h"
#include "esp_sleep.h"
#include "header.h"
#include "timecode.hpp"
#include <HardwareSerial.h>
#include <SPIFFS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Forward Declarations
void LedController();
void SendDataLogMsg(String message);
void SendDataCommon(CommandType cmd_type);
void pollSendCallback(TimerHandle_t xTimer);
void JAONSendCallback(TimerHandle_t xTimer);
void TimeCodeMaskCallback(TimerHandle_t xTimer);
void usb_power(bool flg);
void cbx_restart();
void requestServerTime();
void ResetJAmode();
void taskCreate();
bool detectUARTdevice();
void setupUART();
void setupModem();
void requestLteTimecode();
void PowerOnModemDevice();
void controlCocoboxTask(void *pvParameters);
void sendTask(void *pvParameters);
void timecodeUpdateTask(void *pvParameters);
void JASW_Callback(SwitchEvent event, int pin);
void controlCocoboxCallback(CocoBoxControlCode command);
bool checkModemStatus();
void stop();
void resetJASwitches();
String EncodeJASwitches();
void SendDataATCom(String atcommand);
String getIDFVer();

// Priority Definitions
#define COMM_TASK_PRIORITY_RCV 10
#define COMM_TASK_PRIORITY_SND 9
#define CBX3CONTROL_PRIORITY 8
#define StartUpMain_PRIORITY 15
#define StartUpModem_PRIORITY 14
#define TC_PRIORITY 5

// Queue size
#define sendQueueSize 30
#define cbx3ControlQueueSize 10

enum TimerID {
  TIMER_ID_POLL_SEND,
  TIMER_ID_TIMECODE_MASK,
  TIMER_ID_JAON_SEND,
};

typedef struct {
  volatile bool onStart = true;
  volatile bool isModemReady = false;  // モデムの接続が確立されたらtrueになる
  volatile bool isNSIReceived = false; // NSI受信フラグ
  volatile uint32_t chipID = 0;        // ChipID格納用
  volatile NSI_Type nsi = NSI_Type::UNKNOWN; // モデムLTE動作モード
  volatile bool isTimeCodeMask = false;      // TimeCodeMaskフラグ
  volatile bool isDataTransferring = false;  // LTE通信中を示すフラグ
  volatile bool isHybernation = false;       // Hyberation実行フラグ
  volatile bool isTimeCodeRequested = false; // ServerTime要求中フラグ
  volatile bool isJAmode = false;            // JA発令中フラグ（緑LED制御用）
  String IDFVer;
} JAIStatus;
JAIStatus JAIState;

typedef struct {
  volatile bool JA01 = false;
  volatile bool JA02 = false;
  volatile bool JA03 = false;
  volatile bool JA04 = false;
  volatile bool JA05 = false;
  volatile bool JA06 = false;
  volatile bool JA07 = false;
  volatile bool JA08 = false;
  volatile bool PB01 = false;
  volatile bool PB02 = false;
} JASwitches;
JASwitches JAS; // スイッチ状態構造体

HardwareSerial ModemSerial(1); // UART1を使用
IModem *modem;

QueueHandle_t sendQueue;        // 送信用キュー
QueueHandle_t cbx3ControlQueue; // Cocobox動作制御用キュー

TimerHandle_t pollSendTimerHandle; // PollSendTimerハンドル
// TimerHandle_t doorErrorOneShotTimer = NULL;   // ドアエラー検知用タイマー
TimerHandle_t timecodeMaskTimerHandle = NULL; // タイムコードのマスク用
TimerHandle_t JAONSendTimerHandle = NULL;     // JAONの送信タイマー

// タスクのハンドル
TaskHandle_t receiveTask_hdl = NULL;
TaskHandle_t sendTask_hdl = NULL;
TaskHandle_t controlCocoboxTask_hdl = NULL;
// TaskHandle_t WiFiControlTaskHandle = NULL;
TaskHandle_t timecodeUpdateHdl = NULL;

// BatChecker *oBatChecker = nullptr;
DataCommESP32 oDataComm;

LEDCont *oLED_JA = nullptr;
LEDCont *oLED_Com = nullptr;
LEDCont *oLED_Pwr = nullptr;

SwitchEventHandler *PB01_hdl = nullptr;
SwitchEventHandler *PB02_hdl = nullptr;
SwitchEventHandler *JA01_hdl = nullptr;
SwitchEventHandler *JA02_hdl = nullptr;
SwitchEventHandler *JA03_hdl = nullptr;
SwitchEventHandler *JA04_hdl = nullptr;
SwitchEventHandler *JA05_hdl = nullptr;
SwitchEventHandler *JA06_hdl = nullptr;
SwitchEventHandler *JA07_hdl = nullptr;
SwitchEventHandler *JA08_hdl = nullptr;

TimeCode timecode;
NVSManager nvs;
ConfigManager config;

volatile bool dataInFlg = false; // 受信通知 (legacy)
CocoBoxControlCode executedCode = CocoBoxControlCode::LTE_NODATA;
char last_received_setcmd[MAX_MESSAGE_LEN] = {0};

// MODEM CALLBACKS
// ////////////////////////////////////////////////////////////////////////////
void modemLogCallback(ModemLogLevel level, const char *message) {
  switch (level) {
  case ModemLogLevel::ERR:
    cbx3_log(LOG_ERR, "MODEM: %s", message);
    break;
  case ModemLogLevel::WAR:
    cbx3_log(LOG_WAR, "MODEM: %s", message);
    break;
  case ModemLogLevel::INF:
    cbx3_log(LOG_INF, "MODEM: %s", message);
    break;
  default:
    cbx3_log(LOG_DBG, "MODEM: %s", message);
    break;
  }
}

bool modemMqttStateCallback(MqttConnectType type) {
  cbx3_log(LOG_INF, "MQTT State Changed: %d", (int)type);
  return true;
}

void modemTxLedCallback() {
  if (oLED_Com)
    oLED_Com->setMode(LEDMode::ON_Delay);
}

void modemRxLedCallback() {
  if (oLED_Com)
    oLED_Com->setMode(LEDMode::ON_Delay);
}

void modemDataReceiveCallback(modemDataPacket packet) {
  // cbx3_log(LOG_DBG, "modemDataReceiveCallback rcv:[%s] [%s]", packet.type,
  // packet.message);
  String type = String(packet.type);
  if (type == "fs") {
    std::vector<CocoBoxControlCommands> commands =
        oDataComm.ChkRcvData(packet.message);
    for (const auto &cmd : commands) {
      cbx3_log(LOG_INF, "RECEIVE COMMAND ： Code:[%d] msg:[%s] tc:[%s] ut:[%s]",
               (int)cmd.code, packet.message, packet.cclk, packet.ut);
      CocoBoxControlCode code = cmd.code;
      if (code == CocoBoxControlCode::LTE_SET) {
        strncpy(last_received_setcmd, packet.message,
                sizeof(last_received_setcmd) - 1);
      }
      xQueueSend(cbx3ControlQueue, &code, 0);
    }
  } else if (type == "timecode") {
    cbx3_log(LOG_INF, "Receive timecode: [%s] [%s]", packet.type,
             packet.message);
    timecode.setLteTimeCode(packet.message);
    if (!JAIState.onStart) {
      SendDataLogMsg("TIMECODE UPDATE : corrected " +
                     String(timecode.timeDifference) + " sec");
    }
  } else if (type == "mqttState") {
    mqttConnectHandler(packet.mqttstate);
  }
}

// MQTTの接続管理
bool mqttConnectHandler(MqttConnectType ms) {
  switch (ms) {
  case MqttConnectType::DISCONNECTED: {
    if (JAIState.isModemReady) {
      SendDataLogMsg("MQTT DISCONNECTED.");
    }
    cbx3_log(LOG_ERR, "MODEM DISCONNECTED. TRY MQTT CONNECTION.");
    JAIState.isModemReady = false;
    // Note: Reconnection is usually handled by the monitor task or next
    // periodic check
    return false;
  }
  case MqttConnectType::CONNECTED:
  case MqttConnectType::CONNECTED_SP: {
    if (!JAIState.isModemReady) {
      cbx3_log(LOG_INF, "MODEM CONNECTED.");
      SendDataLogMsg("MQTT CONNECTED.");
      modem->resisterMqttSub();
    }
    JAIState.isModemReady = true;
    return true;
  }
  default:
    return false;
  }
}

// TASK////////////////////////////////////////////////////////////////////////////////////////

//  TimeCodeUpdateタスク
void timecodeUpdateTask(void *pvParameters) {
  while (true) {
    uint32_t waitTimeMs = config.getValue("tcupdate") * 24 * 60 * 60 * 1000;
    if (waitTimeMs <= 0)
      waitTimeMs = 24 * 60 * 60 * 1000; // Default 1 day

    cbx3_log(LOG_INF, "TimeCodeUpdateDays: %d, Ticks: %d",
             config.getValue("tcupdate"), waitTimeMs);
    vTaskDelay(waitTimeMs);

    cbx3_log(LOG_INF, "REQUEST TIME CODE.(timecodeUpdateTask)");
    requestLteTimecode();
  }
}

//  コマンド制御タスク
void controlCocoboxTask(void *pvParameters) {
  CocoBoxControlCode command;

  while (true) {
    // キューからデータを受け取る
    if (xQueueReceive(cbx3ControlQueue, &command, portMAX_DELAY) == pdPASS) {
      controlCocoboxCallback(command); // コールバックへ送る
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// receiveTask removed (replaced by modem data callback)

// 送信タスク
void sendTask(void *pvParameters) {
  char *data; // 送信データ用のポインタ

  while (true) {
    // 送信キューからデータを受け取る
    if (xQueueReceive(sendQueue, &data, portMAX_DELAY)) {
      cbx3_log(LOG_INF, "SND QUEUE Received. (Queue available: %2u/%2u)",
               uxQueueSpacesAvailable(sendQueue), sendQueueSize);
      cbx3_log(LOG_INF, "SND DATA : %s", data);

      JAIState.isDataTransferring = true;
      LedController();

      // データの送信 (U128 MQTT経由)
      modem->enqueueSendMessage(data);

      cbx_wait(300);
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// TASKここまで////////////////////////////////////////////////////////////////////////////////////////
// CallBack////////////////////////////////////////////////////////////////////////////////////////
// ここボックスコマンド実行コールバック
void controlCocoboxCallback(CocoBoxControlCode command) {
  configSetting configSet;

  switch (command) {
  case CocoBoxControlCode::LTE_JRST: // JAスイッチリセット
    cbx3_log(LOG_INF, "Received command: JRST");
    ResetJAmode();
    break;

  case CocoBoxControlCode::LTE_CHECK: // CHECKコマンド受信
    cbx3_log(LOG_INF, "Received command: COM_CHECK");
    SendDataCommon(CommandType::CHK); // データをエンコードして送信
    break;

  case CocoBoxControlCode::LTE_MODEMSTATE: // MODEM状態の確認
    cbx3_log(LOG_INF, "Received command: LTE_MODEMSTATE");
    checkModemStatus();
    break;

  case CocoBoxControlCode::LTE_RESET:
    cbx3_log(LOG_INF, "Received command: COM_RESET");
    cbx3_log(LOG_WAR, "SYSTEM RESET");
    cbx_restart(); // ESP32の再起動
    break;

  case CocoBoxControlCode::LTE_TIMECODE:
  case CocoBoxControlCode::LTE_GPSTIME:
  case CocoBoxControlCode::LTE_SERVERTIME:
    cbx3_log(LOG_INF, "Received response: TIME UPDATE (Code: %d)",
             (int)command);
    // Logic moved to background dispatcher (modemDataReceiveCallback) for
    // better accuracy.
    break;

  case CocoBoxControlCode::LTE_SET: // SETコマンド受信
    cbx3_log(LOG_INF, "Received command: COM_SET");
    configSet = oDataComm.decodeConfigSetting(last_received_setcmd);
    cbx3_log(LOG_INF, "RECEIVE CONFIG SETTING [%s][%d]",
             configSet.command.c_str(), configSet.value);

    if (config.setValueByAccessKey(std::string(configSet.command.c_str()),
                                   configSet.value) ==
        ConfigManager::CONFIG_OK) {
      cbx3_log(LOG_INF, "Config Change Success : %s",
               configSet.command.c_str());
      if (configSet.command == "POLL") {
        if (config.getValue("polltimer") > 0) {
          if (pollSendTimerHandle == NULL) {
            pollSendTimerHandle = xTimerCreate(
                "PollSendTimer", config.getValue("polltimer") * 60 * 1000,
                pdTRUE, (void *)TIMER_ID_POLL_SEND, pollSendCallback);
          } else {
            xTimerChangePeriod(pollSendTimerHandle,
                               config.getValue("polltimer") * 60 * 1000, 0);
          }
          xTimerStart(pollSendTimerHandle, 0);
          cbx3_log(LOG_INF, "POL INTERVAL SET TO %d[min]",
                   config.getValue("polltimer"));
          SendDataLogMsg("SET POL INTERVAL :" +
                         String(config.getValue("polltimer")) + "[min]");
        } else {
          if (pollSendTimerHandle != NULL)
            xTimerStop(pollSendTimerHandle, 0);
          cbx3_log(LOG_INF, "POL DISABLED (timer value set to ZERO)");
          SendDataLogMsg("POL DISABLED. (timer set to ZERO)");
        }
      } else if (configSet.command == "TCUPDATE") {
        SendDataLogMsg("SET TimeCodeUpdate to " +
                       String(config.getValue("tcupdate")) + "[day(s)]");
      }
    } else {
      cbx3_log(LOG_ERR,
               "Config Change Fail (Value out of range or unknown command): "
               "%s = %d",
               configSet.command.c_str(), configSet.value);
      SendDataLogMsg("SET ERROR : " + configSet.command + " = " +
                     String(configSet.value));
    }
    break;

  case CocoBoxControlCode::LTE_ANSWERBACK: // ANSWERBACKの受信
    cbx3_log(LOG_INF, "Received response: ANSWERBACK");
    break;

  case CocoBoxControlCode::LTE_OK: // OKコマンドの受信
    cbx3_log(LOG_INF, "Received response: COM_OK");
    break;

  case CocoBoxControlCode::LTE_ERROR: // ERRORコマンドの受信
    cbx3_log(LOG_INF, "Received response: COM_ERROR");
    break;

  case CocoBoxControlCode::LTE_NODATA: // データを受信していないとき
    cbx3_log(LOG_INF, "Received response: COM_NODATA");
    break;

  default: // 不明なコマンドの受信
    cbx3_log(LOG_WAR, "Unknown command received : code=%d", (int)command);
    break;
  }
  executedCode = command;
  cbx_wait(100); // 複合でコマンドが来た時に連続動作を避ける
}

// タイマーコールバック関数
void JAONSendCallback(TimerHandle_t xTimer) {
  // サーバーにデータ送信
  SendDataCommon(JAON);
}

// JAmodeリセット
void ResetJAmode() {
  JAIState.isJAmode = false;
  resetJASwitches();
  LedController();
  SendDataCommon(CommandType::JRST);
}

// JAスイッチコールバック関数
void JASW_Callback(SwitchEvent event, int pin) {
  if (JAIState.onStart) // 起動が完了するまでは入力の受付をキャンセルする
  {
    return;
  }

  if (event == SwitchEvent::SWITCH_HIGH) {
    // ボタンを離したときのイベント
    // Nothing
    return;
  }

  if (JAIState.isJAmode) {
    if (event == SwitchEvent::LONG_PUSH && pin == P_PB01) {
      cbx3_log(LOG_INF, "PB01 LONGPUSH [%d]", pin);
      ResetJAmode();
      return;
    }
  }

  if (event == SwitchEvent::SWITCH_LOW) {
    if (pin == P_PB01 && !JAS.PB01 && !JAIState.isJAmode) {
      cbx3_log(LOG_INF, "PB01 ON [%d]", pin);
      JAS.PB01 = true;
    } else if (pin == P_PB02 && !JAS.PB02) {
      cbx3_log(LOG_INF, "PB02 ON [%d]", pin);
      JAS.PB02 = true;
    } else if (pin == P_JA01 && !JAS.JA01) {
      cbx3_log(LOG_INF, "JA01 ON [%d]", pin);
      JAS.JA01 = true;
    } else if (pin == P_JA02 && !JAS.JA02) {
      cbx3_log(LOG_INF, "JA02 ON [%d]", pin);
      JAS.JA02 = true;
    } else if (pin == P_JA03 && !JAS.JA03) {
      cbx3_log(LOG_INF, "JA03 ON [%d]", pin);
      JAS.JA03 = true;
    } else if (pin == P_JA04 && !JAS.JA04) {
      cbx3_log(LOG_INF, "JA04 ON [%d]", pin);
      JAS.JA04 = true;
    } else if (pin == P_JA05 && !JAS.JA05) {
      cbx3_log(LOG_INF, "JA05 ON [%d]", pin);
      JAS.JA05 = true;
    } else if (pin == P_JA06 && !JAS.JA06) {
      cbx3_log(LOG_INF, "JA06 ON [%d]", pin);
      JAS.JA06 = true;
    } else if (pin == P_JA07 && !JAS.JA07) {
      cbx3_log(LOG_INF, "JA07 ON [%d]", pin);
      JAS.JA07 = true;
    } else if (pin == P_JA08 && !JAS.JA08) {
      cbx3_log(LOG_INF, "JA08 ON [%d]", pin);
      JAS.JA08 = true;
    } else {
      cbx3_log(LOG_INF, "STATUS NOT CHANGED PIN [%d]", pin);
      return;
    }
  } else {
    cbx3_log(LOG_ERR, "UNKNOWN SWITCH STATE");
    return;
    // Nothing
  }

  // SENDタスクが作成されていない場合のみ送信指示
  if (xTimerIsTimerActive(JAONSendTimerHandle) ==
      pdFALSE) // タイマーが開始されていないか確認
  {
    JAIState.isJAmode = true;
    LedController();
    if (xTimerStart(JAONSendTimerHandle, 0) != pdPASS) {
      // タイマー開始に失敗した場合の処理
      cbx3_log(LOG_ERR, "Failed to start/reset timer");
    } else {
      cbx3_log(LOG_INF, "JA SEND TIMER START.");
    }
  } else {
    cbx3_log(LOG_INF, "JA SEND TIMER ALREADY STARTED");
  }
}

// POWER-GOOD検知コールバック関数
// ※PWR-GOOD関連はいまのところ監視のみ。一応実装しているレベル void
// PGDCallback(SwitchEvent event)
// {
// if (event == SWITCH_ON) // POWER-GOOD-ERROR
// {
//   // oLED_Pwr->setMode(BLINK_FAST);
//   cbx3_log(LOG_ERR, "PGD-ERROR");
//   SendDataLogMsg("POWER-GOOD ERROR OCCURED");
//   // SendDataCommon(PWAC);
// }
// else if (event == SWITCH_OFF) // POWER-GOOD OK
// {
//   cbx3_log(LOG_INF, "PGD-OK");
//   SendDataLogMsg("POWER-GOOD OK");
// }
// else
//   return;
// }

// AC/BT検知コールバック関数
// void PSCallback(SwitchEvent event)
// {
//   isPowerAC(); // ステータス更新
//   LedController();
//   if (event == SWITCH_OFF) // AC
//   {
//     SendDataCommon(CommandType::PWAC);
//   }
//   else if (event == SWITCH_ON)
//   {
//     SendDataCommon(CommandType::PWBT);
//   }
//   else
//     return;
// }

// Legacy callbacks removed

// Legacy USB/AMT callbacks removed

// POLL送信タイマーコールバック
void pollSendCallback(TimerHandle_t xTimer) {
  // POLLタイマー発報時の処理
  // POLLを送る
  SendDataCommon(CommandType::POLL);
}

void TimeCodeMaskCallback(
    TimerHandle_t xTimer) // Timecode受信マスク用タイマーコールバック
{
  cbx3_log(LOG_INF, "UNMASK TIMECODE UPDATE MASK");
  JAIState.isTimeCodeMask = false;
}

// WiFi用最新のステータス更新処理コールバック
// void updateWiFiStatus()
// {
//   // センサーや他のリソースから最新データを取得してステータスを更新
//   wifi->status.FWVersion = String(FW_VER);
//   wifi->status.timecode = timecode.getTimeCode();
//   wifi->status.isKSUexist = I2C_checkDeviceExists(I2C_KEYUNIT_ADDR);
//   // wifi->status.isACPower = isPowerAC();
//   // wifi->status.BatteryVolt = (float)readBatteryMilliVolt() / 1000;
//   cbx3_log(LOG_INF, "WiFi-Status updated.");
// }

// WiFiからのLockコールバック
// void WiFiLockCB()
// {
//   cbx3_log(LOG_INF, "WiFi LOCK called");
//   std::vector<CocoBoxControlCommands> *commands = new
//   std::vector<CocoBoxControlCommands>();
//   commands->push_back(CocoBoxControlCommands::WIFI_LOCK); // COM_DRLOCK
//   を追加

//   if (xQueueSend(cbx3ControlQueue, &commands, portMAX_DELAY) != pdPASS) //
//   実行タスクにキューを渡す
//   {
//     cbx3_log(LOG_ERR, "Failed to send command to queue");
//     delete commands; // エラーハンドリングとしてメモリを解放
//   }
//   else
//   {
//     cbx3_log(LOG_INF, "WiFi LOCK executed");
//   }
// }

// WiFiからのUnLockコールバック
// void WiFiunlockCB()
// {
//   cbx3_log(LOG_INF, "WiFi UnLOCK called");
//   std::vector<CocoBoxControlCommands> *commands = new
//   std::vector<CocoBoxControlCommands>();
//   commands->push_back(CocoBoxControlCommands::WIFI_UNLOCK); // COM_DRLOCK
//   を追加

//   if (xQueueSend(cbx3ControlQueue, &commands, portMAX_DELAY) != pdPASS) //
//   実行タスクにキューを渡す
//   {
//     cbx3_log(LOG_ERR, "Failed to send command to queue");
//     delete commands; // エラーハンドリングとしてメモリを解放
//   }
//   else
//   {
//     cbx3_log(LOG_INF, "WiFi UnLOCK executed");
//   }
//   // UNLOCKに関する具体的な処理を記述
// }

/// その他の関数////////////////////////////////////////////////////////////////
// JASエンコード
String EncodeJASwitches() {
  char sws[11];
  sws[0] = JAS.PB01 ? '1' : '0';
  sws[1] = JAS.PB02 ? '1' : '0';
  sws[2] = JAS.JA01 ? '1' : '0';
  sws[3] = JAS.JA02 ? '1' : '0';
  sws[4] = JAS.JA03 ? '1' : '0';
  sws[5] = JAS.JA04 ? '1' : '0';
  sws[6] = JAS.JA05 ? '1' : '0';
  sws[7] = JAS.JA06 ? '1' : '0';
  sws[8] = JAS.JA07 ? '1' : '0';
  sws[9] = JAS.JA08 ? '1' : '0';
  sws[10] = '\0';

  return String(sws);
}

// JASフラグのリセット
void resetJASwitches() {
  JAS.PB01 = false;
  JAS.PB02 = false;
  JAS.JA01 = false;
  JAS.JA02 = false;
  JAS.JA03 = false;
  JAS.JA04 = false;
  JAS.JA05 = false;
  JAS.JA06 = false;
  JAS.JA07 = false;
  JAS.JA08 = false;
}

// checkNSI removed

// LED Controller
void LedController() {
  // オレンジ
  if (!JAIState.isModemReady) {
    oLED_Com->setMode(LEDMode::BLINK_FAST);
  } else if (JAIState.isDataTransferring) {
    oLED_Com->setMode(LEDMode::ON_Delay); // 一定時間継続点灯
    JAIState.isDataTransferring = false;
    return;
  }
  // else if (JAIState.isWiFiOn) // WiFi
  // {
  //   oLED_Com->setMode(LEDMode::BLINK_SLOW);
  // }
  else {
    oLED_Com->setMode(LEDMode::OFF); // オレンジ消灯
  }

  // 緑
  if (JAIState.onStart) {
    oLED_JA->setMode(LEDMode::BLINK_FAST);
  } else if (JAIState.isJAmode) {
    oLED_JA->setMode(LEDMode::BLINK_FAST); // 高速点滅に変更した　2025/05/09
  } else {
    oLED_JA->setMode(LEDMode::OFF);
  }

  // 赤
  if (JAIState.onStart) {
    oLED_Pwr->setMode(LEDMode::BLINK_FAST);
  }
  // else if (JAIState.isPowerAC)
  // {
  //   oLED_Pwr->setMode(LEDMode::ON);
  // }
  else {
    // oLED_Pwr->setMode(LEDMode::BLINK_SLOW);
    oLED_Pwr->setMode(LEDMode::ON);
  }
}

// MODEM電源のON/OFF
void usb_power(bool flg) { digitalWrite(MODEM_EN, flg); }

// WiFiの開始と停止処理
// void startWifi()
// {
//   if (wifi == nullptr)
//   {
//     JAIState.isWiFiOn = true;
//     LedController();
//     cbx3_log(LOG_DBG, "Creating new cbxWiFi instance");
//     wifi = new cbxWiFi(); // WiFi制御用のクラスのインスタンスを作成
//     // ステータス更新用コールバックを登録
//     wifi->setStatusUpdateCallback(updateWiFiStatus);
//     wifi->setLockCallback(WiFiLockCB);
//     wifi->setUnlockCallback(WiFiunlockCB);
//     cbx3_log(LOG_DBG, "Starting WiFi");
//     wifi->startWifi();
//     vTaskResume(WiFiControlTaskHandle);
//     cbx3_log(LOG_DBG, "WiFi started");

//     // WiFi開始のデータを送信
//     SendDataCommon(CommandType::WLON);
//   }
// }

// void stopWifi()
// {
//   if (wifi != nullptr)
//   {
//     vTaskSuspend(WiFiControlTaskHandle);
//     JAIState.isWiFiOn = false;
//     wifi->stopWifi();
//     delete wifi; // WiFiインスタンスを削除
//     wifi = nullptr;
//     LedController();

//     // WiFi停止のデータを送信
//     SendDataCommon(CommandType::WLOF);
//   }
// }

// モデムの接続待ち処理
bool isModemConnected() {
  if (modem) {
    return modem->checkNetwork();
  }
  return false;
}

// 通常データ送信用
void SendDataCommon(CommandType cmd_type) {
  if (JAIState.isModemReady) {
    // SendData用データセット作成
    SendDatas dataSet;
    dataSet.cmd_type = cmd_type;
    dataSet.JAflgs = EncodeJASwitches();
    dataSet.counter = timecode.getTimeCode();
    dataSet.deviceID = JAIState.chipID;

    oDataComm.EncodeSndData(dataSet);

    // エンコードされたデータを取得
    char *encodedData = oDataComm.DataBuff;

    // 送信キューにデータを追加
    if (xQueueSend(sendQueue, &encodedData, portMAX_DELAY) != pdPASS) {
      cbx3_log(LOG_ERR, "Failed to send to sendQueue");
    }
  }
}

// サーバー時刻リクエスト
void requestServerTime() {
  if (JAIState.isModemReady) {
    SendDatas dataSet;
    dataSet.cmd_type = WHAT_THE_TIME;

    oDataComm.EncodeSndData(dataSet);

    // エンコードされたデータを取得
    char *encodedData = oDataComm.DataBuff;

    // 送信キューにデータを追加
    if (xQueueSend(sendQueue, &encodedData, portMAX_DELAY) != pdPASS) {
      cbx3_log(LOG_ERR, "Failed to send to sendQueue");
    } else {
      cbx3_log(LOG_INF, "QUEING SERVER TIME REQUEST");
    }
  }
}

// void requestTimeCode()
// {
//   xTaskNotify(timecodeUpdateHdl, 0, eNoAction); // timecodeUpdateTaskへ通知
// }

// ATコマンド送信
void SendDataATCom(String atcommand) {
  if (JAIState.isModemReady) {
    SendDatas dataSet;
    dataSet.cmd_type = ATCOM;
    dataSet.AT_cmd_main = atcommand;
    oDataComm.EncodeSndData(dataSet);

    // エンコードされたデータを取得
    char *encodedData = oDataComm.DataBuff;

    // 送信キューにデータを追加
    if (xQueueSend(sendQueue, &encodedData, portMAX_DELAY) != pdPASS) {
      cbx3_log(LOG_ERR, "Failed to send to sendQueue");
    }
  }
}

// LogMessage送信
void SendDataLogMsg(String msg) {
  if (JAIState.isModemReady) {
    SendDatas dataSet;
    dataSet.cmd_type = LOG;
    dataSet.LogMsg = msg;
    dataSet.counter = timecode.getTimeCode();
    oDataComm.EncodeSndData(dataSet);

    // エンコードされたデータを取得
    char *encodedData = oDataComm.DataBuff;
    cbx3_log(LOG_DBG, "SEND MSG %s", oDataComm.DataBuff);

    // 送信キューにデータを追加
    if (xQueueSend(sendQueue, &encodedData, portMAX_DELAY) != pdPASS) {
      cbx3_log(LOG_ERR, "Failed to send to sendQueue");
    }
  }
}

// バッテリー電圧確認
// float readBatteryMilliVolt()
// {
//   JAIState.BatteryMilliVolt = oBatChecker->milliVoltRead();
//   cbx3_log(LOG_DBG, "READ BATTERY VOLTAGE %d[mV]",
//   JAIState.BatteryMilliVolt); return JAIState.BatteryMilliVolt;
// }

// // 電源確認
// bool isPowerAC()
// {
//   JAIState.isPowerAC = !PS_hdl->getState();
//   return JAIState.isPowerAC;
// }

// DEEPSLEEPの開始
// void startDeepsleep()
// {
//   isPowerAC();
//   if (!ENABLE_DEEPSLEEP || JAIState.isPowerAC)
//   {
//     return; //
//     DEEPSLEEPの許可が無いとき,電源がACに復帰しているときは実行しない
//   }

//   esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_AUTO);
//   usb_power(false); // AMT5102の電源OFF

//   // LEDの消灯。この方法でやらないとSLEEP中に出力維持されない
//   rtc_gpio_init(LED_JA);
//   rtc_gpio_init(LED_COM);
//   rtc_gpio_init(LED_PWR);

//   rtc_gpio_set_direction(LED_JA, RTC_GPIO_MODE_OUTPUT_ONLY);
//   rtc_gpio_set_direction(LED_COM, RTC_GPIO_MODE_OUTPUT_ONLY);
//   rtc_gpio_set_direction(LED_PWR, RTC_GPIO_MODE_OUTPUT_ONLY);

//   rtc_gpio_set_level(LED_JA, 1);  // HIGHに設定
//   rtc_gpio_set_level(LED_COM, 1); // HIGHに設定

//   rtc_gpio_set_level(LED_PWR, 1); // HIGHに設定

//   // pinMode(WAKE_UP_PIN, INPUT_PULLUP);
//   rtc_gpio_pullup_en(WAKE_UP_PIN); //
//   この方法でPIN設定しないとうまく動かないので注意（内部PULL-UPの維持）
//   rtc_gpio_pulldown_dis(WAKE_UP_PIN);
//   esp_sleep_enable_ext0_wakeup(WAKE_UP_PIN, 0); //
//   AC/BTがLOW（ACモード）でWAKEUP cbx3_log(LOG_INF, "ENTERING DEEP-SLEEP
//   MODE"); cbx_wait(100); // PINの安定待ち esp_deep_sleep_start();
// }

void cbx_restart() // リセット用
{
  cbx3_log(LOG_INF, "RESTART JAI(AFTER 30s)");
  SendDataLogMsg("RESTART JAI (AFTER 30s)");
  oLED_Pwr->setMode(LEDMode::BLINK_FAST);
  cbx_wait(5 * 1000); // 5sec
  SendDataATCom("AT@HWRESET");
  cbx_wait(25 * 1000); // 55sec
  cbx3_log(LOG_INF, "MODEM POWER OFF");
  usb_power(false);
  cbx_wait(1000);
  ESP.restart();
}

String getIDFVer() {
  String versionString = ESP.getSdkVersion();
  int vIndex = versionString.indexOf('v');
  if (vIndex != -1) {
    return versionString.substring(vIndex + 1,
                                   versionString.indexOf('-', vIndex));
  } else {
    return "Version not found";
  }
}

// New Setup helper functions matching COCOBOX_REF
void taskCreate() {
  // セマフォとキューの初期化
  cbx3_log(LOG_INF, "[ST0]-->>CREATING QUEUE AND SEMAPHORE FOR ALL TASKS.");
  sendQueue = xQueueCreate(sendQueueSize, sizeof(char *));
  cbx3ControlQueue =
      xQueueCreate(cbx3ControlQueueSize, sizeof(CocoBoxControlCode));

  if (sendQueue == NULL || cbx3ControlQueue == NULL) {
    cbx3_log(LOG_ERR, "Failed to create queue");
    stop();
  }

  // LEDオブジェクトの開始
  cbx3_log(LOG_INF, "[ST0]-->>CREATING LED TASKS.");
  oLED_JA = new LEDCont(LED_JA);
  oLED_Com = new LEDCont(LED_COM);
  oLED_Pwr = new LEDCont(LED_PWR);
  oLED_Pwr->setMode(LEDMode::ON);
  LedController();

  // スイッチ類稼働
  cbx3_log(LOG_INF, "[ST0]>>INITIALIZE INPUT SIGNAL");
  PB01_hdl = new SwitchEventHandler(P_PB01);
  PB01_hdl->setCallback(JASW_Callback);
  PB01_hdl->setLongPush(true, false);
  PB01_hdl->begin();

  PB02_hdl = new SwitchEventHandler(P_PB02);
  PB02_hdl->setCallback(JASW_Callback);
  PB02_hdl->setLongPush(true, false);
  PB02_hdl->begin();

  JA01_hdl = new SwitchEventHandler(P_JA01);
  JA01_hdl->setCallback(JASW_Callback);
  JA01_hdl->begin();

  JA02_hdl = new SwitchEventHandler(P_JA02);
  JA02_hdl->setCallback(JASW_Callback);
  JA02_hdl->begin();

  JA03_hdl = new SwitchEventHandler(P_JA03);
  JA03_hdl->setCallback(JASW_Callback);
  JA03_hdl->begin();

  JA04_hdl = new SwitchEventHandler(P_JA04);
  JA04_hdl->setCallback(JASW_Callback);
  JA04_hdl->begin();

  JA05_hdl = new SwitchEventHandler(P_JA05);
  JA05_hdl->setCallback(JASW_Callback);
  JA05_hdl->begin();

  JA06_hdl = new SwitchEventHandler(P_JA06);
  JA06_hdl->setCallback(JASW_Callback);
  JA06_hdl->begin();

  JA07_hdl = new SwitchEventHandler(P_JA07);
  JA07_hdl->setCallback(JASW_Callback);
  JA07_hdl->begin();

  JA08_hdl = new SwitchEventHandler(P_JA08);
  JA08_hdl->setCallback(JASW_Callback);
  JA08_hdl->begin();

  // 送受信タスクの開始
  cbx3_log(LOG_INF, "[ST0]->>CREATING RCV/SND TASK");
  xTaskCreateUniversal(sendTask, "sendTask", 4096, NULL, COMM_TASK_PRIORITY_SND,
                       &sendTask_hdl, APP_CPU_NUM); // 送信タスク
  xTaskCreateUniversal(controlCocoboxTask, "controlCocoboxTask", 4096, NULL,
                       CBX3CONTROL_PRIORITY, &controlCocoboxTask_hdl,
                       APP_CPU_NUM); // 受信後のコマンド処理タスク

  // POLLタイマータスクの開始
  cbx3_log(LOG_INF, "[ST0]->>STARTING POLL-TIMER TASK");
  if (config.getValue("polltimer") > 0) {
    pollSendTimerHandle =
        xTimerCreate("PollSendTimer", config.getValue("polltimer") * 60 * 1000,
                     pdTRUE, (void *)TIMER_ID_POLL_SEND, pollSendCallback);
    xTimerStart(pollSendTimerHandle, 0);
    cbx3_log(LOG_INF, "[ST0]-->>POL INTERVAL SET TO %d[min]",
             config.getValue("polltimer"));
  }

  // JAON送信タイマー
  JAONSendTimerHandle =
      xTimerCreate("JAONSendTimer", pdMS_TO_TICKS(JA_SEND_DELAY), pdFALSE,
                   (void *)TIMER_ID_JAON_SEND, JAONSendCallback);

  cbx3_log(LOG_INF, "[ST0]->>ALL TASKS CREATED.");
}

bool detectUARTdevice() {
  pinMode(RX, INPUT); // Use INPUT mode to avoid fighting the modem's driver
  int counter = 0;
  while (1) {
    bool rx_state = digitalRead(RX);
    cbx3_log(LOG_INF, ">>CHECKING UART CT：[%3d] STATE：[%d]", counter,
             rx_state);
    if (rx_state == HIGH) {
      return true;
    } else if (counter > 30) { // Timeout (increased to 30s for safety)
      return false;
    }
    cbx_wait(1000);
    counter++;
  }
}

void setupUART() {
  cbx3_log(LOG_INF, "[ST1]->>Checking UART Device Ready.");
  if (detectUARTdevice()) {
    cbx3_log(LOG_INF, "[ST1]-->>UART Device Detected. RX_PIN:%d TX_PIN:%d",
             (int)RX, (int)TX);
    ModemSerial.begin(115200, SERIAL_8N1, RX, TX);
  } else {
    cbx3_log(LOG_WAR, "[ST1]-->>Could not detected UART Device.");
    cbx_restart();
  }
}

void setupModem() {
  cbx3_log(LOG_INF, "[ST1]->>Setting U128.");
  if (modem != nullptr) {
    delete modem;
    modem = nullptr;
  }
  modem = new UartModemU128(&ModemSerial);

  modem->setDebugLogCallback(modemLogCallback);
  modem->setMqttStateCallback(modemMqttStateCallback);
  modem->setTxLedCallback(modemTxLedCallback);
  modem->setRxLedCallback(modemRxLedCallback);
  modem->setModemDataReceiveCallback(modemDataReceiveCallback);

  // START TRASPORT TASKS (Necessary for init to work as it relies on async
  // response parsing)
  cbx3_log(LOG_INF, "[ST1]->>STARTING TRANSPORT TASKS");
  modem->startReceiveTasks(COMM_TASK_PRIORITY_RCV, COMM_TASK_PRIORITY_RCV,
                           COMM_TASK_PRIORITY_RCV, APP_CPU_NUM);
  modem->startSendTask(COMM_TASK_PRIORITY_SND, APP_CPU_NUM);
  modem->startMonitorTask(CBX3CONTROL_PRIORITY, APP_CPU_NUM);

  cbx3_log(LOG_INF, "[ST1]->>Start Modem Initialization.");
  if (modem->init()) {
    cbx_wait(1000);
    cbx3_log(LOG_INF, "[ST1]-->>Modem Init SUCCESS.");
    JAIState.isModemReady = true;
  } else {
    cbx3_log(LOG_ERR, "[ST1]-->>MODEM INIT FAIL");
    cbx_restart();
  }
}

void requestLteTimecode() {
  if (JAIState.isModemReady) {
    modem->requestTimecode();
  }
}

void PowerOnModemDevice() {
  cbx3_log(LOG_INF, "[ST0]->>COM DEVICE POWER ON");
  pinMode(MODEM_EN, OUTPUT);
  usb_power(false);
  cbx_wait(1000);
  usb_power(true);
  // Removed the long blocking wait here to allow background tasks to start and
  // capture boot messages.
}

// SetUp////////////////////////////////////////////////////////////////////////////////
void setup() {
  Serial.begin(115200);
  cbx_wait(1000);
  cbx3_log(LOG_INF,
           "///////////STARTING J-ALART INTERFACE/////////////////////");
  cbx3_log(LOG_INF, "FW Ver : %s", FW_VER);
  JAIState.IDFVer = getIDFVer();
  cbx3_log(LOG_INF, "ESP-IDF Ver : %s", JAIState.IDFVer.c_str());

  // ChipIDの取得
  cbx3_log(LOG_INF, "[ST0]->>Get ESP32 ChipID");
  for (int i = 0; i < 17; i = i + 8) {
    JAIState.chipID |= ((ESP.getEfuseMac() >> (40 - i)) & 0xff) << i;
  }
  cbx3_log(LOG_INF, "[ST0]->>ChipID:%06X", JAIState.chipID);

  // Configのロード
  cbx3_log(LOG_INF, "[ST0]->>LOADING [NVS Config]");
  if (!config.begin(&nvs)) {
    cbx3_log(LOG_ERR,
             "[ST0]->>An error has occurred while initializing NVS Config");
  } else {
    cbx3_log(LOG_INF, "[ST0]->>NVS Config loaded.");
    config.printAllConfigValues();
  }

  // タスクとデバイスの初期化
  taskCreate();

  // モデム電源ON
  PowerOnModemDevice();

  // UART設定
  setupUART();

  // モデム設定
  setupModem();

  // タイムコード取得
  cbx3_log(LOG_INF, "[ST0]->>Request Timecode");
  requestLteTimecode();

  // 開始messageフェーズ
  cbx3_log(LOG_INF, "[ST0]->>SENDING START MSG");
  String start_msg = "START J-ALART INTERFACE / FW=";
  start_msg += String(FW_VER);
  start_msg += ",IDF=";
  start_msg += JAIState.IDFVer;
  start_msg += ",POLL=";
  start_msg += String(config.getValue("polltimer"));
  start_msg += ",TCUD=";
  start_msg += String(config.getValue("tcupdate"));
  SendDataLogMsg(start_msg);

  // タイムコード更新タスク開始
  cbx3_log(LOG_INF, "[ST0]->>STARTING TIMECODE-UPDATE TASK");
  xTaskCreateUniversal(timecodeUpdateTask, "timecodeUpdateTask", 4096, NULL,
                       TC_PRIORITY, &timecodeUpdateHdl, tskNO_AFFINITY);

  // BGN送信フェーズ
  cbx3_log(LOG_INF, "[ST0]->>SENDING BGN MSG");
  SendDataCommon(CommandType::BGN);

  cbx_wait(3000);

  // LED状態の初期化
  JAIState.onStart = false;
  cbx3_log(LOG_INF, "[ST0]->>INITIALIZE STATUS LEDs");
  LedController();

  cbx3_log(LOG_INF, "[ST0]->>ALL SETUP FINISHED. J-ALART INTERFACE RUNNING.");
}

// loop////////////////////////////////////////////////////////////////////////////////
void loop() {
  // nothing
  cbx_wait(10);
}

// テスト用
void stop() {
  cbx3_log(LOG_WAR, "PROGRAM STOP");
  int counter = 0;
  while (1) {
    if (counter < 10) {
      cbx3_log(LOG_WAR, "PROGRAM STOP : %d", counter);
      cbx_wait(1000);
      counter++;
    }
  }
}

// モデムのステータスチェックとログ送信
bool checkModemStatus() {
  int counter = 0;
  while (true) {
    if (modem) {
      char *info = modem->chkSystemInformation();
      if (info[0] != '\0') {
        cbx3_log(LOG_INF, "MODEM STATE : %s", info);
        SendDataLogMsg(info);
        return true;
      } else {
        cbx3_log(LOG_WAR, "CHECK MODEM STATUS : Data is empty.");
        SendDataLogMsg("ERROR : COULD NOT GET MODEM STATUS.");
        return false;
      }
    } else if (counter > 20) {
      cbx3_log(LOG_ERR, "checkModemStatus : TIMEOUT");
      SendDataLogMsg("ERROR : COULD NOT GET MODEM STATUS.");
      return false;
    } else {
      cbx3_log(LOG_WAR,
               "COULD NOT TAKE MODEM SEMAPHORE(checkModemStatus) RETRY...");
      counter++;
      cbx_wait(1000);
    }
  }
}
