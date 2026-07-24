#pragma once

/*
UARTモデムの共通基盤クラス

子クラスはこれを継承し、具体的なATコマンド群を定義する
UART通信に関連する汎用的な処理（送受信、応答待機など）を実装

    517Factory
*/

#include <Arduino.h>
#include "IModem.hpp"

class BaseUartModem : public IModem
{
protected:
  // メンバー変数
  HardwareSerial *_uart;
  bool _isInitialized = false; // 初期化されたかどうかのフラグ
  bool isLteConnected = false;
  bool isPdpConnected = false;
  bool wasPdpReset = false;
  bool wasModemReset = false;
  uint32_t connectionFailureCount = 0; // 連続接続失敗回数
  bool useFallbackBand = false;        // フォールバック用バンド設定を使用中かどうか

  // 共通通信プライベートメソッド (BaseUartModem.cppで実装)
  // bool sendCommand(const String &cmd, const String &expected_resp, uint32_t timeout_ms = 0);
  void sendAtCommand(const String &cmd);

protected:
  QueueHandle_t _resQueue = nullptr; // コマンド応答キュー
  QueueHandle_t _urcQueue = nullptr; // 非同期イベントキュー
  QueueHandle_t _sendQueue = nullptr; // 送信用キュー
  QueueHandle_t _rawReceiveQueue = nullptr; // 生データ受信用キュー
  SemaphoreHandle_t _modemSemaphore = nullptr; // モデム排他制御用セマフォ
  String _pendingPartialLine = ""; // 受信バッファ分割時の不完全行の保留用バッファ

  TaskHandle_t _modemMonitorTask_hdl = nullptr;
  TaskHandle_t _sendTask_hdl = nullptr;
  TaskHandle_t _receiveTask_hdl = nullptr;
  TaskHandle_t _rawDataHandlerTask_hdl = nullptr;
  TaskHandle_t _urcHandlerTask_hdl = nullptr;
  uint8_t _monitorPriority = 8;
  uint8_t _sendPriority = 10;
  uint8_t _receivePriority = 14;
  uint8_t _handlerPriority = 13;
  uint8_t _urcHandlerPriority = 13;
  bool (*_mqttStateCallback)(MqttConnectType) = nullptr;
  void (*_txLedCallback)() = nullptr;
  void (*_rxLedCallback)() = nullptr;
  void (*_modemDataReceiveCallback)(modemDataPacket) = nullptr;
  void (*_debugLogCallback)(ModemLogLevel level, const char* msg) = nullptr;

  void modemLog(ModemLogLevel level, const char* format, ...);
  void setString2Char(char *dest, const String &src, size_t destSize);

  static void modemMonitorTaskWrapper(void *pvParameters);
  static void sendTaskWrapper(void *pvParameters);
  static void receiveTaskWrapper(void *pvParameters);
  static void rawDataHandlerTaskWrapper(void *pvParameters);
  static void urcHandlerTaskWrapper(void *pvParameters);

public:
  BaseUartModem(HardwareSerial *uart, uint16_t sendQueueLength = MODEM_SEND_QUEUE_SIZE_DEFAULT, uint16_t rawReceiveQueueLength = MODEM_RAW_RECEIVE_QUEUE_SIZE_DEFAULT); // コンストラクタ
  virtual ~BaseUartModem() {}                                      // 仮想デストラクタ

  // 排他制御
  bool lockModem(uint32_t ticksToWait = 0xFFFFFFFF) override;
  void unlockModem() override;

  // コールバックとタスク管理
  void setDebugLogCallback(void (*callback)(ModemLogLevel level, const char* msg)) override { _debugLogCallback = callback; }
  void setMqttStateCallback(bool (*callback)(MqttConnectType)) override { _mqttStateCallback = callback; }
  void setTxLedCallback(void (*callback)()) override { _txLedCallback = callback; }
  void setRxLedCallback(void (*callback)()) override { _rxLedCallback = callback; }
  void setModemDataReceiveCallback(void (*callback)(modemDataPacket)) override { _modemDataReceiveCallback = callback; }
  void startMonitorTask(uint8_t priority, uint8_t cpu_core) override;
  void startSendTask(uint8_t priority, uint8_t cpu_core) override;
  void startReceiveTasks(uint8_t receive_priority, uint8_t raw_handler_priority, uint8_t urc_handler_priority, uint8_t cpu_core) override;

  // データ送信キューイング
  bool enqueueSendMessage(const char *msg, uint32_t timeout_ms = 0) override;
  uint16_t getSendQueueWaitingCount() override;
  bool wasPdpResetPerformed() override;
  void clearPdpResetFlag() override;
  bool wasModemResetPerformed() override;
  void clearModemResetFlag() override;
  void ModemDisconnectTest(int flg) override = 0;

  RawDataItem_t readRawData();                                     // 生データの読み込み
  void splitAndQueueMessage(const RawDataItem_t &item);            // 電文を分轄してQueueに送る
  ModemType getType() const override { return ModemType::UART; }   // IModem のオーバーライド (共通実装)
  QueueHandle_t getUrcQueueHandle() override { return _urcQueue; } // Queueハンドルを渡す

  // 以下はコマンド体系に依存するため、純粋仮想メソッドとして子クラスでの実装を強制
  bool init() override = 0;
  bool InitialModemSetup() override = 0; // Modemの初期設定をしてセーブする（初回起動時のみ使うもの）
  bool connectMqttNetwork() override = 0;
  char *chkSystemInformation(void) override = 0;
  String getSimStateInformation(void) override = 0;
  virtual MODEM_RESULT chkMqtt() = 0;
  // virtual bool connectMqtt() = 0;
  bool checkNetwork();
  bool connectNetwork();

protected:
  String cleanSegment(const String &response);                     // デリミタなどの処理
  virtual bool chkLteConnection() = 0;
  virtual bool chkPdpConnection() = 0;
  virtual bool activatePdpConnection() = 0;
  virtual bool deactivatePdpConnection() = 0;
  virtual size_t receiveData(uint8_t *buffer, size_t max_len) = 0;
  virtual CpsiState_t chkSignal() = 0;
  virtual modemDataPacket processResponse(const String &response) = 0;
};
