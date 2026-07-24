/*
抽象モデムインターフェース


    517Factory

    -　以下のような実装
      IModem　---　Modem用抽象化クラス（インターフェース）
          ┗ BaseUartModem ---　Uartモデム用のインターフェース
            ┗ UartModemU128　---　U128専用実行コマンドクラス
          ┗ WiFiModem ---　WiFi接続用のインターフェース（未実装）
*/

#pragma once

// ATコマンドアクセス時の共通戻り値定義
enum class MODEM_RESULT : int
{
  // 成功状態
  M_OK = 0,      // コマンド実行成功、モデムから "OK" を受信
                 // M_PROMPT,       AT+SMPUBのように、コマンド送信後の ">" プロンプトを受信 (特殊な成功)
  M_RECONNECTED, // 再接続処理成功

  // 失敗状態（エラータイプ別）
  M_ERROR,     // コマンド実行失敗、モデムから "ERROR" または "+CME ERROR" などのエラー応答を受信
  M_TIMEOUT,   // モデムからの応答（OK/ERRORなど）を指定時間内に受信できなかった
  M_SEND_FAIL, // コマンドまたはデータペイロードをUARTに送信できなかった（ハードウェア/キュー障害など）
               // M_STATE_ERROR,    // モデムの状態が不正なため、コマンドを実行できなかった（例: 接続されていないのにPUBLISHを試みた）
               // M_INVALID_PARAM, // コマンドの引数などが不正

  // 接続確認用
  M_LTE_DISCONNECTED,  // LTE未接続(CEREG)
  M_MODEM_OFFLINE,     // MODEMオフライン（CPSI）
  M_MODEM_SIGNAL_FAIL, // SIGNAL異常(RSRP)
  M_MQTT_DISCONNECTED, // MQTT未接続

  // その他
  M_UNKNOWN // 未確定時に使用
};

enum class ModemType
{
  UNKNOWN = 0,
  UART,
  WIFI
  // USB
};

// データサイズは後で調整
#define MAX_TOPIC_LEN 100
#define MAX_IMSI_LEN 20
#define MAX_TYPE_LEN 10
#define MAX_MESSAGE_LEN 256
#define MAX_CCLK_LEN 25
#define MAX_UT_LEN 12
#define MAX_UART_READ_SIZE 256

// モデム動作設定・デフォルト値
#define MODEM_RECHECK_DELAY_MS_DEFAULT 60000
#define MODEM_SEND_QUEUE_SIZE_DEFAULT 30
#define MODEM_RAW_RECEIVE_QUEUE_SIZE_DEFAULT 10

// 汎用ログレベル定義
enum class ModemLogLevel {
    ERR,
    WAR,
    INF,
    DBG,
    MDBG1,
    MDBG2,
    MDBG3
};

enum class MqttConnectType
{
  UNKNOWN = -1,
  DISCONNECTED = 0,
  CONNECTED = 1,
  CONNECTED_SP = 2
};

struct modemDataPacket
{
  bool requiresExecution = false;
  char topic[MAX_TOPIC_LEN] = {0};
  char imsi[MAX_IMSI_LEN] = {0};
  char type[MAX_TYPE_LEN] = {0};
  char message[MAX_MESSAGE_LEN] = {0};
  char cclk[MAX_CCLK_LEN] = {0};
  char ut[MAX_UT_LEN] = {0};
  MqttConnectType mqttstate = MqttConnectType::UNKNOWN;
};

// CPSI用システムモード
typedef enum
{
  CPSI_MODE_NONE = 0,
  CPSI_MODE_LTM1,   // LTE CAT-M1
  CPSI_MODE_NBIOT,  // NB-IOT
  CPSI_MODE_GSM,    // GSM
  CPSI_MODE_UNKNOWN // その他のモード
} CpsiSystemMode_t;

// CPSI用オペレーションモード
typedef enum
{
  CPSI_OP_NONE = 0,
  CPSI_OP_ONLINE,   // Online
  CPSI_OP_LOWPOWER, // Low Power Mode
  CPSI_OP_UNKNOWN   // その他
} CpsiOperationMode_t;

// システム情報(AT+CPSI?の結果保存用)
typedef struct
{
  // システム情報 (EnumとIntで固定長化)
  CpsiSystemMode_t systemMode;       // Index 0 を enum に変換
  CpsiOperationMode_t operationMode; // Index 1 を enum に変換
  unsigned int mccMnc;               // 44010 など (Index 2 を int に変換)
  unsigned int tracingAreaCode;      // TAC (Index 3 を 16進数で int に変換)
  unsigned int servingCellId;        // SCellID (Index 4)
  unsigned int band;                 // バンド

  // 信号品質 (Int - 単位はdBmまたはIndex値)
  int rsrq;  // RSRQ (Index 10)
  int rsrp;  // RSRP (Index 11)
  int rssi;  // RSSI (Index 12)
  int rssnr; // RSSNR (Index 13)

  char rawLine[128]; // 全ステータスを1行にまとめた文字列

  // 値が有効かどうか
  bool isDataValid;
} CpsiState_t;

typedef struct // 生の受信データ用構造体
{
  char data[MAX_UART_READ_SIZE];
  size_t len;
} RawDataItem_t;

class IModem
{
public:
  virtual ~IModem() = default; // 仮想デストラクタ

  // MQTT接続機能
  virtual MODEM_RESULT chkMqtt() = 0;  // MQTT接続状態チェック
  // virtual bool connectMqtt() = 0;                        // MQTT再接続処理
  virtual bool sendFsMessage(const String &message) = 0; // データ送信

  // ネットワーク層の確認（共通で使う）
  virtual bool init() = 0;              // 初期化関数
  virtual bool checkNetwork() = 0;      // ネットワークの接続チェック
  virtual bool connectNetwork() = 0;    // ネットワークの接続
  virtual bool InitialModemSetup() = 0; // 初回のみ使う（モデムに設定を書き込む）
  virtual bool connectMqttNetwork() = 0;
  virtual char *chkSystemInformation(void) = 0;
  virtual String getSimStateInformation(void) = 0;
  virtual ModemType getType() const = 0;
  virtual RawDataItem_t readRawData() = 0;
  virtual bool requestTimecode() = 0;
  virtual QueueHandle_t getUrcQueueHandle() = 0;
  virtual void splitAndQueueMessage(const RawDataItem_t &item) = 0;
  virtual MODEM_RESULT resisterMqttSub() = 0;
  virtual void setCarrierSW(bool sw) { (void)sw; }
  virtual void setPlatinumBandSW(bool sw) { (void)sw; } // プラチナバンド固定スイッチ用
  virtual bool setupCarrierBasedOnSim() { return false; }
  virtual bool isGlobalSim() const { return false; }
  virtual String getSimCarrierString() const { return "UNKNOWN"; }

  // コールバックとタスク管理
  virtual void setDebugLogCallback(void (*callback)(ModemLogLevel level, const char* msg)) = 0;
  virtual void setMqttStateCallback(bool (*callback)(MqttConnectType)) = 0;
  virtual void setTxLedCallback(void (*callback)()) = 0;
  virtual void setRxLedCallback(void (*callback)()) = 0;
  virtual void setModemDataReceiveCallback(void (*callback)(modemDataPacket)) = 0;
  virtual void startMonitorTask(uint8_t priority, uint8_t cpu_core) = 0;
  virtual void startSendTask(uint8_t priority, uint8_t cpu_core) = 0;
  virtual void startReceiveTasks(uint8_t receive_priority, uint8_t raw_handler_priority, uint8_t urc_handler_priority, uint8_t cpu_core) = 0;

  // データ送信キューイング
  virtual bool enqueueSendMessage(const char *msg, uint32_t timeout_ms = 0) = 0;
  virtual uint16_t getSendQueueWaitingCount() = 0;
  virtual bool wasPdpResetPerformed() = 0;
  virtual void clearPdpResetFlag() = 0;
  virtual bool wasModemResetPerformed() = 0;
  virtual void clearModemResetFlag() = 0;

  // テスト用：切断状態のシミュレーション
  virtual void ModemDisconnectTest(int flg) = 0;

  // セマフォ（排他制御）
  virtual bool lockModem(uint32_t ticksToWait = 0xFFFFFFFF) = 0; // portMAX_DELAYの代替として0xFFFFFFFF
  virtual void unlockModem() = 0;
};
