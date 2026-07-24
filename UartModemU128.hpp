/*
M5Stack U128制御用クラス

    517Factory
*/

#pragma once

#include "BaseUartModem.hpp"
#include <ArduinoJson.h>
#include "CredentialOfNetwork.h"

enum class LteCarrier : int
{
  AUTO,
  DOCOMO,
  SOFTBANK,
  KDDI,
};

enum class SimCarrier : int
{
  DOCOMO,
  SOFTBANK,
  GLOBAL,
  KDDI,
  UNKNOWN,
};

// #define MQTT_QOS 1
// #define WAKEUP_TIMEOUT_MS 10000

const int DEFAULT_TIMEOUT = 10000;
const int COPS_TIMEOUT = 120000; // 最大120秒必要
const int MQTT_CONNECT_TIMEOUT = 30000;
const int MAX_NET_RETRY_COUNT = 3; // テスト用に3に設定（元は10）。テスト後に元に戻すこと。

// Modemステータス構造体
struct ModemStatus
{
  // volatile bool isMqttFirstConnect = true; // 初回接続確認フラグ
  volatile bool isU128RDY = false;
  volatile bool isFullFunction = false;
  volatile bool isSimState = false; // SIMステータス
  volatile bool isSMSready = false; // SMSの状態
  volatile int LteStatus = -1;      // LTEステータス（CEREGのアンサー）
  volatile MqttConnectType mqttConnectType = MqttConnectType::UNKNOWN;
  volatile bool commandInputMode = false;
  volatile int echoMode = -1;
  volatile bool echoDetected = false;                       // エコーバック検知フラグ
  volatile bool isQueryActive = false;                      // 問い合わせ実行中フラグ
  volatile MODEM_RESULT atResult = MODEM_RESULT::M_UNKNOWN; // ATコマンドの成功ステータス
  volatile int RSSIValue = 0;                               // 最後に取得したRSSI値
  volatile bool isSystemInfo = false;                       // System情報の取得フラグ
  volatile bool isPdpConnection = false;                    // LTE Cat-Mネットワークコネクション
  volatile int state = -1;                                  // なにかの情報を送るためのTag
  String deviceName = "";                                   // デバイス名
  String IPAddress = "";
  String IMSI = "";
  volatile SimCarrier simCarrier = SimCarrier::UNKNOWN;
  volatile LteCarrier carrier = LteCarrier::SOFTBANK;
  CpsiState_t cpsiState;
};

class UartModemU128 : public BaseUartModem
{
protected:
public:
  // コンストラクタ
  UartModemU128(HardwareSerial *uart, uint16_t sendQueueLength = 30);
  ModemStatus mState;
  String getSimCarrierString() const override;
  bool carrierSW = true;
  bool platinumBandOnlySW = false;

  // 純粋仮想メソッドの実装
  bool init() override;                                             // 初期化 (U128特有のATコマンドシーケンス)
  bool InitialModemSetup(void) override;                            // 設定確認
  bool sendFsMessage(const String &message) override;               // 旧FreeSpace電文に対応。メッセージをHEX変換後にJSON形式で送る
  bool connectMqttNetwork() override;                               // ネットワーク接続 (APN設定、アタッチなど)
  MODEM_RESULT chkMqtt() override;                                  // MQTTの接続チェック
  MODEM_RESULT resisterMqttSub() override;                          // MQTT SUB登録
  bool requestTimecode(void);
  char *chkSystemInformation(void) override;
  String getSimStateInformation(void) override;
  void setCarrierSW(bool sw) override;
  void setPlatinumBandSW(bool sw) override;
  bool setupCarrierBasedOnSim() override;
  bool isGlobalSim() const override;
  void ModemDisconnectTest(int flg) override;

  // デストラクタ
  ~UartModemU128() override
  {
  }

  const char *IMSI_PREFIX_DOCOMO = "44010";
  const char *IMSI_PREFIX_KDDI_1 = "4405";
  const char *IMSI_PREFIX_KDDI_2 = "4407";
  const char *IMSI_PREFIX_JP = "44";
  const char *IMSI_PREFIX_GLOBAL = "901";

private:
  bool chkLteConnection() override;
  bool chkPdpConnection() override;

  // 内部処理用（ロック済みで呼び出すことを想定）
  bool init_internal();
  bool InitialModemSetup_internal();
  bool connectMqttNetwork_internal();
  MODEM_RESULT resisterMqttSub_internal();
  MODEM_RESULT chkMqtt_internal();
  char *chkSystemInformation_internal();
  bool activatePdpConnection() override;
  bool deactivatePdpConnection() override;
  size_t receiveData(uint8_t *buffer, size_t max_len) override;     // データの受信
  CpsiState_t chkSignal() override;                                 // 信号の確認（RSSI）
  modemDataPacket processResponse(const String &response) override; // 応答した電文の処理

  String _apn = "u128.internet"; // 例としてAPNを定義

  // U128固有のユーティリティメソッド (必要に応じて)
  modemDataPacket decodeMqttSub(const String &subResponse);
  bool checkU128Version();
  bool checkSIM();
  bool chkWakeupState();                           // 起動時ステータスの確認
  static int decodeCSQ(const String &csqResponse); // CSQからRSSIの取得
  bool parseJsonPayload(const String &jsonString, modemDataPacket &packet);
  bool setMqttConfigration(void);
  bool sendCommandWithPrompt(const char *cmd, const char *prompt, int timeoutMs);
  MODEM_RESULT queryU128(const String &command, uint32_t timeoutMs);
  bool decodeAPPconnection(const String &Response);   // Cat-Mアクティベートの結果取得
  CpsiState_t decodeCPSI(const String &cpsiResponse); // CPSIから各種情報の取得
  bool decodeCNACT(const String &response);
  bool decodeSMSTATE(const String &response);
  bool decodeCEREG(const String &response);
  const char *getSystemModeString(CpsiSystemMode_t mode);
  const char *getOperationModeString(CpsiOperationMode_t mode);
  void printCPSI(const CpsiState_t &state);
  bool setCarrier(LteCarrier cr);
  SimCarrier chkSimCarrier(String IMSI);
};

//////////////
