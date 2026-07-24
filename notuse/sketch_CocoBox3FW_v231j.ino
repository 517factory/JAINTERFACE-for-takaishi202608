#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <SPIFFS.h>
#include "JAI_header.h"
#include "DHT22Cont.h"
#include "DataCommESP32.h"
#include "Debug.h"
#include "LEDCont.h"
#include "LockSystemLEDC.h"
#include "cbx3_wifi.h"
#include "usb/usb_host.h"
#include "usb_amt5102.hpp"
#include "SwitchEventHandler.hpp"
#include "BatteryChecker.h"
#include "cbx_I2C.hpp"
#include "KeyUnitCont.hpp"
#include "timecode.hpp"
#include "AccessSPIFS.hpp"
#include "esp_sleep.h"
#include "driver/rtc_io.h"

enum TimerID
{
  TIMER_ID_DOOR_ERROR,
  TIMER_ID_AUTO_LOCK,
  TIMER_ID_LOCK_TIMEOUT,
  TIMER_ID_POLL_SEND,
  TIMER_ID_TIMECODE_MASK,
};

typedef struct
{
  volatile bool onStart = true;
  volatile bool isAMT5102Ready = false; // ACM5102のLTE接続が確率されたらtrueになる
  volatile bool isWiFiOn = false;
  volatile bool isDoorClosed;                                              // ドア開閉状態
  volatile bool isKeyLocked;                                               // 鍵施錠状態
  volatile bool isPowerAC;                                                 // AC/BT状態
  volatile bool isEqOn = false;                                            // EQ状態
  volatile bool isNSIReceived = false;                                     // NSI受信フラグ
  volatile uint32_t chipID = 0;                                            // ChipID格納用
  volatile uint8_t endurance_flg = 0;                                      // 耐久モード実行用フラグ
  volatile NSI_Type nsi = NSI_Type::UNKNOWN;                               // AMT5102LTE動作モード
  volatile bool isAutoLockEnable = true;                                   // AUTOLOCKの有効・無効
  volatile bool isAutoLocking = false;                                     // AUTOLOCK動作中フラグ
  volatile bool isTimeCodeMask = false;                                    // TimeCodeMaskフラグ
  volatile LockReason lockReason = LockReason::MANUAL;                     // Lock/Unlockの動作Reason
  volatile bool lockTarget;                                                // Lock/Unlock動作時にこれからどちらの動作をするか記憶して、結果と比較する（ULER用）
  volatile bool isDataTransferring = false;                                // LTE通信中を示すフラグ
  volatile bool isHybernation = false;                                     // Hyberation実行フラグ
  volatile bool isTimeCodeRequested = false;                               // ServerTime要求中フラグ
  volatile CommandType lastSendDoorStatus = static_cast<CommandType>(LOG); // ここには最後に送ったドアステータスを格納。ドアステータスに変化がなければ送信をキャンセルする。初期値はダミー。
  volatile uint32_t BatteryMilliVolt = 0;                                  // バッテリー電圧[mV]
  String IDFVer;
} CocoBoxStatus;
CocoBoxStatus cbxState;

USBhost *host;
AMT5102 *amt5102;
cbxWiFi *wifi = nullptr;
SemaphoreHandle_t wifiMutex;    // Wifi用
SemaphoreHandle_t ACMSemaphore; // USB送受信用
QueueHandle_t sendQueue;        // 送信用キュー
QueueHandle_t cbx3ControlQueue; // Cocobox動作制御用キュー

TimerHandle_t pollSendTimerHandle;            // PollSendTimerハンドル
TimerHandle_t doorErrorOneShotTimer = NULL;   // ドアエラー検知用タイマー
TimerHandle_t lockTimeOutTimerHandle = NULL;  // Lock動作が規定時間以内に完了するかのタイマーハンドル
TimerHandle_t autoLockTimerHandle = NULL;     // 自動ロック用タイマーハンドル
TimerHandle_t timecodeMaskTimerHandle = NULL; // タイムコードのマスク用

// タスクのハンドル
TaskHandle_t receiveTask_hdl = NULL;
TaskHandle_t sendTask_hdl = NULL;
TaskHandle_t controlCocoboxTask_hdl = NULL;
TaskHandle_t WiFiControlTaskHandle = NULL;
TaskHandle_t StartUpMainHdl = NULL;
TaskHandle_t StartUpAAMT5102Hdl = NULL;
TaskHandle_t timecodeUpdateHdl = NULL;

LockSystem *oLockSystem = nullptr;
DHT22Cont *oDHT22 = nullptr;
BatChecker *oBatChecker = nullptr;
DataCommESP32 oDataComm;

LEDCont *oLED_DS = nullptr;
LEDCont *oLED_KS = nullptr;
LEDCont *oLED_Com = nullptr;
LEDCont *oLED_Pwr = nullptr;

SwitchEventHandler *DS_hdl = nullptr;
SwitchEventHandler *KS_hdl = nullptr;
SwitchEventHandler *WS_hdl = nullptr;
SwitchEventHandler *PS_hdl = nullptr;

SwitchEventHandler *EQ_hdl = nullptr; // EQの検知

KeyUnitCont keyUnit(I2C_KEYUNIT_ADDR);
TimeCode timecode;
AccessSPIFS spifs;

CocoBoxControlCommands executedCode = CocoBoxControlCommands::LTE_NODATA;

volatile bool usb_dev_flg = false;
volatile bool dataInFlg = false; // 受信通知
volatile LockCommandStatus lcstate = LockCommandStatus::lockCommand_MANUAL;

// TASK////////////////////////////////////////////////////////////////////////////////////////
// 起動シーケンス用OneShotTask（Main）
void StartUpMainTask(void *pvParameters)
{
  LedController(); // 初期点灯
  // タスクの処理
  cbx3_log(LOG_INF, "[ST1]>>StartUpMainTask Started.");

  // ChipIDの取得
  cbx3_log(LOG_INF, "[ST1]->>Get ESP32 ChipID");
  for (int i = 0; i < 17; i = i + 8)
  {
    cbxState.chipID |= ((ESP.getEfuseMac() >> (40 - i)) & 0xff) << i;
  }
  cbx3_log(LOG_INF, "[ST1]->>ChipID:%06X", cbxState.chipID);

  // 感震センサーの設定
  cbx3_log(LOG_INF, "[ST1]->>INITIALIZE EQ-UNIT Controller I/O");
  pinMode(EQ_RST, OUTPUT);
  EQ_reset();

  // DHT22初期化
  cbx3_log(LOG_INF, "[ST1]->>INITIALIZE DHT22 TEMP-SENSOR");
  if (DHT22_ENABLE)
  {
    oDHT22 = new DHT22Cont(DHTPIN);
    oDHT22->DHT22begin();
    String DHT22msg = oDHT22->getStrMessage();
    cbx3_log(LOG_INF, "[ST1]-->>READ DATA : [%s]", DHT22msg.c_str());
  }

  // ドアエラー検知用タイマータスク作成
  doorErrorOneShotTimer = xTimerCreate("doorErrorOneShotTimer", pdMS_TO_TICKS(DERRTIMER), pdFALSE, (void *)TIMER_ID_DOOR_ERROR, onDoorErrorCB);
  // AutoLockタイマータスク作成
  if (spifs.autolockDelay != 0)
  {
    autoLockTimerHandle = xTimerCreate("AutoLockTimer", pdMS_TO_TICKS(spifs.autolockDelay * 1000), pdFALSE, (void *)TIMER_ID_AUTO_LOCK, AutoLockCallback);
  }
  else // Timer値がゼロでタスクを作るとエラーになってしまうため仮値でタイマーを作る。（ゼロのときはAUTOLOCKはDisableになるのでこのタイマーは使われない）
  {
    autoLockTimerHandle = xTimerCreate("AutoLockTimer", pdMS_TO_TICKS(AUTOLOCK_DELAY_DEFAULT * 1000), pdFALSE, (void *)TIMER_ID_AUTO_LOCK, AutoLockCallback);
  }
  // LockTimeOutタイマータスク作成
  lockTimeOutTimerHandle = xTimerCreate("LockTimeOutTimer", pdMS_TO_TICKS(LOCK_TO_TIMER), pdFALSE, (void *)TIMER_ID_LOCK_TIMEOUT, LockTimeoutCallback);

  // AMT5102の接続を待機
  cbx3_log(LOG_INF, "[ST1]-->>WAITING AMT5102 CONNECTION.");
  xTaskNotifyWait(0x00, 0x00, NULL, portMAX_DELAY); // 接続待ち
  cbx3_log(LOG_INF, "[ST1]-->>AMT5102 CONNECTED.");
  cbxState.isAMT5102Ready = true;
  LedController();
  cbx_wait(8000); // ここを長くとらないと最初の電文が飛ばない（おそらくAMT5102の安定待ちが必要と思われる）

  // 開始messageフェーズ
  cbx3_log(LOG_INF, "[ST1]->>SENDING START MSG");
  String start_msg = "START COCOBOX / FW=";
  start_msg += String(FW_VER);
  start_msg += ",IDF=";
  start_msg += cbxState.IDFVer;
  start_msg += ",KSU=";
  start_msg += I2C_checkDeviceExists(I2C_KEYUNIT_ADDR) ? "T" : "F";
  start_msg += ",POLL=";
  start_msg += String(spifs.pollTimerValue);
  start_msg += ",TCUD=";
  start_msg += String(spifs.tcUpdateDays);
  start_msg += ",AL=";
  start_msg += spifs.autolockDelay;
  start_msg += ",VTH=";
  start_msg += String(spifs.vth);
  start_msg += ",VCAL=";
  start_msg += String(spifs.vcal);
  SendDataLogMsg(start_msg);

  cbx_wait(2000); // ここを長くとらないとWHATS_THE_TIMEが飛ばないことがある
  // サーバー時刻要求フェーズ
  cbx3_log(LOG_INF, "[ST1]->>REQUEST SERVER TIME");
  // TimeCode用タスクの開始(タスク開始と同時に1回リクエストが出る)
  cbx3_log(LOG_INF, "[ST1]->>STARTING TIMECODE-UPDATE TASK");
  cbxState.isTimeCodeRequested = false;
  cbx3_log(LOG_INF, "[ST1]-->>Setting TimeCode Update : every %d[day(s)]", spifs.tcUpdateDays);
  if (timecodeUpdateHdl != NULL)
  {
    // タスクは作成済み
    cbx3_log(LOG_INF, "[ST1]-->>timecodeUpdateTask Already Running");
  }
  else
  {
    // タスクは作成されていない
    cbx3_log(LOG_INF, "[ST1]-->>CREATE TIMECODE-UPDATE TASK");
    xTaskCreateUniversal(timecodeUpdateTask, "timecodeUpdateTask", 4096, NULL, TC_PRIORITY, &timecodeUpdateHdl, tskNO_AFFINITY); // タスク開始
  }

  // サーバー時刻受信フェーズ
  uint8_t counter = 0;
  cbx3_log(LOG_INF, "[ST1]->>RECEIVE SERVER TIME");
  while (timecode.timeMode != SERVER_TIME)
  {
    cbx3_log(LOG_INF, "[ST1]-->>WAITING SERVER RESPONSE [%d]", counter);
    counter++;
    cbx_wait(1000); // 1秒周期で待つ
    if (counter > TC_SERVER_TIMEOUT_VALUE)
    {
      cbx3_log(LOG_INF, "[ST1]-->>RCV SERVER TIME TIMEOUT.");
      counter = 0;
      break;
    }
  }
  if (timecode.timeMode == SERVER_TIME)
  {
    cbx3_log(LOG_INF, "[ST1]-->>SERVER TIME RECEIVED.");
    counter = 0;
  }

  if (timecode.timeMode != SERVER_TIME)
  {
    // GPS時刻要求フェーズ
    cbx3_log(LOG_INF, "[ST1]->>REQUEST GPS TIME");
    SendDataATCom("AT+CCLK?");
    cbx3_log(LOG_INF, "[ST1]-->>REQUEST GPS TIME OK");
    // GPS時刻受信フェーズ
    cbx3_log(LOG_INF, "[ST1]->>RECEIVE GPS TIME");
    while (timecode.timeMode != GPS_TIME)
    {
      cbx3_log(LOG_INF, "[ST1]-->>WAITING GPS TIME RESPONSE [%d]", counter);
      counter++;
      cbx_wait(1000); // 1秒周期で待つ
      if (counter > TC_GPS_TIMEOUT_VALUE)
      {
        break;
      }
    }
    if (timecode.timeMode == GPS_TIME)
    {
      cbx3_log(LOG_INF, "[ST1]-->>RCV GPS TIME OK");
    }
    else
    {
      cbx3_log(LOG_INF, "[ST1]-->>RCV GPS TIME ERROR");
    }
  }

  // BGN送信フェーズ
  cbx3_log(LOG_INF, "[ST1]->>SENDING BGN MSG");
  SendDataCommon(CommandType::BGN); // 開始時用初回データの送信（BGN）
  cbx_wait(3000);

  // POLLタイマータスクの開始
  cbx3_log(LOG_INF, "[ST1]->>STARTING POLL-TIMER TASK");

  if (spifs.pollTimerValue > 0)
  {
    cbx3_log(LOG_INF, "spifs.pollTimerValue=%d: ", spifs.pollTimerValue);
    pollSendTimerHandle = xTimerCreate("PollSendTimer", spifs.pollTimerValue * 60 * 1000, pdTRUE, (void *)TIMER_ID_POLL_SEND, pollSendCallback);
    xTimerStart(pollSendTimerHandle, 0);
    cbx3_log(LOG_INF, "[ST1]-->>POL INTERVAL SET TO %d[min]", spifs.pollTimerValue);
  }
  else
  {
    cbx3_log(LOG_INF, "[ST1]-->>POL DISABLED. (timer value set to %d)", spifs.pollTimerValue);
  }

  // 鍵ユニット動作Task開始
  cbx3_log(LOG_INF, "[ST1]->>STARTING KEY-STRAGE-UNIT TASK");
  if (I2C_checkDeviceExists(I2C_KEYUNIT_ADDR))
  {
    if (keyUnit.begin())
    {
      keyUnit.setTagChangeCallback(tagChangeCallback);
      keyUnit.startTask();
      cbx3_log(LOG_INF, "[ST1]-->>KEY UNIT TASK STARTED.");
    }
  }
  else
  {
    cbx3_log(LOG_WAR, "[ST1]-->>KEY UNIT NOT EXIST");
  }

  // WiFiタスク動作開始
  if (WIFI_ENABLE)
  {
    cbx3_log(LOG_INF, "[ST1]->>STARTING WiFi Control TASK");
    WS_hdl = new SwitchEventHandler(WIFI_SW);
    WS_hdl->setCallback(WSCallback);
    WS_hdl->setLongPush(true, false);
    WS_hdl->begin();

    // WiFiミューテックスの初期化
    wifiMutex = xSemaphoreCreateMutex();

    if (wifiMutex == NULL)
    {
      cbx3_log(LOG_ERR, "Failed to create WiFi mutex");
    }
    else
    {
      cbx3_log(LOG_INF, "[ST1]-->>WiFi mutex created successfully");
      xTaskCreateUniversal(WiFiControlTask, "WiFiControlTask", 4096, NULL, WIFI_PRIORITY, &WiFiControlTaskHandle, APP_CPU_NUM); // タスク開始
      cbx3_log(LOG_INF, "[ST1]-->>WiFi Control Task created");
      if (WIFI_ENABLE_ONSTART)
      {
        startWifi();
      }
      else
      {
        vTaskSuspend(WiFiControlTaskHandle); // 使用しないときは停止しておく
      }
    }
    // cbx_wait(1000);
  }

  cbx_wait(3000); // ここまで完了した後も通信が少し残っているのでランプを消すのを少し待ってやる

  // LED状態の初期化
  cbxState.onStart = false;
  cbx3_log(LOG_INF, "[ST1]->>INITIALIZE STATUS LEDs");
  LedController();

  cbx3_log(LOG_INF, "[ST1]->>ALL SETUP FINISHED. COCOBOX RUNNING.");
  // タスク完了後にタスクを削除
  vTaskDelete(NULL); // このタスクを削除
}

// 起動シーケンス用OneShotTask（AMT5102）
void StartUpAMT5102Task(void *pvParameters)
{
  uint8_t counter = 0;
  // タスクの処理
  cbx3_log(LOG_INF, "[ST2]>>StartUpAMT5102Task Started.");

  // USB HOST開始
  cbx3_log(LOG_INF, "[ST2]->>STARTING USB HOST");
  cbx3_log(LOG_INF, "[ST2]-->>INIT USB HOST");
  host = new USBhost();
  initUSBDevice();

  // AMT5102電源ON
  cbx3_log(LOG_INF, "[ST2]->>AMT5102 POWER ON");
  pinMode(AMT5102_EN, OUTPUT); // AMT5102電源制御PINの有効化
  usb_power(false);
  cbx_wait(1000); // コンデンサー放電待ちがあるので念の為入れておく。
  usb_power(true);

  cbx3_log(LOG_INF, "[ST2]->>AMT5102 POWER ON WAIT %d[sec]", AMT5102_POWER_ON_WAIT);
  cbx_wait(AMT5102_POWER_ON_WAIT * 1000); // AMT5102起動待ちWAIT。起動タイミングによりUSB受信ができなくなる問題の対応

  // USB接続フェーズ
  cbx3_log(LOG_INF, "[ST2]->>CONNECTING USB DEVICE");
  while (!usb_dev_flg) // USBデバイス検出待ち
  {
    cbx3_log(LOG_INF, "[ST2]-->>WAITING USB DEVICE CONNECTION[%d]]", counter);
    cbx_wait(1000); // 1秒間隔でUSB接続を待つ
    counter++;
  }
  cbx3_log(LOG_INF, "[ST2]-->>USB CONNECTED.");
  amt5102->inDataFlg = READY; // 最初のゴミの処理用
  counter = 0;

  // AMT5102接続フェーズ
  cbx3_log(LOG_INF, "[ST2]->>CONNECTING AMT5102");
  if (!isAMT5102Connected()) // AMT5102接続待ち
  {
    cbx3_log(LOG_INF, "[ST2]-->>WAITING USB DEVICE CONNECTION[%d]]", counter);
    counter++;
    cbx_wait(2000); // 問い合わせ間隔が短いとハングアップする
  }
  cbx3_log(LOG_INF, "[ST2]-->>AMT5102_CONNECTED");

  // セマフォとキューの初期化
  sendQueue = xQueueCreate(sendQueueSize, sizeof(char *));
  cbx3ControlQueue = xQueueCreate(cbx3ControlQueueSize, sizeof(CocoBoxControlCommands));
  ACMSemaphore = xSemaphoreCreateMutex();

  if (sendQueue == NULL || cbx3ControlQueue == NULL || ACMSemaphore == NULL)
  {
    cbx3_log(LOG_ERR, "[ST2]-->>Failed to create queue or semaphore");
    stop(); // 成功しないと開始できないようになっているので注意。
  }
  else // PWM出力を設定
  {
    cbx3_log(LOG_INF, "[ST2]-->>QUEUE AND SEMAPHORE FOR SND/RCV CREATED.");

    // 送受信タスクの開始
    cbx3_log(LOG_INF, "[ST2]->>CREATING RCV/SND TASK");
    xTaskCreateUniversal(receiveTask, "receiveTask", 4096, NULL, COMM_TASK_PRIORITY_RCV, &receiveTask_hdl, APP_CPU_NUM);                    // 受信タスク
    xTaskCreateUniversal(sendTask, "sendTask", 4096, NULL, COMM_TASK_PRIORITY_SND, &sendTask_hdl, APP_CPU_NUM);                             // 送信タスク
    xTaskCreateUniversal(controlCocoboxTask, "controlCocoboxTask", 4096, NULL, CBX3CONTROL_PRIORITY, &controlCocoboxTask_hdl, APP_CPU_NUM); // 受信後のコマンド処理タスク

    // eTaskState rcv_task_state = eTaskGetState(receiveTask_hdl);
    // eTaskState snd_task_state = eTaskGetState(sendTask_hdl);
    // eTaskState cbx_task_state = eTaskGetState(controlCocoboxTask_hdl);

    // cbx3_log(LOG_INF, "RT:%d / ST:%d /CBXT:%d", rcv_task_state, snd_task_state, cbx_task_state);
  }

  // ネットワークサービス確認
  cbx3_log(LOG_INF, "[ST2]->>CHECKING LTE NETWORK SERVICE");
  counter = 0;
  while (cbxState.nsi != NSI_Type::IN_SRV)
  {
    cbx3_log(LOG_INF, "[ST2]-->>REQUEST LTE SERVICE[%d]", counter);
    checkNSI();
    counter++;
    // if (cbxState.nsi == NSI_Type::TIMEOUT || counter > CHECK_NSI_MAXCOUNT)
    if (counter > CHECK_NSI_MAXCOUNT)
    {
      cbx_restart(); // 再起動させる
    }
    cbx_wait(CHECK_NSI_INTERVAL);
  }
  cbx3_log(LOG_INF, "[ST2]-->>AMT5102 SETUP FINISHED.");

  xTaskNotify(StartUpMainHdl, 0, eNoAction); // 完了を通知
  // タスク完了後にタスクを削除
  vTaskDelete(NULL); // このタスクを削除
}

//  WiFiメッセージ受信用タスク
void WiFiControlTask(void *pvParameters)
{
  while (true)
  {
    // Wifiサーバーがメッセージを受信したときの処理
    if (cbxState.isWiFiOn) // WiFi Onだった場合
    {
      if (wifi != nullptr)
      {
        LedController();
        wifi->handleClient();
        if (wifi->isMessage())
        {
          cbx3_log(LOG_INF, "WiFi MSG Received : %s", wifi->getMessage());
          cbx3_log(LOG_INF, "Send to server.");
          SendDatas dataSet;
          dataSet.cmd_type = WMSG;
          dataSet.rcvd_cmd = wifi->getMessage();
          dataSet.counter = timecode.getTimeCode();
          oDataComm.EncodeSndData(dataSet);
          amt5102->sendData(oDataComm.DataBuff);
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

//  TimeCodeUpdateタスク
void timecodeUpdateTask(void *pvParameters)
{
  while (true)
  {
    cbx3_log(LOG_INF, "REQUEST TIME CODE.(timecodeUpdateTask)");
    if (timecode.timeMode == SERVER_TIME || timecode.timeMode == ELPS_TIME)
    {
      cbx3_log(LOG_INF, "REQUEST SERVER TIME.(timecodeUpdateTask)");
      requestServerTime();
    }
    else if (timecode.timeMode == GPS_TIME)
    {
      cbx3_log(LOG_INF, "REQUEST GPS TIME.(timecodeUpdateTask)");
      SendDataATCom("AT+CCLK?");
    }
    else
    {
      // nothing
    }

    uint32_t waitTimeMs = spifs.tcUpdateDays * 24 * 60 * 60 * 1000; // spifs.tcUpdateDays x 日数（デフォルト1日）
    // waitTimeMs = 5 * 60 * 1000;                                       // 5分（テスト用）
    // waitTimeMs = 1 * 60 * 60 * 1000;                                                            // 1時間（テスト用）
    cbx3_log(LOG_INF, "TimeCodeUpdateDays: %d, Ticks: %d", spifs.tcUpdateDays, waitTimeMs); // 設定された日数分待機
    vTaskDelay(waitTimeMs);
  }
}

//  コマンド制御タスク
void controlCocoboxTask(void *pvParameters)
{
  std::vector<CocoBoxControlCommands> *commandList;

  while (true)
  {
    // キューからデータを受け取る
    if (xQueueReceive(cbx3ControlQueue, &commandList, portMAX_DELAY) == pdPASS)
    {
      controlCocoboxCallback(*commandList); // コールバックへ送る
    }
    else
    {
      // エラーログなどの処理
    }

    if (commandList)
    {
      delete commandList;    // メモリを解放
      commandList = nullptr; // ポインタを安全に扱うためnullに設定
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

//  受信タスク
void receiveTask(void *pvParameters)
{
  bool smpFlg = false; // セマフォ取得用

  while (true)
  {
    if (dataInFlg)
    {
      dataInFlg = false;
      // cbx3_log(LOG_INF, ">>>>RCVD");
      if (smpFlg == false)
      {
        // cbx_wait(100);
        if (xSemaphoreTake(ACMSemaphore, portMAX_DELAY) == pdTRUE) // セマフォ取得に成功した場合のみ
        {
          cbxState.isDataTransferring = true;
          LedController();
          smpFlg = true;
        }
        else
        {
          cbx3_log(LOG_ERR, "SemaphoreTake failed : rcvTask");
        }
        smpFlg = true;
      }
      amt5102->rcvData();

      if (amt5102->inDataFlg == READY)
      {
      }
      else if (amt5102->inDataFlg == ON_TRANS)
      {
      }
      else if (amt5102->inDataFlg == FINISHED)
      {
        char *recvBuffer = (char *)malloc(strlen(amt5102->in_buf_all) + 1);
        if (recvBuffer != NULL)
        {
          strcpy(recvBuffer, amt5102->in_buf_all);
          cbx3_log(LOG_INF, "RCV DATA : %s", replaceData4Disp(recvBuffer));
          // 受信データからコマンドに展開
          std::vector<CocoBoxControlCommands> *commands = new std::vector<CocoBoxControlCommands>(oDataComm.ChkRcvData(recvBuffer));

          free(recvBuffer); // メモリを解放
          amt5102->inDataFlg = READY;

          if (xQueueSend(cbx3ControlQueue, &commands, portMAX_DELAY) != pdPASS) // 実行タスクにキューを渡す
          {
            cbx3_log(LOG_ERR, "Failed to send command to queue");
            delete commands; // エラーハンドリングとしてメモリを解放
          }
        }
        else
        {
          cbx3_log(LOG_ERR, "Failed to allocate memory for recvBuffer");
        }
        cbx_wait(100);
        xSemaphoreGive(ACMSemaphore); // 排他制御終了
        // oLED_Com->setMode(LEDMode::OFF);
        smpFlg = false;
      }
      else
      {
        cbx3_log(LOG_INF, "STATUS : OTHER : %d", amt5102->inDataFlg);
        xSemaphoreGive(ACMSemaphore); // 排他制御終了
        // oLED_Com->setMode(LEDMode::OFF);
        smpFlg = false;
      }
    }
    cbx_wait(1); // 他のタスクに実行時間を与える
  }
}

// 送信タスク
void sendTask(void *pvParameters)
{
  char *data; // 送信データ用のポインタ

  while (true)
  {
    // 送信キューからデータを受け取る
    if (xQueueReceive(sendQueue, &data, portMAX_DELAY))
    {
      cbx3_log(LOG_INF, "SND QUEUE Received. (Queue available: %2u/%2u)", uxQueueSpacesAvailable(sendQueue), sendQueueSize);
      cbx3_log(LOG_INF, "SND DATA : %s", replaceData4Disp(data));
      xSemaphoreTake(ACMSemaphore, portMAX_DELAY); // 排他制御開始
      cbxState.isDataTransferring = true;
      LedController();

      // データの送信
      amt5102->sendData(data); // 64バイト分割送信
      cbx_wait(300);
      xSemaphoreGive(ACMSemaphore); // 排他制御終了
    }
    cbx_wait(2000); // 連続送信を避ける
  }
}

// TASKここまで////////////////////////////////////////////////////////////////////////////////////////
// CallBack////////////////////////////////////////////////////////////////////////////////////////
// ここボックスコマンド実行コールバック
void controlCocoboxCallback(const std::vector<CocoBoxControlCommands> &rcodeList)
{
  configSetting configSet;
  for (CocoBoxControlCommands rcode : rcodeList)
  {
    switch (rcode)
    {
    case CocoBoxControlCommands::LTE_LOCK: // DRLOCK受信
    {
      cbx3_log(LOG_INF, "CMD : LTE_LOCK");
      lockCommandHandler(LockReason::LTE, true);
      break;
    }
    case CocoBoxControlCommands::SELF_AUTOLOCK: // 自己AUTOLOCK
    {
      cbx3_log(LOG_INF, "CMD : SELF_AUTOLOCK");
      cbxState.lockReason = LockReason::SELF_AUTOLOCK;
      lockCommandHandler(LockReason::SELF_AUTOLOCK, true);
      break;
    }
    case CocoBoxControlCommands::LTE_AUTOLOCK: // DRAT_LOCK受信（サーバー指示自動施錠）
    {
      cbx3_log(LOG_INF, "CMD : LTE_AUTOLOCK");
      lockCommandHandler(LockReason::LTE_AUTOLOCK, true);
      break;
    }

    case CocoBoxControlCommands::LTE_UNLOCK: // DRUNLOCK受信
    {
      cbx3_log(LOG_INF, "CMD : LTE_UNLOCK");
      lockCommandHandler(LockReason::LTE, false);
      break;
    }

    case CocoBoxControlCommands::WIFI_LOCK: // WiFiからのLOCK
    {
      cbx3_log(LOG_INF, "CMD : WiFi_LOCK");
      cbxState.lockReason = LockReason::WIFI;
      lockCommandHandler(LockReason::WIFI, true);
      break;
    }

    case CocoBoxControlCommands::WIFI_UNLOCK: // WiFiからのUNLOCK
    {
      cbx3_log(LOG_INF, "CMD : WiFi_UNLOCK");
      cbxState.lockReason = LockReason::WIFI;
      lockCommandHandler(LockReason::WIFI, false);
      break;
    }

    case CocoBoxControlCommands::LTE_CHECK: // CHECKコマンド受信
      cbx3_log(LOG_INF, "Received command: COM_CHECK");
      SendDataCommon(CommandType::CHK); // データをエンコードして送信
      break;

    case CocoBoxControlCommands::LTE_RESET:
      cbx3_log(LOG_INF, "Received command: COM_RESET");
      cbx3_log(LOG_WAR, "SYSTEM RESET");
      cbx_restart(); // ESP32の再起動
      break;

    case CocoBoxControlCommands::LTE_AMT5102OK:
      cbx3_log(LOG_INF, "Received response: AMT5102_OK");
      // NOTHING
      break;

    case CocoBoxControlCommands::LTE_AMT5102_NSI:
      cbx3_log(LOG_INF, "Received response: AMT5102_NSI");
      cbxState.nsi = oDataComm.decodeNSI(oDataComm.NSI_buff);
      cbxState.isNSIReceived = true;
      break;

    case CocoBoxControlCommands::LTE_WIFION:
      cbx3_log(LOG_INF, "Received command: COM_WLON");
      startWifi();
      break;

    case CocoBoxControlCommands::LTE_WIFIOFF:
      cbx3_log(LOG_INF, "Received command: COM_WLOFF");
      stopWifi();
      break;

    case CocoBoxControlCommands::LTE_KCHK:
      cbx3_log(LOG_INF, "Received command: COM_KCHK");
      if (keyUnit.deviceExist)
      {
        tagChangeCallback(keyUnit.getCurrentTagIDs());
      }
      break;

    case CocoBoxControlCommands::LTE_SET: // SETコマンド受信
      cbx3_log(LOG_INF, "Received command: COM_SET");
      configSet = oDataComm.decodeConfigSetting(amt5102->in_buf_all);
      cbx3_log(LOG_INF, "RECEIVE CONFIG SETTING [%s][%d]", configSet.command.c_str(), configSet.value);

      if (configSet.command == "POLL")
      {
        if (configSet.value >= 0 && configSet.value <= 1440) // 範囲内であった場合
        {
          if (spifs.setConfig(&configSet)) // config.json書き換え
          {
            cbx3_log(LOG_INF, "Config Change Success : POLL");
            if (spifs.pollTimerValue > 0)
            {
              if (pollSendTimerHandle == NULL)
              {
                pollSendTimerHandle = xTimerCreate("PollSendTimer", spifs.pollTimerValue * 60 * 1000, pdTRUE, (void *)TIMER_ID_POLL_SEND, pollSendCallback);
              }
              else
              {
                xTimerChangePeriod(pollSendTimerHandle, spifs.pollTimerValue * 60 * 1000, 0);
              }
              xTimerStart(pollSendTimerHandle, 0);
              cbx3_log(LOG_INF, "POL INTERVAL SET TO %d[min]", spifs.pollTimerValue);
              SendDataLogMsg("SET POL INTERVAL :" + String(spifs.pollTimerValue) + "[min]");
            }
            else // zeroだった場合、POLLを停止する
            {
              if (pollSendTimerHandle != NULL)
              {
                xTimerStop(pollSendTimerHandle, 0);
              }
              cbx3_log(LOG_INF, "POL DISABLED (timer value set to ZERO)");
              SendDataLogMsg("POL DISABLED. (timer set to ZERO)");
            }
          }
          else
          {
            cbx3_log(LOG_INF, "Config Change Error : POLL");
            cbx3_log(LOG_ERR, "Config Change Fail");
            SendDataLogMsg("Config Change Fail : POLL");
          }
        }
        else // 範囲外だったらSPIFSの書き換えを行わない
        {
          cbx3_log(LOG_INF, "Config Change Error : POLL");
          cbx3_log(LOG_ERR, "out of range %d/(0-1440[min])", configSet.value);
          SendDataLogMsg("SET ERROR (out of range) : " + String(configSet.value) + "/(0-1440[min])");
          cbx3_log(LOG_INF, "Setting POLL %d[min]", spifs.pollTimerValue);
        }
      }
      else if (configSet.command == "TCUPDATE")
      {
        if (configSet.value >= 1 && configSet.value <= 7) // 範囲内であった場合
        {
          if (spifs.setConfig(&configSet)) // config.json書き換え
          {
            cbx3_log(LOG_INF, "Config Change Success : TCUPDATE");
            cbx3_log(LOG_INF, "Setting TimeCode Update  %02d[day(s)]", spifs.tcUpdateDays);
            SendDataLogMsg("SET TimeCodeUpdate to " + String(spifs.tcUpdateDays) + "[day(s)]");
          }
          else
          {
            cbx3_log(LOG_INF, "Config Change Error : TCUPDATE");
            cbx3_log(LOG_ERR, "Config Change Fail");
            SendDataLogMsg("Config Change Fail : TCUPDATE");
          }
        }
        else // 範囲外だったらSPIFSの書き換えを行わない
        {
          cbx3_log(LOG_INF, "Config Change Error : TCUPDATE");
          cbx3_log(LOG_ERR, "out of range %d/(1-7[day(s)])", configSet.value);
          SendDataLogMsg("SET ERROR (out of range) : " + String(configSet.value) + "/(1-7[days])");
          cbx3_log(LOG_INF, "Setting TimeCodeUpdate %d[day(s)]", spifs.tcUpdateDays);
        }
      }
      else if (configSet.command == "AUTOLOCK")
      {
        if (configSet.value >= 0 && configSet.value <= 300) // 範囲内であった場合
        {
          if (spifs.setConfig(&configSet)) // config.json書き換え
          {
            cbx3_log(LOG_INF, "Config Change Success : AUTOLOCK");
            if (spifs.autolockDelay == 0)
            {
              cbxState.isAutoLockEnable = false; // AUTOLOCKの禁止
              cbx3_log(LOG_INF, "Autolock Disabled. (TIMER SET TO %d)", spifs.autolockDelay);
              SendDataLogMsg("Autolock Disabled. (TIMER SET TO " + String(spifs.autolockDelay) + ")");
            }
            else
            {
              cbxState.isAutoLockEnable = true; // AUTOLOCKの許可
              xTimerChangePeriod(autoLockTimerHandle, pdMS_TO_TICKS(spifs.autolockDelay * 1000), 0);
              xTimerStop(autoLockTimerHandle, 0); // 周期変更を行うとタイマーが実行されてしまうので止める
              cbx3_log(LOG_INF, "Setting Autolock delay time Update to  %02d[s]", spifs.autolockDelay);
              SendDataLogMsg("SET Autolock delay time Update to " + String(spifs.autolockDelay) + "[sec]");
            }
          }
          else
          {
            cbx3_log(LOG_INF, "Config Change Error : AUTOLOCK");
            cbx3_log(LOG_ERR, "Config Change Fail");
            SendDataLogMsg("Config Change Fail : AUTOLOCK");
          }
        }
        else // 範囲外だったらSPIFSの書き換えを行わない
        {
          cbx3_log(LOG_INF, "Config Change Error : AUTOLOCK");
          cbx3_log(LOG_ERR, "out of range %d/(1-300[sec])", configSet.value);
          SendDataLogMsg("SET ERROR (out of range) : " + String(configSet.value) + "/(1-300[sec])");
          cbx3_log(LOG_INF, "Setting AUTOLOCK %d[sec]", spifs.autolockDelay);
        }
      }
      else if (configSet.command == "VTH")
      {
        if (configSet.value >= 5000 && configSet.value <= 9000) // 範囲内であった場合
        {
          if (spifs.setConfig(&configSet)) // config.json書き換え
          {
            cbx3_log(LOG_INF, "Config Change Success : VTH");
            // oBatChecker->milliVoltLowThreshold = spifs.vth;
            cbx3_log(LOG_INF, "LowVoltage Threshold Update to  %04d[mV]", spifs.vth);
            SendDataLogMsg("SET LowVoltage Threshold Update to " + String(spifs.vth) + "[mV]");
          }
          else
          {
            cbx3_log(LOG_INF, "Config Change Error : VTH");
            cbx3_log(LOG_ERR, "Config Change Fail");
            SendDataLogMsg("Config Change Fail : VTH");
          }
        }
        else // 範囲外だったらSPIFSの書き換えを行わない
        {
          cbx3_log(LOG_INF, "Config Change Error : VTH");
          cbx3_log(LOG_ERR, "out of range %d/(5000-9000[mV])", configSet.value);
          SendDataLogMsg("SET ERROR (out of range) : " + String(configSet.value) + "/(5000-9000[mV])");
          cbx3_log(LOG_INF, "Setting VTH %4d[mV]", spifs.vth);
        }
      }
      else if (configSet.command == "VCALP")
      {
        if (configSet.value >= 0 && configSet.value <= 1000) // 範囲内であった場合
        {
          if (spifs.setConfig(&configSet)) // config.json書き換え
          {
            cbx3_log(LOG_INF, "Config Change Success : VCAL");
            oBatChecker->VrefCalib = spifs.vcal;
            cbx3_log(LOG_INF, "VCAL Update to  %04d[mV]", spifs.vcal);
            SendDataLogMsg("SET VCAL Update to " + String(spifs.vcal) + "[mV]");
          }
          else
          {
            cbx3_log(LOG_INF, "Config Change Error : VCALP");
            cbx3_log(LOG_ERR, "Config Change Fail");
            SendDataLogMsg("Config Change Fail : VCALP");
          }
        }
        else // 範囲外だったらSPIFSの書き換えを行わない
        {
          cbx3_log(LOG_INF, "Config Change Error : VCALP");
          cbx3_log(LOG_ERR, "out of range %d/(0~1000[mV])", configSet.value);
          SendDataLogMsg("SET ERROR (out of range) : " + String(configSet.value) + "/(0~1000[mV])");
          cbx3_log(LOG_INF, "Setting VCAL %4d[mV]", spifs.vcal);
        }
      }
      else if (configSet.command == "VCALM")
      {
        if (configSet.value >= 0 && configSet.value <= 1000) // 範囲内であった場合
        {
          configSet.value *= -1;           // 反転
          if (spifs.setConfig(&configSet)) // config.json書き換え
          {
            cbx3_log(LOG_INF, "Config Change Success : VCALM");
            oBatChecker->VrefCalib = spifs.vcal;
            cbx3_log(LOG_INF, "VCAL Update to  %04d[mV]", spifs.vcal);
            SendDataLogMsg("SET VCAL Update to " + String(spifs.vcal) + "[mV]");
          }
          else
          {
            cbx3_log(LOG_INF, "Config Change Error : VCALM");
            cbx3_log(LOG_ERR, "Config Change Fail");
            SendDataLogMsg("Config Change Fail : VCALM");
          }
        }
        else // 範囲外だったらSPIFSの書き換えを行わない
        {
          cbx3_log(LOG_INF, "Config Change Error : VCAL");
          cbx3_log(LOG_ERR, "out of range %d/(0~1000[mV])", configSet.value);
          SendDataLogMsg("SET ERROR (out of range) : " + String(configSet.value) + "/(0~1000[mV])");
          cbx3_log(LOG_INF, "Setting VCAL %4d[mV]", spifs.vcal);
        }
      }
      else
      {
        cbx3_log(LOG_INF, "SET ERROR (Unknown Set Command)");
        SendDataLogMsg("SET ERROR (Unknown Set Command)");
      }
      break;

    case CocoBoxControlCommands::LTE_GPSTIME: // GPS時刻の取得
      cbx3_log(LOG_INF, "Received GPS TIMECODE");
      timecode.setGPSTimeCode(amt5102->in_buf_all);
      timecode.timeMode = GPS_TIME;
      SendDataLogMsg("TIMECODE UPDATE : corrected " + String(timecode.timeDifference) + " sec");
      break;

    case CocoBoxControlCommands::LTE_SERVERTIME: // サーバータイムの取得
      if (!cbxState.isTimeCodeMask)
      {
        cbxState.isTimeCodeMask = true; // 一定時間マスクする
        timecodeMaskTimerHandle = xTimerCreate("timecodeMaskTimer", pdMS_TO_TICKS(TIMECODE_MASK_TIME), pdFALSE, (void *)TIMER_ID_TIMECODE_MASK, TimeCodeMaskCallback);
        if (timecodeMaskTimerHandle != NULL)
        {
          if (xTimerStart(timecodeMaskTimerHandle, 0) == pdPASS)
          {
            cbx3_log(LOG_INF, "timeCodeMaskTimer Started");
          }
          else
          {
            cbx3_log(LOG_ERR, "ERROR:failed to start timeCodeMaskTimer.");
            // 必要であれば、タイマーハンドルの解放を検討 (vTimerDelete)
          }
        }
        else
        {
          cbx3_log(LOG_ERR, "ERROR:failed to start timeCodeMaskTimer.");
        }
        cbx3_log(LOG_INF, "Received SERVERTIME");

        timecode.setServerTimeCode(amt5102->in_buf_all);
        timecode.timeMode = SERVER_TIME;
        SendDataLogMsg("TIMECODE UPDATE : corrected " + String(timecode.timeDifference) + " sec");
      }
      break;

    case CocoBoxControlCommands::LTE_ANSWERBACK: // ANSWERBACKの受信
      cbx3_log(LOG_INF, "Received response: ANSWERBACK");
      // NOTHING
      break;

    case CocoBoxControlCommands::LTE_OK: // OKコマンドの受信
      cbx3_log(LOG_INF, "Received response: COM_OK");
      // NOTHING
      break;

    case CocoBoxControlCommands::LTE_ERROR: // ERRORコマンドの受信
      cbx3_log(LOG_INF, "Received response: COM_ERROR");
      // TODO: NOTHING
      break;

    case CocoBoxControlCommands::LTE_NODATA: // データを受信していないとき
      cbx3_log(LOG_INF, "Received response: COM_NODATA");
      // NOTHING
      break;

    default: // 不明なコマンドの受信
      // cbx3_log(LOG_WAR, "Unknown command received : code=%d", rcode);
      break;
    }
    executedCode = rcode;
    cbx_wait(100); // 複合でコマンドが来た時に連続動作を避ける
  }
}

// WiFiスイッチコールバック関数
void WSCallback(SwitchEvent event)
{
  if (event == LONG_PUSH)
  {
    cbx3_log(LOG_INF, "WIFI_SW_RCV");
    if (xSemaphoreTake(wifiMutex, portMAX_DELAY) == pdTRUE)
    {
      if (cbxState.isWiFiOn)
      {
        cbx3_log(LOG_INF, "STOP_WIFI");
        stopWifi();
      }
      else
      {
        cbx3_log(LOG_INF, "START_WIFI");
        startWifi();
      }
      xSemaphoreGive(wifiMutex);
    }
    else
    {
      cbx3_log(LOG_WAR, "WSCallback WiFi Mutex acquisition failed");
    }
  }
}

// POWER-GOOD検知コールバック関数 ※PWR-GOOD関連はいまのところ監視のみ。一応実装しているレベル
void PGDCallback(SwitchEvent event)
{
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
}

// EQ検知コールバック関数
void EQCallback(SwitchEvent event)
{
  isEqOn();               // ステータス更新
  if (event == SWITCH_ON) // EQ検知
  {
    SendDataCommon(CommandType::EQON);
    cbx_wait(EQ_RESET_TIMER); // EQリセットまでの時間
    EQ_reset();
  }
  else if (event == SWITCH_OFF)
  {
    SendDataCommon(CommandType::EQOF);
  }
  else
    return;
}

// AC/BT検知コールバック関数
void PSCallback(SwitchEvent event)
{
  isPowerAC(); // ステータス更新
  LedController();
  if (event == SWITCH_OFF) // AC
  {
    SendDataCommon(CommandType::PWAC);
  }
  else if (event == SWITCH_ON)
  {
    SendDataCommon(CommandType::PWBT);
  }
  else
    return;
}

// 鍵スイッチコールバック関数
void KSCallback(SwitchEvent event)
{
  cbx3_log(LOG_INF, "KSCallback : LockReason=%d", cbxState.lockReason);
  isKeyLocked(); // ステータス更新
  LedController();
  // 鍵ステータスが変化したらロックタイムアウトタイマーを止める
  xTimerStop(lockTimeOutTimerHandle, 0);

  if (cbxState.isAutoLocking)
  {
    cbxState.isAutoLocking = false;

    LedController();
    if (xTimerIsTimerActive(autoLockTimerHandle) == pdTRUE)
    {
      if (xTimerStop(autoLockTimerHandle, 0) == pdPASS)
      {
        cbx3_log(LOG_INF, "Auto-lock timer stopped because key is already Locked.");
      }
    }
  }

  // タイマーを一旦停止する（前のタイマーが残っている場合があるので）
  if (doorErrorOneShotTimer != NULL)
  {
    xTimerStop(doorErrorOneShotTimer, 0);
  }

  // 施錠されたとき
  if (cbxState.isKeyLocked)
  {
    if (cbxState.isDoorClosed) // ドアが閉まっている場合（正常動作）
    {
      if (cbxState.lockReason == LockReason::MANUAL)
      {
        SendDataCommon(CommandType::KGLK);
      }
      else if (cbxState.lockReason == LockReason::LTE)
      {
        SendDataCommon(CommandType::LOCK);
        cbxState.lockReason = LockReason::MANUAL; // 動作後はマニュアルモードに戻さないと手動は検知がない
      }
      else if (cbxState.lockReason == LockReason::SELF_AUTOLOCK)
      {
        SendDataCommon(CommandType::ATLK);
        cbxState.lockReason = LockReason::MANUAL; // 動作後はマニュアルモードに戻さないと手動は検知がない
      }
      else if (cbxState.lockReason == LockReason::LTE_AUTOLOCK)
      {
        SendDataCommon(CommandType::ALRT);
        cbxState.lockReason = LockReason::MANUAL; // 動作後はマニュアルモードに戻さないと手動は検知がない
      }
      else if (cbxState.lockReason == LockReason::WIFI)
      {
        SendDataCommon(CommandType::WFLK);
        cbxState.lockReason = LockReason::MANUAL; // 動作後はマニュアルモードに戻さないと手動は検知がない
      }
    }
    else // ドアが開いているのに施錠されたらErrorを発報する（センサー不具合）
    {
      if (doorErrorOneShotTimer != NULL)
      {
        // タイマーをリスタート
        if (xTimerStart(doorErrorOneShotTimer, 0) != pdPASS)
        {
          cbx3_log(LOG_ERR, "Failed to restart doorErrorOneShotTimer.");
        }
        else
        {
          cbx3_log(LOG_INF, "DERR TIMER START");
        }
      }
    }
  }

  else // 開錠されたとき
  {
    if (cbxState.lockReason == LockReason::MANUAL)
    {
      SendDataCommon(CommandType::KGUL);
    }
    else if (cbxState.lockReason == LockReason::LTE)
    {
      SendDataCommon(CommandType::UNLOCK);
      cbxState.lockReason = LockReason::MANUAL; // 動作後はマニュアルモードに戻さないと手動は検知がない
    }
    else if (cbxState.lockReason == LockReason::WIFI)
    {
      SendDataCommon(CommandType::WFUL);
      cbxState.lockReason = LockReason::MANUAL; // 動作後はマニュアルモードに戻さないと手動は検知がない
    }
    if (!cbxState.isDoorClosed) // 開錠と同時にドアが開いたとき（先にレバーが上がっていた場合）
    {
      SendDataCommon(CommandType::DROP); // ドアオープンを通知してやる
    }
  }
}

// ドアスイッチコールバック関数
void DSCallback(SwitchEvent event)
{
  isDoorClosed(); // ステータス更新
  LedController();
  if (cbxState.isKeyLocked) // 施錠中だった場合
  {
    // 施錠中にドアスイッチが変化した場合の処理
    cbx3_log(LOG_WAR, "DOOR SW CANCELED（INTERLOCK）");
    cbxState.isDoorClosed = true; // ドアステータスは強制的にCLOSEにする
  }
  else // 開錠中
  {
    if (cbxState.isDoorClosed) // ドアクローズ
    {
      SendDataCommon(CommandType::DRCL); // サーバーへ送信
      if (autoLockTimerHandle != NULL)
      {
        if (cbxState.isAutoLockEnable)
        {

          // AutoLockTimerの開始
          if (xTimerStart(autoLockTimerHandle, 0) != pdPASS)
          {
            // タイマー開始失敗
            cbx3_log(LOG_ERR, "Failed to start AutoLockTimer");
          }
          else
          {
            cbx3_log(LOG_INF, "AutoLock timer Started on DSCallback.");
            cbxState.isAutoLocking = true;
            LedController();
            cbx_wait(100); // シリアルモニター表示乱れのための調整
          }
        }
        else
        {
          cbx3_log(LOG_INF, "(AutoLock Disable)");
        }
      }
      else
      {
        cbx3_log(LOG_ERR, "Failed to create AutoLock timer.");
      }

      if (keyUnit.deviceExist)
      {
        tagChangeCallback(keyUnit.getCurrentTagIDs()); // ドアを閉めたら鍵情報を送る
      }
    }
    else // ドアオープン
    {
      SendDataCommon(CommandType::DROP);
      // 自動ロックタイマーが動作中なら停止する
      if (xTimerIsTimerActive(autoLockTimerHandle) == pdTRUE)
      {
        xTimerStop(autoLockTimerHandle, 0);
        cbx3_log(LOG_INF, "AutoLock timer stopped due to door open.");
        cbxState.isAutoLocking = false;
        LedController();
      }
    }
  }
}

// 鍵情報送信用コールバック
void tagChangeCallback(const String &UIDs)
{
  SendDatas dataSet;
  dataSet.cmd_type = KBOX;
  dataSet.UIDs = keyUnit.getCurrentTagIDs();
  dataSet.counter = timecode.getTimeCode();

  // データのエンコード
  oDataComm.EncodeSndData(dataSet);

  // エンコードされたデータを取得
  char *encodedData = oDataComm.DataBuff;

  // 送信キューにデータを追加
  if (xQueueSend(sendQueue, &encodedData, portMAX_DELAY) != pdPASS)
  {
    cbx3_log(LOG_ERR, "Failed to send to sendQueue");
  }
  else
  {
    // waitExecutedCode(ANSWERBACK);
    // waitExecutedCode(COM_OK);
    // cbx3_log(LOG_INF, "Data added to sendQueue: %s", replaceData4Disp(encodedData));
  }
}

// ACMイベントコールバック
void acm_events(int event, void *data, size_t len)
{
  switch (event)
  {
  case CDC_CTRL_SET_CONTROL_LINE_STATE:
    amt5102->setLineCoding(115200, 0, 0, 8);
    break;

  case CDC_DATA_IN:
  {
    // cbx3_log(LOG_INF, "CDC_DATA_IN");
    dataInFlg = true;
    break;
  }
  case CDC_DATA_OUT:
    break;

  case CDC_CTRL_SET_LINE_CODING:
    break;
  }
}

// USBクライアントイベントコールバック
void USB_client_event_callback(const usb_host_client_event_msg_t *event_msg, void *arg)
{
  // 新しいデバイスが接続されたらCDCACMの接続を行う
  if (event_msg->event == USB_HOST_CLIENT_EVENT_NEW_DEV)
  {
    cbx_wait(1000); // 安定化のため。暫定処置
    cbx3_log(LOG_INF, "USB DEVICE DETECTED.");
    usb_dev_flg = true;
    host->open(event_msg);
    // usb_device_info_t info = host.getDeviceInfo();
    const usb_device_desc_t *dev_desc = host->getDeviceDescriptor();
    int offset = 0;
    if (dev_desc->bNumConfigurations == 1)
    // Configは必ず１個になるはずなのでこうしておく
    {
      const usb_config_desc_t *config_desc =
          host->getConfigurationDescriptor(); // Config Descripterの取得
      for (size_t n = 0; n < config_desc->bNumInterfaces; n++)
      {
        const usb_intf_desc_t *intf =
            usb_parse_interface_descriptor(config_desc, n, 0, &offset);
        if (intf->bInterfaceClass == USB_CDCDATA) // CDC ACM
        {
          amt5102 = new AMT5102(config_desc, host);
          if (amt5102)
          {
            amt5102->init();
            amt5102->onEvent(acm_events);
            amt5102->setControlLine(1, 1);
            amt5102->rcvData();
          }
        }
      }
    }
    else
    {
      cbx3_log(LOG_ERR, "bNumConfigurations not 1");
    }
  }
  else
  {
    cbx3_log(LOG_WAR, "amt5102 gone event");
    usb_dev_flg = false;
  }
}

// POLL送信タイマーコールバック
void pollSendCallback(TimerHandle_t xTimer)
{
  // POLLタイマー発報時の処理
  if (ENDULANCE_MODE) // 耐久モードではPOLLの代わりにロック→アンロックを実行
  {
    if (cbxState.endurance_flg == 0) // ロック側
    {
      std::vector<CocoBoxControlCommands> *commandList = new std::vector<CocoBoxControlCommands>;
      commandList->push_back(CocoBoxControlCommands::LTE_LOCK);
      if (xQueueSend(cbx3ControlQueue, &commandList, portMAX_DELAY) != pdPASS) // 実行タスクにキューを渡す
      {
        delete commandList; // キュー送信失敗時にメモリを解放
        cbx3_log(LOG_ERR, "Failed to send command to queue");
      }
      cbxState.endurance_flg = 1;
    }
    else if (cbxState.endurance_flg == 1) // アンロック側
    {
      std::vector<CocoBoxControlCommands> *commandList = new std::vector<CocoBoxControlCommands>;
      commandList->push_back(CocoBoxControlCommands::LTE_UNLOCK);
      if (xQueueSend(cbx3ControlQueue, &commandList, portMAX_DELAY) != pdPASS) // 実行タスクにキューを渡す
      {
        delete commandList; // キュー送信失敗時にメモリを解放
        cbx3_log(LOG_ERR, "Failed to send command to queue");
      }
      cbxState.endurance_flg = 2;
    }
    else if (cbxState.endurance_flg == 2)
    {
      cbxState.endurance_flg = 0;
    }
  }
  else
  {
    // POLLを送る
    SendDataCommon(CommandType::POLL);
  }
}

// Lockタイムアウトタイマーコールバック
// Lockタイムアウトタイマーコールバック
void LockTimeoutCallback(TimerHandle_t xTimer)
{
  if (cbxState.isAutoLocking)
  {
    cbxState.isAutoLocking = false;
    LedController();
  }

  if (cbxState.isKeyLocked == cbxState.lockTarget)
  {
    // Lock成功
  }
  else
  {
    SendDataCommon(CommandType::ULER);
    cbx3_log(LOG_ERR, "LOCK FAIL(lock timeout)");
  }
}

// 自動ロックタイマーのコールバック関数
void AutoLockCallback(TimerHandle_t xTimer)
{
  if (cbxState.isAutoLocking) // この判断は不要になったが念のため残しておく
  {
    std::vector<CocoBoxControlCommands> *commands = new std::vector<CocoBoxControlCommands>();
    commands->push_back(CocoBoxControlCommands::SELF_AUTOLOCK);

    if (xQueueSend(cbx3ControlQueue, &commands, portMAX_DELAY) != pdPASS) // 実行タスクにキューを渡す
    {
      cbx3_log(LOG_ERR, "Failed to send command to queue");
      delete commands; // エラーハンドリングとしてメモリを解放
    }
    else
    {
      cbx3_log(LOG_INF, "AUTO LOCK QUED.");
      cbx_wait(300); // シリアルモニター表示バグのための調整
    }
  }
  else
  {
    cbx3_log(LOG_ERR, "Autolock canseled.(Autolock Called but isAutolocking=false)");
  }
}

void TimeCodeMaskCallback(TimerHandle_t xTimer) // Timecode受信マスク用タイマーコールバック
{
  cbx3_log(LOG_INF, "UNMASK TIMECODE UPDATE MASK");
  cbxState.isTimeCodeMask = false;
}

// ドアエラー検出コールバック
void onDoorErrorCB(TimerHandle_t doorErrorOneShotTimer)
{
  // タイマーが満了したときにコマンドを実行
  cbx3_log(LOG_ERR, "Door Error: Door open, but lock status ON.");
  SendDataCommon(CommandType::DRER); // エラー送信
}

// WiFi用最新のステータス更新処理コールバック
void updateWiFiStatus()
{
  // センサーや他のリソースから最新データを取得してステータスを更新
  oDHT22->ReadData();
  wifi->status.FWVersion = String(FW_VER);
  wifi->status.timecode = timecode.getTimeCode();
  wifi->status.isKSUexist = I2C_checkDeviceExists(I2C_KEYUNIT_ADDR);
  wifi->status.isKeyLocked = isKeyLocked();
  wifi->status.isDoorClosed = isDoorClosed();
  wifi->status.isACPower = isPowerAC();
  wifi->status.temperature = oDHT22->TMP;
  wifi->status.humidity = oDHT22->HUM;
  wifi->status.BatteryVolt = (float)readBatteryMilliVolt() / 1000;
  cbx3_log(LOG_INF, "WiFi-Status updated.");
}

// WiFiからのLockコールバック
void WiFiLockCB()
{
  cbx3_log(LOG_INF, "WiFi LOCK called");
  std::vector<CocoBoxControlCommands> *commands = new std::vector<CocoBoxControlCommands>();
  commands->push_back(CocoBoxControlCommands::WIFI_LOCK); // COM_DRLOCK を追加

  if (xQueueSend(cbx3ControlQueue, &commands, portMAX_DELAY) != pdPASS) // 実行タスクにキューを渡す
  {
    cbx3_log(LOG_ERR, "Failed to send command to queue");
    delete commands; // エラーハンドリングとしてメモリを解放
  }
  else
  {
    cbx3_log(LOG_INF, "WiFi LOCK executed");
  }
}

// WiFiからのUnLockコールバック
void WiFiunlockCB()
{
  cbx3_log(LOG_INF, "WiFi UnLOCK called");
  std::vector<CocoBoxControlCommands> *commands = new std::vector<CocoBoxControlCommands>();
  commands->push_back(CocoBoxControlCommands::WIFI_UNLOCK); // COM_DRLOCK を追加

  if (xQueueSend(cbx3ControlQueue, &commands, portMAX_DELAY) != pdPASS) // 実行タスクにキューを渡す
  {
    cbx3_log(LOG_ERR, "Failed to send command to queue");
    delete commands; // エラーハンドリングとしてメモリを解放
  }
  else
  {
    cbx3_log(LOG_INF, "WiFi UnLOCK executed");
  }
  // UNLOCKに関する具体的な処理を記述
}

/// その他の関数////////////////////////////////////////////////////////////////
// LTE接続チェック
void checkNSI()
{
  unsigned long startTime = millis();
  SendDataATCom("AT@NSI"); // Request NSI
  cbx3_log(LOG_INF, "WAITING LTE SERVICE");
  while (!cbxState.isNSIReceived)
  {
    if (millis() - startTime > CHECK_NSI_TIMEOUT)
    {
      cbx3_log(LOG_INF, "NSI CHECK TIMEOUT");
      cbxState.nsi = NSI_Type::TIMEOUT;
      return; // TimeOut
    }
    cbx_wait(10);
  }
  cbxState.isNSIReceived = false;
  cbx3_log(LOG_INF, "NSI RECEIVED[%d]", cbxState.nsi);
  return;
}

// Lock/Unlock制御
void lockCommandHandler(LockReason reason, bool isLock)
{
  if (lockTimeOutTimerHandle != NULL)
  {
    xTimerStart(lockTimeOutTimerHandle, 0);
    cbx3_log(LOG_INF, "LOCK TIMER STARTED on LockHandler");
  }
  cbxState.lockReason = reason;
  cbxState.lockTarget = isLock;

  if (isLock) // Lock動作
  {
    if (!cbxState.isKeyLocked) // UNLOCKになっているか確認
    {
      oLockSystem->KGLock(SET_SV_LK, cbxState.isDoorClosed);
    }
    else
    {
      cbx3_log(LOG_INF, "LOCK CANCELED(Already Locked)");
      if (cbxState.lockReason == LockReason::LTE)
      {
        SendDataCommon(CommandType::LOCK_C);
        cbxState.lockReason = LockReason::MANUAL; // 動作後はマニュアルモードに戻さないと手動は検知がない
      }
      else if (cbxState.lockReason == LockReason::LTE_AUTOLOCK)
      {
        SendDataCommon(CommandType::ALRT_C);
        cbxState.lockReason = LockReason::MANUAL; // 動作後はマニュアルモードに戻さないと手動は検知がない
      }
      else if (cbxState.lockReason == LockReason::WIFI)
      {
        SendDataCommon(CommandType::WFLK_C);
        cbxState.lockReason = LockReason::MANUAL; // 動作後はマニュアルモードに戻さないと手動は検知がない
      }
    }
  }
  else // Unlock動作
  {
    if (cbxState.isKeyLocked) // LOCKになっているか確認
    {
      oLockSystem->KGLock(SET_SV_UL, cbxState.isKeyLocked);
    }
    else
    {
      cbx3_log(LOG_INF, "UNLOCK CANCELED(Already Unlocked)");
      if (cbxState.lockReason == LockReason::LTE)
      {
        SendDataCommon(CommandType::UNLOCK_C);
        cbxState.lockReason = LockReason::MANUAL; // 動作後はマニュアルモードに戻さないと手動は検知がない
      }
      else if (cbxState.lockReason == LockReason::WIFI)
      {
        SendDataCommon(CommandType::WFUL_C);
        cbxState.lockReason = LockReason::MANUAL; // 動作後はマニュアルモードに戻さないと手動は検知がない
      }
    }
  }
}

// LED Controller
void LedController()
{
  // オレンジ
  if (!cbxState.isAMT5102Ready)
  {
    oLED_Com->setMode(LEDMode::BLINK_FAST);
  }
  else if (cbxState.isDataTransferring)
  {
    oLED_Com->setMode(LEDMode::ON_Delay); // 一定時間継続点灯
    cbxState.isDataTransferring = false;
    return;
  }
  else if (cbxState.isWiFiOn) // WiFi
  {
    oLED_Com->setMode(LEDMode::BLINK_SLOW);
  }
  else
  {
    oLED_Com->setMode(LEDMode::OFF); // オレンジ消灯
  }

  // 白
  if (cbxState.onStart)
  {
    oLED_DS->setMode(LEDMode::OFF);
  }
  else if (cbxState.isKeyLocked) // 鍵がかかっていたらドアLEDは強制OFF
  {
    oLED_DS->setMode(LEDMode::OFF); // LEDの消灯
  }
  else
  {
    if (cbxState.isDoorClosed)
    {
      oLED_DS->setMode(LEDMode::OFF); // LEDの消灯
    }
    else
    {
      oLED_DS->setMode(LEDMode::ON); // LEDの点灯
    }
  }

  // 緑
  if (cbxState.onStart)
  {
    oLED_KS->setMode(LEDMode::BLINK_FAST);
  }
  else if (cbxState.isAutoLocking)
  {
    oLED_KS->setMode(LEDMode::BLINK_SLOW);
  }
  else if (cbxState.isKeyLocked)
  {
    oLED_KS->setMode(LEDMode::OFF);
  }
  else
  {
    oLED_KS->setMode(LEDMode::ON);
  }

  // 赤
  if (cbxState.onStart)
  {
    oLED_Pwr->setMode(LEDMode::BLINK_FAST);
  }
  else if (cbxState.isPowerAC)
  {
    oLED_Pwr->setMode(LEDMode::ON);
  }
  else
  {
    oLED_Pwr->setMode(LEDMode::BLINK_SLOW);
  }

  // EQ
}

// 感震センサーのリセット出力
void EQ_reset()
{
  digitalWrite(EQ_RST, ON);
  cbx_wait(500); // 0.5秒ONにして離す
  digitalWrite(EQ_RST, OFF);
}

// USB電源のON/OFF
void usb_power(bool flg)
{
  digitalWrite(AMT5102_EN, flg);
}

// WiFiの開始と停止処理
void startWifi()
{
  if (wifi == nullptr)
  {
    cbxState.isWiFiOn = true;
    LedController();
    cbx3_log(LOG_DBG, "Creating new cbxWiFi instance");
    wifi = new cbxWiFi(); // WiFi制御用のクラスのインスタンスを作成
    // ステータス更新用コールバックを登録
    wifi->setStatusUpdateCallback(updateWiFiStatus);
    wifi->setLockCallback(WiFiLockCB);
    wifi->setUnlockCallback(WiFiunlockCB);
    cbx3_log(LOG_DBG, "Starting WiFi");
    wifi->startWifi();
    vTaskResume(WiFiControlTaskHandle);
    cbx3_log(LOG_DBG, "WiFi started");

    // WiFi開始のデータを送信
    SendDataCommon(CommandType::WLON);
  }
}

void stopWifi()
{
  if (wifi != nullptr)
  {
    vTaskSuspend(WiFiControlTaskHandle);
    cbxState.isWiFiOn = false;
    wifi->stopWifi();
    delete wifi; // WiFiインスタンスを削除
    wifi = nullptr;
    LedController();

    // WiFi停止のデータを送信
    SendDataCommon(CommandType::WLOF);
  }
}

// USB デバイスの初期化処理
void initUSBDevice()
{
  host->registerClientCb(USB_client_event_callback); // イベントコールバック関数の登録
  host->init();                                      // USB HOST 初期化
}

// AMT5102 の接続待ち処理
bool isAMT5102Connected()
{
  while (true) // AMT5102接続待ち
  {
    // cbx3_log(LOG_DBG, "WAITING ACM5102 CONNECTION...");
    if (amt5102 && amt5102->isConnected())
    {
      return true;
    }
    else
    {
      return false;
      // 必要ならリセット処理を入れる
    }
  }
}

// 通常データ送信用
void SendDataCommon(CommandType cmd_type)
{
  if (cmd_type == CommandType::DRCL || cmd_type == CommandType::DROP)
  {
    if (cmd_type == cbxState.lastSendDoorStatus)
    {
      cbx3_log(LOG_INF, "Data Send Cansel(SAME STATUS).");
      return;
    }
    bool tmpDoorState = cbxState.isDoorClosed;
    for (int i = 0; i < 10; i++)
    {
      isDoorClosed();
      cbx_wait(DRCOMMAND_SEND_DELAY / 10);
      if (tmpDoorState != cbxState.isDoorClosed)
      {
        cbx3_log(LOG_INF, "Data Send Cansel(STATUS CHANGED).");
        return;
      }
    }
  }

  if (amt5102 && amt5102->isConnected())
  {
    // 最初にステータスを読み込む
    isKeyLocked();
    isDoorClosed();
    isPowerAC();
    readBatteryMilliVolt();

    // SendData用データセット作成
    SendDatas dataSet;
    dataSet.cmd_type = cmd_type;
    dataSet.st_isKeyLocked = cbxState.isKeyLocked;
    dataSet.st_isDoorClosed = cbxState.isDoorClosed;
    dataSet.st_isEqOn = EQ_hdl->getState();
    dataSet.DHT22msg = oDHT22->getStrMessage();
    dataSet.st_isPwrAC = cbxState.isPowerAC;
    dataSet.vbat = (float)(cbxState.BatteryMilliVolt) / 1000; // mV→Vに変換して送る
    dataSet.st_isWiFiOn = cbxState.isWiFiOn;
    dataSet.counter = timecode.getTimeCode();
    dataSet.deviceID = cbxState.chipID;

    oDataComm.EncodeSndData(dataSet);

    // エンコードされたデータを取得
    char *encodedData = oDataComm.DataBuff;

    // 送信キューにデータを追加
    if (xQueueSend(sendQueue, &encodedData, portMAX_DELAY) != pdPASS)
    {
      cbx3_log(LOG_ERR, "Failed to send to sendQueue");
    }
    else
    {
      if (cmd_type == CommandType::DRCL || cmd_type == CommandType::DROP)
      {
        cbxState.lastSendDoorStatus = cmd_type; // 最後に送ったドアステータスを格納
      }
    }
  }
}

// サーバー時刻リクエスト
void requestServerTime()
{
  if (amt5102 && amt5102->isConnected())
  {
    SendDatas dataSet;
    dataSet.cmd_type = WHAT_THE_TIME;

    oDataComm.EncodeSndData(dataSet);

    // エンコードされたデータを取得
    char *encodedData = oDataComm.DataBuff;

    // 送信キューにデータを追加
    if (xQueueSend(sendQueue, &encodedData, portMAX_DELAY) != pdPASS)
    {
      cbx3_log(LOG_ERR, "Failed to send to sendQueue");
    }
    else
    {
      cbx3_log(LOG_INF, "QUEING SERVER TIME REQUEST");
    }
  }
}

// void requestTimeCode()
// {
//   xTaskNotify(timecodeUpdateHdl, 0, eNoAction); // timecodeUpdateTaskへ通知
// }

// ATコマンド送信
void SendDataATCom(String atcommand)
{
  SendDatas dataSet;
  dataSet.cmd_type = ATCOM;
  dataSet.AT_cmd_main = atcommand;
  oDataComm.EncodeSndData(dataSet);

  // エンコードされたデータを取得
  char *encodedData = oDataComm.DataBuff;

  // 送信キューにデータを追加
  if (xQueueSend(sendQueue, &encodedData, portMAX_DELAY) != pdPASS)
  {
    cbx3_log(LOG_ERR, "Failed to send to sendQueue");
  }
  else
  {
    // cbx3_log(LOG_INF, "Data added to sendQueue: %s", replaceData4Disp(encodedData));
  }
}

// LogMessage送信
void SendDataLogMsg(String msg)
{
  SendDatas dataSet;
  dataSet.cmd_type = LOG;
  dataSet.LogMsg = msg;
  dataSet.counter = timecode.getTimeCode();
  oDataComm.EncodeSndData(dataSet);

  // エンコードされたデータを取得
  char *encodedData = oDataComm.DataBuff;
  cbx3_log(LOG_DBG, "SEND MSG %s", oDataComm.DataBuff);

  // 送信キューにデータを追加
  if (xQueueSend(sendQueue, &encodedData, portMAX_DELAY) != pdPASS)
  {
    cbx3_log(LOG_ERR, "Failed to send to sendQueue");
  }
  else
  {
    // cbx3_log(LOG_INF, "Data added to sendQueue: %s", replaceData4Disp(encodedData));
  }
}

// バッテリー電圧確認
float readBatteryMilliVolt()
{
  cbxState.BatteryMilliVolt = oBatChecker->milliVoltRead();
  cbx3_log(LOG_DBG, "READ BATTERY VOLTAGE %d[mV]", cbxState.BatteryMilliVolt);
  return cbxState.BatteryMilliVolt;
}

// ドア確認
bool isDoorClosed()
{
  cbxState.isDoorClosed = DS_hdl->getState();
  return cbxState.isDoorClosed;
}

// 鍵確認
bool isKeyLocked()
{
  cbxState.isKeyLocked = KS_hdl->getState();
  return cbxState.isKeyLocked;
}

// 電源確認
bool isPowerAC()
{
  cbxState.isPowerAC = !PS_hdl->getState();
  return cbxState.isPowerAC;
}

// EQ確認
bool isEqOn()
{
  cbxState.isEqOn = !EQ_hdl->getState();
  return cbxState.isEqOn;
}

// DEEPSLEEPの開始
void startDeepsleep()
{
  isPowerAC();
  if (!ENABLE_DEEPSLEEP || cbxState.isPowerAC)
  {
    return; // DEEPSLEEPの許可が無いとき,電源がACに復帰しているときは実行しない
  }

  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_AUTO);
  usb_power(false); // AMT5102の電源OFF

  // LEDの消灯。この方法でやらないとSLEEP中に出力維持されない
  rtc_gpio_init(LED_DS);
  rtc_gpio_init(LED_KS);
  rtc_gpio_init(LED_COM);
  rtc_gpio_init(LED_PWR);

  rtc_gpio_set_direction(LED_DS, RTC_GPIO_MODE_OUTPUT_ONLY);
  rtc_gpio_set_direction(LED_KS, RTC_GPIO_MODE_OUTPUT_ONLY);
  rtc_gpio_set_direction(LED_COM, RTC_GPIO_MODE_OUTPUT_ONLY);
  rtc_gpio_set_direction(LED_PWR, RTC_GPIO_MODE_OUTPUT_ONLY);

  rtc_gpio_set_level(LED_DS, 1);  // HIGHに設定
  rtc_gpio_set_level(LED_KS, 1);  // HIGHに設定
  rtc_gpio_set_level(LED_COM, 1); // HIGHに設定

  rtc_gpio_set_level(LED_PWR, 1); // HIGHに設定

  // pinMode(WAKE_UP_PIN, INPUT_PULLUP);
  rtc_gpio_pullup_en(WAKE_UP_PIN); // この方法でPIN設定しないとうまく動かないので注意（内部PULL-UPの維持）
  rtc_gpio_pulldown_dis(WAKE_UP_PIN);
  esp_sleep_enable_ext0_wakeup(WAKE_UP_PIN, 0); // AC/BTがLOW（ACモード）でWAKEUP
  cbx3_log(LOG_INF, "ENTERING DEEP-SLEEP MODE");
  cbx_wait(100); // PINの安定待ち
  esp_deep_sleep_start();
}

void cbx_restart() // リセット用
{
  cbx3_log(LOG_INF, "RESTART COCOBOX(AFTER 30s)");
  SendDataLogMsg("RESTART COCOBOX (AFTER 30s)");
  oLED_Pwr->setMode(LEDMode::BLINK_FAST);
  cbx_wait(5 * 1000); // 5sec
  SendDataATCom("AT@HWRESET");
  cbx_wait(25 * 1000); // 55sec
  cbx3_log(LOG_INF, "USB POWER OFF");
  usb_power(false); // AMT5102の電源OFF(ROOT HUB ERROR対策)
  cbx_wait(1000);
  ESP.restart();
}

String getIDFVer()
{
  String versionString = ESP.getSdkVersion();
  int vIndex = versionString.indexOf('v');
  if (vIndex != -1)
  {
    return versionString.substring(vIndex + 1, versionString.indexOf('-', vIndex));
  }
  else
  {
    return "Version not found";
  }
}

// ちょっとしたテスト用
void testseq()
{
  // for debug
  int pin = LED_KS;
  pinMode(pin, OUTPUT);
  while (true)
  {
    cbx3_log(LOG_INF, "HIGH");
    digitalWrite(pin, true);
    delay(1000);
    cbx3_log(LOG_INF, "LOW");
    digitalWrite(pin, false);
    delay(1000);
  }
}

// SetUp////////////////////////////////////////////////////////////////////////////////
void setup()
{
  Serial.begin(115200);
  cbx_wait(1000); // シリアル通信の安定待ち。これがないと再起動時にログの最初のほうがでてこない
  cbx3_log(LOG_INF, "///////////STARTING COCOBOX3/////////////////////");
  cbx3_log(LOG_INF, "FW Ver : %s", FW_VER);
  cbxState.IDFVer = getIDFVer();
  cbx3_log(LOG_INF, "ESP-IDF Ver : %s", cbxState.IDFVer.c_str());

  // testseq(); // for debug

  // 起動理由を取得
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

  // 起動理由のログ出力
  if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0)
  {
    cbx3_log(LOG_INF, "[ST0]>>Device restarted due to AC-POWER ON Wake-up.");
    cbx_restart(); // WAKE直後の起動では何故か通信がうまくいかないのでもう一回リスタートさせる
  }
  else
  {
    cbx3_log(LOG_INF, "[ST0]->>Normal boot start.");
  }

  // Config.jasonのロード
  cbx3_log(LOG_INF, "[ST0]->>LOADING [config.json]");

  if (!SPIFFS.begin(true))
  {
    cbx3_log(LOG_ERR, "[ST0]->>An error has occurred while mounting SPIFFS");
    stop();
  }
  else
  {
    cbx3_log(LOG_INF, "[ST0]->>SPIFS BEGIN");
  }

  // spifs.remove(); // test用でconfig.jasonの削除したいときのみ使用

  if (!spifs.load())
  {
    cbx3_log(LOG_WAR, "[ST0]->>Failed to load config, using default values");
    if (spifs.save())
    {
      cbx3_log(LOG_INF, "[ST0]->>Save default config.");
    }
    else
    {
      cbx3_log(LOG_ERR, "[ST0]->>Failed to save config.");
    }
  }
  else
  {
    if (spifs.autolockDelay == 0)
    {
      cbxState.isAutoLockEnable = false; // AUTOLOCKの禁止
    }
    else
    {
      cbxState.isAutoLockEnable = true; // AUTOLOCKの許可
    }
    cbx3_log(LOG_INF, "[ST0]->>LOAD [config.json] SUCCESS.");
    spifs.printJson();
  }

  // LEDオブジェクトの開始
  oLED_DS = new LEDCont(LED_DS);
  oLED_KS = new LEDCont(LED_KS);
  oLED_Com = new LEDCont(LED_COM);
  oLED_Pwr = new LEDCont(LED_PWR);
  oLED_Pwr->setMode(LEDMode::ON);

  // スイッチ類稼働
  cbx3_log(LOG_INF, "[ST0]>>INITIALIZE INPUT SIGNAL");
  cbx3_log(LOG_INF, "[ST0]->>DOOR SW");
  DS_hdl = new SwitchEventHandler(DOOR_SW);
  DS_hdl->setCallback(DSCallback);
  cbx_wait(100);
  DS_hdl->begin();

  cbx3_log(LOG_INF, "[ST0]->>KEY SW");
  KS_hdl = new SwitchEventHandler(KEY_SW);
  KS_hdl->setCallback(KSCallback);
  cbx_wait(100);
  KS_hdl->begin();

  cbx3_log(LOG_INF, "[ST0]->>AC/BT");
  PS_hdl = new SwitchEventHandler(ACBT);
  PS_hdl->setCallback(PSCallback);
  cbx_wait(100);
  PS_hdl->begin();

  // cbx3_log(LOG_INF, "[ST0]->>POWER-GOOD");
  // PGD_hdl = new SwitchEventHandler(POWERGOOD);
  // PGD_hdl->setCallback(PGDCallback);
  // cbx_wait(100);
  // PGD_hdl->begin();

  cbx3_log(LOG_INF, "[ST0]->>EQ");
  EQ_hdl = new SwitchEventHandler(EQ);
  EQ_hdl->setCallback(EQCallback);
  cbx_wait(100);
  EQ_hdl->begin();

  // 入力信号のチェック
  cbx3_log(LOG_INF, "[ST0]>>CHECKING INPUT STATUS");
  isDoorClosed();
  isKeyLocked();
  isPowerAC();
  isEqOn();

  // 入力信号のチェック-各ステータスの取得と表示
  cbx3_log(LOG_INF, "[ST0]->>Door Status  : %s", cbxState.isDoorClosed ? "CLOSE" : "OPEN");
  cbx3_log(LOG_INF, "[ST0]->>Key Status   : %s", cbxState.isKeyLocked ? "LOCK" : "UNLOCK");
  cbx3_log(LOG_INF, "[ST0]->>Power Status : %s", cbxState.isPowerAC ? "AC" : "BT");
  cbx3_log(LOG_INF, "[ST0]->>EQ Status    : %s", cbxState.isEqOn ? "EQ-ON" : "EQ-OFF");

  // バッテリー読み取り
  cbx3_log(LOG_INF, "[ST0]>>INITIALIZE BATTERY CHECKER");
  if (VBAT_ENABLE)
  {
    oBatChecker = new BatChecker(VBT_EN, VBT, spifs.vth);
    oBatChecker->VrefCalib = spifs.vcal;
    cbxState.BatteryMilliVolt = readBatteryMilliVolt();
    cbx3_log(LOG_INF, "[ST0]->>BATTERY VOLTAGE = %1.2f[V]", (float)cbxState.BatteryMilliVolt / 1000);
  }

  // Battery電圧チェック（BTモード時のみ） ※起動時にバッテリーLOWだった場合は即DEEPSLEEPする
  // if (!cbxState.isPowerAC && cbxState.BatteryMilliVolt < spifs.vth)
  // {
  //   cbx3_log(LOG_INF, "[ST0]->>ENTERING DEEP-SLEEP (Battery low at startup.)");
  //   startDeepsleep(); // メッセージは送らずにDEEPSLEEP直行する
  // }
  // else
  // {
  //   // NOTHING
  // }

  // サーボモーター初期化
  cbx3_log(LOG_INF, "[ST0]>>INITIALIZE SERVO MOTOR");
  oLockSystem = new LockSystem(SV_DATA, DOOR_INTLK_ENABLE, SV_INVERT_MODE);
  cbx_wait(1000);
  if (SV_DEMO_ENABLE) // サーボデモモード
  {
    cbx3_log(LOG_INF, "[ST0]->>SV DEMO START");
    oLockSystem->KGLock(SET_SV_LK, true);
    cbx_wait(500);
    oLockSystem->KGLock(SET_SV_UL, true);
    cbx3_log(LOG_INF, "[ST0]->>SV DEMO END");
  }
  else
  {
    cbx3_log(LOG_INF, "[ST0]->>Servo Motor SET TO NEUTRAL POSITION");
    oLockSystem->KGNtral(); // サーボを0度位置に動かす
    cbx3_log(LOG_INF, "[ST0]->>...DONE.");
    // I2Cの開始
    if (I2C_start(I2C_SDA, I2C_SCL))
    {
      cbx3_log(LOG_INF, "[ST0]>>I2C Started");
    }
    else
    {
      cbx3_log(LOG_ERR, "[ST0]->>Failed to start I2C");
    }

    // I2C初期化
    cbx3_log(LOG_INF, "[ST0]>>CHECKING I2C DEVICE(s)");
    if (I2C_checkDeviceExists(I2C_KEYUNIT_ADDR))
    {
      cbx3_log(LOG_INF, "[ST0]->>KEY STORAGE UNIT EXIST");
    }
    else
    {
      cbx3_log(LOG_WAR, "[ST0]->>KEY STORAGE UNIT NOT EXIST");
    }
  }

  // StartUpタスク開始
  xTaskCreateUniversal(StartUpMainTask, "StartUpMainTask", 4096, NULL, StartUpMain_PRIORITY, &StartUpMainHdl, APP_CPU_NUM);
  cbx_wait(100); // 単なるログ表示バグ対策
  // AMT5102接続開始
  xTaskCreateUniversal(StartUpAMT5102Task, "StartUpAMT5102Task", 4096, NULL, StartUpAMT5102_PRIORITY, &StartUpAAMT5102Hdl, APP_CPU_NUM);
}

// loop////////////////////////////////////////////////////////////////////////////////
void loop()
{
  // nothing
  cbx_wait(10);
}

// テスト用
void stop()
{
  cbx3_log(LOG_WAR, "PROGRAM STOP");
  int counter = 0;
  while (1)
  {
    if (counter < 10)
    {
      cbx3_log(LOG_WAR, "PROGRAM STOP : %d", counter);
      cbx_wait(1000);
      counter++;
    }
  }
}
