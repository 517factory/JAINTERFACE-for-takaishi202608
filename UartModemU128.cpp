/*
M5Stack U128制御用クラス

517Factory
*/
#include "UartModemU128.hpp"
#include "ConfigManager.hpp"

extern ConfigManager *CocoboxConfigManager;

// UartModemU128 のコンストラクタ定義
UartModemU128::UartModemU128(HardwareSerial *uart, uint16_t sendQueueLength) : BaseUartModem(uart, sendQueueLength)
{
    // 子クラス固有の初期化処理があればここに追加
}

char *UartModemU128::chkSystemInformation(void)
{
    if (!lockModem(portMAX_DELAY)) return (char*)"";
    char *result = chkSystemInformation_internal();
    unlockModem();
    return result;
}

bool UartModemU128::init()
{
    if (!lockModem(portMAX_DELAY)) return false;
    bool success = init_internal();
    unlockModem();
    return success;
}

// 初期化シーケンス
bool UartModemU128::InitialModemSetup(void)
{
    if (!lockModem(portMAX_DELAY)) return false;
    bool success = InitialModemSetup_internal();
    unlockModem();
    return success;
}

bool UartModemU128::connectMqttNetwork()
{
    if (!lockModem(portMAX_DELAY)) return false;
    bool success = connectMqttNetwork_internal();
    unlockModem();
    return success;
}

MODEM_RESULT UartModemU128::resisterMqttSub()
{
    if (!lockModem(portMAX_DELAY)) return MODEM_RESULT::M_SEND_FAIL;
    MODEM_RESULT result = resisterMqttSub_internal();
    unlockModem();
    return result;
}

MODEM_RESULT UartModemU128::chkMqtt()
{
    if (!lockModem(portMAX_DELAY)) return MODEM_RESULT::M_SEND_FAIL;
    MODEM_RESULT result = chkMqtt_internal();
    unlockModem();
    return result;
}

bool UartModemU128::init_internal()
{ 
    modemLog(ModemLogLevel::INF, "U128 : Checking U128 POWER-ON State.");

    // 電源ON時に自動的に送られてくるメッセージの確認
    if (!chkWakeupState())
    {
        modemLog(ModemLogLevel::ERR, "U128 : Could not receive U128 power-on message.");
        return false;
    }

    // 再起動検知用のフラグをここでクリアして、再帰呼び出しを防ぎつつ次の再起動に備える
    mState.isU128RDY = false;
    mState.isFullFunction = false;
    mState.isSimState = false;
    mState.isSMSready = false;

    // ここから初期設定
    modemLog(ModemLogLevel::INF, "U128 : Starting Connect Sequence (LTE Cat-M1 Network.)");

    // アンサーバックの禁止 ※ATE0はNO_SAVEなので毎回やる必要あり
    modemLog(ModemLogLevel::INF, "U128 : Disable AnswerBack.)");
    if (queryU128("ATE0", DEFAULT_TIMEOUT) != MODEM_RESULT::M_OK)
    {
        modemLog(ModemLogLevel::ERR, "U128 : Failed to disable modem echo (ATE0).");
        return false;
    }

    //  ERROR REPORT 詳細モード
    modemLog(ModemLogLevel::INF, "U128 : SET REPORT TYPE(CMEE)"); // SAVEされないのでやるなら毎回
    if (queryU128("AT+CMEE=2", DEFAULT_TIMEOUT) != MODEM_RESULT::M_OK)
    {
        modemLog(ModemLogLevel::ERR, "U128 : Failed to set CMEE.");
        return false;
    }

    modemLog(ModemLogLevel::INF, "U128 : CHECK SIM.)"); // SIMのチェック
    if (checkSIM())
    {
        modemLog(ModemLogLevel::INF, "U128 : SIM OK.");
    }
    else
    {
        modemLog(ModemLogLevel::ERR, "U128 : SIM not installed.");
        return false;
    }

    // IMSI取得　※送受信に使うので必須。
    modemLog(ModemLogLevel::INF, "U128 : Receive IMSI Code.)");
    if (queryU128("AT+CIMI", DEFAULT_TIMEOUT) != MODEM_RESULT::M_OK)
    {
        modemLog(ModemLogLevel::ERR, "U128 : Failed to get IMSI.");
        return false;
    }

    // IMSIからSimタイプをチェック。結果により次のキャリア設定を変更する
    modemLog(ModemLogLevel::INF, "U128 : Checking sim Type.)");
    mState.simCarrier = chkSimCarrier(mState.IMSI);
    if (mState.simCarrier == SimCarrier::UNKNOWN)
    {
        modemLog(ModemLogLevel::ERR, "U128 : Unknown Sim Carrier.");
        return false;
    }

    // キャリアの設定
    if (!setupCarrierBasedOnSim())
    {
        return false;
    }

    //  CEREGのモード設定
    modemLog(ModemLogLevel::INF, "U128 : SET CEREG MODE"); // SAVEされないのでやるなら毎回

    // if (queryU128("AT+CEREG=2", DEFAULT_TIMEOUT) != MODEM_RESULT::M_OK)    //詳細モード、URCあり
    // if (queryU128("AT+CEREG=1", DEFAULT_TIMEOUT) != MODEM_RESULT::M_OK)     // 簡易モード、URCあり
    if (queryU128("AT+CEREG=1", DEFAULT_TIMEOUT) != MODEM_RESULT::M_OK) // 簡易モード、URCなし
    {
        modemLog(ModemLogLevel::ERR, "U128 : Failed to set CEREG.");
        return false;
    }

    // 接続確認
    int counter = 0;
    int maxTimeoutSec = 40;
    int currentSimMode = 0;
    if (CocoboxConfigManager != nullptr)
    {
        currentSimMode = CocoboxConfigManager->getValueByAccessKey("SIMMODE");
    }
    if (currentSimMode == 3)
    {
        maxTimeoutSec = 120; // 完全自動モード(SIMMODE=3)時は全キャリアスキャン用に最大120秒待機
    }

    while (!chkLteConnection())
    {
        modemLog(ModemLogLevel::INF, "U128 : Check LTE Connection...");
        vTaskDelay(pdMS_TO_TICKS(1000));
        counter++;

        if (counter > maxTimeoutSec)
        {
            modemLog(ModemLogLevel::ERR, "U128 : LTE Connection TIMEOUT.(CEREG STATE)");
            return false;
        }
    }
    modemLog(ModemLogLevel::INF, "U128 : LTE Connected.");

    counter = 0;
    while (true)
    {
        modemLog(ModemLogLevel::INF, "U128 : Trying PDP Connection...");
        modemLog(ModemLogLevel::MDBG3, "U128 : ACTIVATE Slot:0"); // SAVEされないのでやるなら毎回
        activatePdpConnection();                   // Slot:0のアクティブ化 TODO ERR CHK

        if (mState.isPdpConnection)
        {
            modemLog(ModemLogLevel::INF, "U128 : PDP connection has been established.");
            this->isPdpConnected = true;
            break;
        }

        if (counter > 40)
        {
            modemLog(ModemLogLevel::ERR, "U128 : PDP connection Timeout.");
            this->isPdpConnected = false;
            return false; // とりあえず起動失敗としてFalseを返しておく。あまりここには来ないと思われる　（TODO）
        }
        modemLog(ModemLogLevel::WAR, "U128 : LTE connection fail. RETRY...");
        vTaskDelay(pdMS_TO_TICKS(1000));
        counter++;
    }

    // 信号の確認（TEST)
    printCPSI(chkSignal());

    // 接続確認（同時にIPアドレス取得）　※これもなくてもよい
    modemLog(ModemLogLevel::INF, "U128 : Checking IP-Address.)");
    if (queryU128("AT+CNACT?", DEFAULT_TIMEOUT) != MODEM_RESULT::M_OK)
    {
        modemLog(ModemLogLevel::ERR, "U128 : Failed to check IP-Address.");
        return false;
    }

    if (mState.IMSI == "")
    {
        // コマンドはOKだったが、IMSIの応答（URC）がprocessResponseで捕捉できなかった
        modemLog(ModemLogLevel::ERR, "U128 : Failed to get IMSI. mState.IMSI is empty.");
        return false;
    }

    // MQTTパラメータの設定
    modemLog(ModemLogLevel::INF, "U128 : SETUP MQTT CONFIGRATION.");
    if (!setMqttConfigration())
    {
        modemLog(ModemLogLevel::ERR, "U128 : FAIL TO SETUP MQTT CONFIGRATION.");
        return false;
    }

    // ここからMQTTの接続
    // 事前に既存のMQTTセッションがあれば切断し、ソケット状態をリセットする
    queryU128("AT+SMDISC", DEFAULT_TIMEOUT);
    vTaskDelay(pdMS_TO_TICKS(500));

    // 1回、CLEANSS=1で接続し、セッションをクリアする
    // そのあとCLEANSS=0で接続しなおすことで、セッション継続を実現する
    modemLog(ModemLogLevel::INF, "U128 : TRYING MQTT CONNECTION.");
    modemLog(ModemLogLevel::INF, "U128 : Phase 1: Setting CLEANSS=1 and connecting.");
    if (queryU128("AT+SMCONF=\"CLEANSS\",1", DEFAULT_TIMEOUT) != MODEM_RESULT::M_OK)
    {
        modemLog(ModemLogLevel::ERR, "U128 : Phase 1: Failed to set CLEANSS=1.");
        return false;
    }

    if (!connectMqttNetwork())
    {
        modemLog(ModemLogLevel::ERR, "U128 : Phase 1: Failed to connect with CLEANSS=1. Aborting.");
        return false;
    }

    // 一度MQTTを切断する
    modemLog(ModemLogLevel::INF, "U128 : Phase 2: Disconnecting current session (SMDISC).");
    queryU128("AT+SMDISC", DEFAULT_TIMEOUT);
    vTaskDelay(pdMS_TO_TICKS(500)); // 【安定性のための推奨】SMDISC後の待ち時間を追加

    // CLEANSS=0 に設定を変更 (次回接続で適用される)
    modemLog(ModemLogLevel::INF, "U128 : Phase 3: Setting CLEANSS=0 for next session.");
    if (queryU128("AT+SMCONF=\"CLEANSS\",0", DEFAULT_TIMEOUT) != MODEM_RESULT::M_OK)
    {
        modemLog(ModemLogLevel::ERR, "U128 : Phase 2: Failed to set CLEANSS=0. Setup FAILED.");
        return false;
    }

    // CLEANSS=0 を適用するための再接続
    modemLog(ModemLogLevel::INF, "U128 : Phase 4: Reconnecting with new CLEANSS=0 setting.");
    if (!connectMqttNetwork())
    {
        modemLog(ModemLogLevel::ERR, "U128 : Phase 2: Failed to reconnect with CLEANSS=0. Setup FAILED.");
        return false;
    }

    // 確認用のSMCONF?(TEST)
    // queryU128("AT+SMCONF?", MQTT_CONNECT_TIMEOUT);

    // SUBSCRIBE 実行
    modemLog(ModemLogLevel::INF, "U128 : TRYING MQTT SUBSCRIBE.");
    if (resisterMqttSub() != MODEM_RESULT::M_OK) // 次回はCLEANSS=0で再試行される
    {
        modemLog(ModemLogLevel::ERR, "U128 : MQTT Subscribe FAILED. Final Disconnect.");
        queryU128("AT+SMDISC", DEFAULT_TIMEOUT);
        return false;
    }

    // TEST - RESET
    // if (queryU128("AT+IPR=0", DEFAULT_TIMEOUT) != MODEM_RESULT::M_OK)
    // {
    //     modemLog(ModemLogLevel::ERR, "U128 : XXXX");
    //     return false;
    // }

    modemLog(ModemLogLevel::INF, "U128 : MQTT Connection and Subscribe SUCCESS.");
    mState.mqttConnectType = MqttConnectType::CONNECTED;
    return true;
}

// MODEM初期設定
bool UartModemU128::InitialModemSetup_internal()
{ 
    modemLog(ModemLogLevel::INF, "U128 : STARTING INITIAL MODEM SETUP MODE.");

    const int MAX_WAIT = 30;
    int counter = 0;
    while (true)
    {
        if (counter > MAX_WAIT)
        {
            modemLog(ModemLogLevel::ERR, "U128 : MODEM WAKEUP ERROR.");
            return false;
        }

        if (queryU128("ATE", DEFAULT_TIMEOUT) == MODEM_RESULT::M_OK)
        {
            modemLog(ModemLogLevel::INF, "U128 : MODEM WAKEUP.");
            break;
        }
        else
        {
            modemLog(ModemLogLevel::INF, "U128 : WAIT MODEM WAKEUP....");
            counter++;
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    modemLog(ModemLogLevel::INF, "U128 : SET ECHO OFF.)");
    if (queryU128("ATE0", DEFAULT_TIMEOUT) != MODEM_RESULT::M_OK)
    {
        modemLog(ModemLogLevel::ERR, "U128 : Failed to set modem echo (ATE0).");
        return false;
    }

    modemLog(ModemLogLevel::INF, "U128 : SETTING BAUD-RATE.)"); // ボーレート手動設定（これをやらないと起動時のRDYが飛んでこない）
    if (queryU128("AT+IPR=115200", DEFAULT_TIMEOUT) != MODEM_RESULT::M_OK)
    {
        modemLog(ModemLogLevel::ERR, "U128 : Failed to set baud-rate.");
        return false;
    }

    modemLog(ModemLogLevel::INF, "U128 : SET ERROR REPORT MODE.)");
    if (queryU128("AT+CMEE=2", DEFAULT_TIMEOUT) != MODEM_RESULT::M_OK)
    {
        modemLog(ModemLogLevel::ERR, "U128 : Failed to set error report mode(AT+CMEE=2).");
        return false;
    }

    modemLog(ModemLogLevel::INF, "U128 : CHECK SIM.)"); // SIMのチェック
    if (checkSIM())
    {
        modemLog(ModemLogLevel::INF, "U128 : SIM OK.");
    }
    else
    {
        modemLog(ModemLogLevel::ERR, "U128 : SIM not installed.");
        return false;
    }

    // IMSIのチェック
    modemLog(ModemLogLevel::INF, "U128 : Check IMSI Code.)");
    if (queryU128("AT+CIMI", DEFAULT_TIMEOUT) != MODEM_RESULT::M_OK)
    {
        modemLog(ModemLogLevel::ERR, "U128 : Failed to get IMSI.");
        return false;
    }

    if (mState.IMSI == "")
    {
        // コマンドはOKだったが、IMSIの応答（URC）がprocessResponseで捕捉できなかった
        modemLog(ModemLogLevel::ERR, "U128 : Failed to get IMSI. mState.IMSI is empty.");
        return false;
    }

    modemLog(ModemLogLevel::INF, "U128 : SET CAT-M MODE.)");
    if (queryU128("AT+CMNB=1", DEFAULT_TIMEOUT) != MODEM_RESULT::M_OK)
    {
        modemLog(ModemLogLevel::ERR, "U128 : Failed to set CAT-M mode(AT+CMNB=1).");
        return false;
    }

    modemLog(ModemLogLevel::INF, "U128 : ENABLE TIMESTAMP MODE.)");
    if (queryU128("AT+CLTS=1", DEFAULT_TIMEOUT) != MODEM_RESULT::M_OK)
    {
        modemLog(ModemLogLevel::ERR, "U128 : Failed to set Local Timestamp mode(AT+CLTS=1).");
        return false;
    }

    modemLog(ModemLogLevel::INF, "U128 : DEFINE PDP CONTEXT.");
    // CNACT=0,1 (Slot 0) と CNACT=1,1 (Slot 1) の両方に対応するため CID 0 と CID 1 の両方に APN を設定
    queryU128("AT+CGDCONT=0,\"IP\",\"" GPRS_APN "\"", DEFAULT_TIMEOUT);
    if (queryU128("AT+CGDCONT=1,\"IP\",\"" GPRS_APN "\"", DEFAULT_TIMEOUT) != MODEM_RESULT::M_OK)
    {
        modemLog(ModemLogLevel::ERR, "U128 : Failed to set PDP context.(AT+CGDCONT).");
        return false;
    }

    modemLog(ModemLogLevel::INF, "U128 : SET AUTH.");
    // CID 0 と CID 1 の両方に認証情報を設定
    queryU128("AT+CGAUTH=0,1,\"" GPRS_USER "\",\"" GPRS_PASS "\"", DEFAULT_TIMEOUT);
    if (queryU128("AT+CGAUTH=1,1,\"" GPRS_USER "\",\"" GPRS_PASS "\"", DEFAULT_TIMEOUT) != MODEM_RESULT::M_OK)
    {
        modemLog(ModemLogLevel::ERR, "U128 : Failed to set Authentication for PDP.(AT+CGAUTH).");
        return false;
    }

    if (!setupCarrierBasedOnSim())
    {
        return false;
    }

    // キャリア設定(AT+COPS)後、LTEアタッチ(CEREG=1 or 5)が完了するまで待機する
    int setupLteCounter = 0;
    int setupMaxTimeoutSec = 40;
    int setupSimMode = 0;
    if (CocoboxConfigManager != nullptr)
    {
        setupSimMode = CocoboxConfigManager->getValueByAccessKey("SIMMODE");
    }
    if (setupSimMode == 3)
    {
        setupMaxTimeoutSec = 120; // 完全自動モード(SIMMODE=3)時は全キャリアスキャン用に最大120秒待機
    }

    while (!chkLteConnection())
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
        setupLteCounter++;
        if (setupLteCounter >= setupMaxTimeoutSec)
        {
            modemLog(ModemLogLevel::ERR, "U128 : LTE Connection TIMEOUT during Modem Setup.");
            return false;
        }
    }

    modemLog(ModemLogLevel::INF, "U128 : Activate PDP Connection.");
    activatePdpConnection();

    modemLog(ModemLogLevel::INF, "U128 : SAVING CONFIGURATION.");
    if (queryU128("AT+CSAS", DEFAULT_TIMEOUT) != MODEM_RESULT::M_OK)
    {
        modemLog(ModemLogLevel::ERR, "U128 : Failed to save configuration.(AT+CSAS).");
        return false;
    }

    return true;

    // TEST RESET
    if (queryU128("ATZ", DEFAULT_TIMEOUT) != MODEM_RESULT::M_OK)
    {
        modemLog(ModemLogLevel::ERR, "U128 : Failed to Reset.");
        return false;
    }
    if (queryU128("AT+IPR=0", DEFAULT_TIMEOUT) != MODEM_RESULT::M_OK)
    {
        modemLog(ModemLogLevel::ERR, "U128 : Failed to Reset.");
        return false;
    }

    return false;
}

bool UartModemU128::setupCarrierBasedOnSim()
{
    modemLog(ModemLogLevel::INF, "U128 : Set Carrier.");
    // 旧接続のMQTTセッションを事前に切断・クリーンアップ
    queryU128("AT+SMDISC", DEFAULT_TIMEOUT);

    // キャリア設定変更時は接続フラグをクリアし、フライング送信を防いで安全な順番で再接続させる
    this->isLteConnected = false;
    this->isPdpConnected = false;
    mState.isPdpConnection = false;
    mState.mqttConnectType = MqttConnectType::UNKNOWN;
    if (mState.simCarrier == SimCarrier::DOCOMO)
    {
        modemLog(ModemLogLevel::INF, "U128 : DOCOMO SIM DETECTED.");
        if (!setCarrier(LteCarrier::DOCOMO))
        {
            modemLog(ModemLogLevel::ERR, "U128 : Failed to Set Carrier.");
            return false;
        }
    }
    else if (mState.simCarrier == SimCarrier::SOFTBANK)
    {
        modemLog(ModemLogLevel::INF, "U128 : SOFTBANK SIM DETECTED.");
        if (!setCarrier(LteCarrier::SOFTBANK))
        {
            modemLog(ModemLogLevel::ERR, "U128 : Failed to Set Carrier.");
            return false;
        }
    }
    else if (mState.simCarrier == SimCarrier::KDDI)
    {
        modemLog(ModemLogLevel::INF, "U128 : KDDI SIM DETECTED.");
        if (!setCarrier(LteCarrier::KDDI))
        {
            modemLog(ModemLogLevel::ERR, "U128 : Failed to Set Carrier.");
            return false;
        }
    }
    else if (mState.simCarrier == SimCarrier::GLOBAL)
    {
        modemLog(ModemLogLevel::INF, "U128 : GLOBAL SIM DETECTED.");
        int simMode = 0;
        int simSel = 0;
        if (CocoboxConfigManager != nullptr)
        {
            simMode = CocoboxConfigManager->getValueByAccessKey("SIMMODE");
            simSel = CocoboxConfigManager->getValueByAccessKey("SIMSEL");
        }

        if (simMode == 3)
        {
            // 2.4 完全自動モード (SET_SIMMODE_3X) -> AT+COPS=0
            int bandSel = 0;
            if (CocoboxConfigManager != nullptr)
            {
                bandSel = CocoboxConfigManager->getValueByAccessKey("BANDSEL");
            }
            const char* bandCfgStr = (bandSel == 1) ? LTE_BAND_AUTO_PB_PRI : LTE_BAND_AUTO_MB_PRI;

            char cmd[96];
            snprintf(cmd, sizeof(cmd), "AT+CBANDCFG=\"CAT-M\",%s", bandCfgStr);
            queryU128(cmd, DEFAULT_TIMEOUT);

            modemLog(ModemLogLevel::INF, "U128 : Set Carrier [FULL-AUTO] (SIMMODE=3, AT+COPS=0)");
            if (queryU128("AT+COPS=0", 120000) != MODEM_RESULT::M_OK)
            {
                modemLog(ModemLogLevel::ERR, "U128 : Failed to Set Carrier [FULL-AUTO].");
                return false;
            }
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
        else if (simMode == 2)
        {
            // 2.2 設定一覧: 自動モード (SET_SIMMODE_2X)
            int bandSel = 0;
            if (CocoboxConfigManager != nullptr)
            {
                bandSel = CocoboxConfigManager->getValueByAccessKey("BANDSEL");
            }
            const char* bandCfgStr = (bandSel == 1) ? LTE_BAND_AUTO_PB_PRI : LTE_BAND_AUTO_MB_PRI;

            char cmd[96];
            snprintf(cmd, sizeof(cmd), "AT+CBANDCFG=\"CAT-M\",%s", bandCfgStr);
            queryU128(cmd, DEFAULT_TIMEOUT);

            if (simSel == 1)
            {
                modemLog(ModemLogLevel::INF, "U128 : Set Carrier [AUTO-SOFTBANK] (SIMMODE=2, SIMSEL=1)");
                if (queryU128("AT+COPS=4,2,\"44020\",7", 120000) != MODEM_RESULT::M_OK)
                {
                    modemLog(ModemLogLevel::ERR, "U128 : Failed to Set Carrier [AUTO-SOFTBANK].");
                    return false;
                }
            }
            else
            {
                modemLog(ModemLogLevel::INF, "U128 : Set Carrier [AUTO-DOCOMO] (SIMMODE=2, SIMSEL=0)");
                if (queryU128("AT+COPS=4,2,\"44010\",7", 120000) != MODEM_RESULT::M_OK)
                {
                    modemLog(ModemLogLevel::ERR, "U128 : Failed to Set Carrier [AUTO-DOCOMO].");
                    return false;
                }
            }
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
        else if (simMode == 1)
        {
            // 2.2 設定一覧: リモート固定モード (SET_SIMMODE_1X)
            if (simSel == 1)
            {
                modemLog(ModemLogLevel::INF, "U128 : Set Carrier [FIX-SOFTBANK] (SIMMODE=1, SIMSEL=1)");
                if (!setCarrier(LteCarrier::SOFTBANK))
                {
                    modemLog(ModemLogLevel::ERR, "U128 : Failed to Set Carrier [FIX-SOFTBANK].");
                    return false;
                }
            }
            else
            {
                modemLog(ModemLogLevel::INF, "U128 : Set Carrier [FIX-DOCOMO] (SIMMODE=1, SIMSEL=0)");
                if (!setCarrier(LteCarrier::DOCOMO))
                {
                    modemLog(ModemLogLevel::ERR, "U128 : Failed to Set Carrier [FIX-DOCOMO].");
                    return false;
                }
            }
        }
        else
        {
            // 2.2 設定一覧: 手動モード (SET_SIMMODE_0X) -> DIPスイッチに従う
#if ENABLE_DIP_SWITCH
            if (carrierSW)
            {
                // DIP2 = ON (1:上) -> SOFTBANK
                modemLog(ModemLogLevel::INF, "U128 : Set Carrier [MANUAL-SOFTBANK] (SIMMODE=0, DIPSW=ON)");
                if (!setCarrier(LteCarrier::SOFTBANK))
                {
                    modemLog(ModemLogLevel::ERR, "U128 : Failed to Set Carrier [MANUAL-SOFTBANK].");
                    return false;
                }
            }
            else
            {
                // DIP2 = OFF (0:下) -> DOCOMO
                modemLog(ModemLogLevel::INF, "U128 : Set Carrier [MANUAL-DOCOMO] (SIMMODE=0, DIPSW=OFF)");
                if (!setCarrier(LteCarrier::DOCOMO))
                {
                    modemLog(ModemLogLevel::ERR, "U128 : Failed to Set Carrier [MANUAL-DOCOMO].");
                    return false;
                }
            }
#else
            // DIPスイッチ非搭載時：MODE 0(SIMMODE=0)が読み込まれた場合はMODE 1 (FIXモード) にフォールバック
            modemLog(ModemLogLevel::WAR, "U128 : MODE 0 selected but DIP switch is disabled. Falling back to FIX mode (SIMSEL).");
            if (simSel == 1)
            {
                modemLog(ModemLogLevel::INF, "U128 : Set Carrier [FIX-SOFTBANK] (SIMMODE=0->1, SIMSEL=1)");
                if (!setCarrier(LteCarrier::SOFTBANK))
                {
                    modemLog(ModemLogLevel::ERR, "U128 : Failed to Set Carrier [FIX-SOFTBANK].");
                    return false;
                }
            }
            else
            {
                modemLog(ModemLogLevel::INF, "U128 : Set Carrier [FIX-DOCOMO] (SIMMODE=0->1, SIMSEL=0)");
                if (!setCarrier(LteCarrier::DOCOMO))
                {
                    modemLog(ModemLogLevel::ERR, "U128 : Failed to Set Carrier [FIX-DOCOMO].");
                    return false;
                }
            }
#endif
        }
    }
    else
    {
        modemLog(ModemLogLevel::ERR, "U128 : UNKNOWN SIM CARRIER.");
        // もしUNKNOWNでも処理を続けたい場合は true を返しますが、
        // 今回のフローでは false(セットアップ失敗) とするべきでしょう。
        return false;
    }

    return true;
}

bool UartModemU128::setCarrier(LteCarrier cr)
{
    MODEM_RESULT res0 = MODEM_RESULT::M_ERROR;
    MODEM_RESULT res1 = MODEM_RESULT::M_ERROR;
    MODEM_RESULT res2 = MODEM_RESULT::M_ERROR;
    char cmd[96];

    int bandSel = 0;
    if (CocoboxConfigManager != nullptr)
    {
        bandSel = CocoboxConfigManager->getValueByAccessKey("BANDSEL");
    }

    switch (cr)
    {
    case LteCarrier::AUTO:
        {
            const char* bandStr = (bandSel == 1) ? LTE_BAND_AUTO_PB_PRI : LTE_BAND_AUTO_MB_PRI;
            modemLog(ModemLogLevel::INF, "U128 : Set Carrier to AUTO (Preferred: DOCOMO, BandSel=%d)", bandSel);
            snprintf(cmd, sizeof(cmd), "AT+CBANDCFG=\"CAT-M\",%s", bandStr);
            res1 = queryU128(cmd, DEFAULT_TIMEOUT);
            res2 = queryU128("AT+COPS=4,2,\"44010\",7", 120000);
            if (res2 == MODEM_RESULT::M_OK)
            {
                modemLog(ModemLogLevel::INF, "U128 : Waiting 2000ms for network stabilization...");
                vTaskDelay(pdMS_TO_TICKS(2000));
            }
        }
        break;

    case LteCarrier::DOCOMO:
        {
            const char* bandStr = (bandSel == 1) ? LTE_BAND_DOCOMO_PB_PRI : LTE_BAND_DOCOMO_MB_PRI;
            modemLog(ModemLogLevel::INF, "U128 : Set Carrier to DOCOMO (BandSel=%d)", bandSel);
            snprintf(cmd, sizeof(cmd), "AT+CBANDCFG=\"CAT-M\",%s", bandStr);
            res1 = queryU128(cmd, DEFAULT_TIMEOUT);
            res2 = queryU128("AT+COPS=1,2,\"44010\",7", 120000);
        }
        break;

    case LteCarrier::SOFTBANK:
        {
            const char* bandStr = (bandSel == 1) ? LTE_BAND_SB_PB_PRI : LTE_BAND_SB_MB_PRI;
            modemLog(ModemLogLevel::INF, "U128 : Set Carrier to SOFTBANK (BandSel=%d)", bandSel);
            snprintf(cmd, sizeof(cmd), "AT+CBANDCFG=\"CAT-M\",%s", bandStr);
            res1 = queryU128(cmd, DEFAULT_TIMEOUT);
            res2 = queryU128("AT+COPS=1,2,\"44020\",7", 120000);
        }
        break;
    }

    bool success = (res1 == MODEM_RESULT::M_OK && res2 == MODEM_RESULT::M_OK);
    if (success)
    {
        modemLog(ModemLogLevel::INF, "U128 : Carrier switch completed. Waiting 3000ms for network stabilization...");
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
    return success;
}

bool UartModemU128::chkLteConnection()
{
    if (mState.isU128RDY)
    {
        modemLog(ModemLogLevel::WAR, "U128 : Modem reboot detected (isU128RDY is true). Re-initializing modem...");
        this->wasModemReset = true;
        if (this->init())
        {
            modemLog(ModemLogLevel::INF, "U128 : Modem re-initialization SUCCESS.");
            this->isLteConnected = true;
            return true;
        }
        else
        {
            modemLog(ModemLogLevel::ERR, "U128 : Modem re-initialization FAILED.");
            this->isLteConnected = false;
            return false;
        }
    }

    mState.LteStatus = -1; // 一度リセットする
    MODEM_RESULT result = queryU128("AT+CEREG?", DEFAULT_TIMEOUT);
    if (result != MODEM_RESULT::M_OK)
    {
        modemLog(ModemLogLevel::ERR, "U128 : CEREG REQUEST ERROR.[%d]", result);
        this->isLteConnected = false;
        return false;
    }

    // ここでmState.LteStatusは更新されているはず
    // modemLog(ModemLogLevel::MDBG2, "CEREG MODE:%d", mState.LteStatus);
    if (mState.LteStatus == 1 || mState.LteStatus == 5)
    {
        this->isLteConnected = true;
        return true;
    }
    else
    {
        modemLog(ModemLogLevel::WAR, "U128 : LTE NOT CONNECTED : MODE:%d", mState.LteStatus);
        if (mState.LteStatus == 0)
        {
            modemLog(ModemLogLevel::INF, "U128 : GPRS detached (status 0). Sending AT+CGATT=1 to re-attach...");
            queryU128("AT+CGATT=1", 10000);
        }
        this->isLteConnected = false;
        return false;
    }
}

// Slot0のアクティブ化
bool UartModemU128::activatePdpConnection()
{
    // 接続アクティブ化　※スロット0に設定が保存されている前提
    modemLog(ModemLogLevel::INF, "U128 : Set Slot:0 Active.");
    
    // AT+COPS直後などモデムのベアラー準備待ちのため最大3回リトライ
    bool actOk = false;
    for (int retry = 0; retry < 3; retry++)
    {
        if (queryU128("AT+CNACT=0,1", DEFAULT_TIMEOUT) == MODEM_RESULT::M_OK)
        {
            actOk = true;
            break;
        }
        modemLog(ModemLogLevel::WAR, "U128 : AT+CNACT=0,1 failed (attempt %d/3). Retrying in 1000ms...", retry + 1);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    if (!actOk)
    {
        modemLog(ModemLogLevel::ERR, "U128 : Failed to set Slot:0 after retries.");
        mState.isPdpConnection = false;
        return false;
    }

    // AT+CNACT=0,1 送信後、+APP PDP: 0,ACTIVE URC の受信または AT+CNACT? で IPアドレス取得を待機
    uint32_t waitStart = millis();
    const uint32_t pdpTimeoutMs = 15000; // 最大15秒待機
    while (millis() - waitStart < pdpTimeoutMs)
    {
        if (chkPdpConnection() && mState.isPdpConnection)
        {
            modemLog(ModemLogLevel::INF, "U128 : Slot:0 Activated Successfully.");
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    modemLog(ModemLogLevel::ERR, "U128 : Slot:0 Activation Timeout.");
    mState.isPdpConnection = false;
    return false;
}

// Slot0の非アクティブ化
bool UartModemU128::deactivatePdpConnection()
{
    this->wasPdpReset = true; // Mark that a PDP reset was attempted/performed
    modemLog(ModemLogLevel::INF, "U128 : Deactivating Slot:0.");
    if (queryU128("AT+CNACT=0,0", DEFAULT_TIMEOUT) != MODEM_RESULT::M_OK)
    {
        modemLog(ModemLogLevel::ERR, "U128 : Failed to deactivate Slot:0.");
        mState.isPdpConnection = false;
        this->isPdpConnected = false;
        return false;
    }
    mState.isPdpConnection = false;
    this->isPdpConnected = false;
    return true;
}

// MQTTネットワーク接続の実装
bool UartModemU128::connectMqttNetwork_internal()
{ 
    // PDP接続の事前状態確認
    if (!chkPdpConnection())
    {
        modemLog(ModemLogLevel::WAR, "connectMqttNetwork_internal: PDP not active. Retrying activation...");
        if (!activatePdpConnection())
        {
            modemLog(ModemLogLevel::ERR, "connectMqttNetwork_internal: PDP activation failed.");
            return false;
        }
    }

    // PDP確立直後の内部ソケットバッファ安定化のためのわずかな待ち時間
    vTaskDelay(pdMS_TO_TICKS(500));

    bool connOk = false;
    for (int retry = 0; retry < 3; retry++)
    {
        if (queryU128("AT+SMCONN", MQTT_CONNECT_TIMEOUT) == MODEM_RESULT::M_OK)
        {
            connOk = true;
            break;
        }
        modemLog(ModemLogLevel::WAR, "AT+SMCONN failed (attempt %d/3). Retrying in 1500ms...", retry + 1);
        vTaskDelay(pdMS_TO_TICKS(1500));
    }

    if (!connOk)
    {
        modemLog(ModemLogLevel::ERR, "Failed to connect MQTT Network after retries.");
        // MQTT失敗時にPDPが切れてしまっている場合のみ非アクティブ化を行う
        if (!chkPdpConnection())
        {
            modemLog(ModemLogLevel::WAR, "PDP connection lost after MQTT failure. Deactivating Slot:0.");
            deactivatePdpConnection();
        }
        return false;
    }

    mState.mqttConnectType = MqttConnectType::CONNECTED;
    vTaskDelay(pdMS_TO_TICKS(5000)); // 長めのディレイをいれておかないと、初回のSUBが時間がかかる？未確認（TODO）
    return true;
}

size_t UartModemU128::receiveData(uint8_t *buffer, size_t max_len)
{
    // ここに U128 固有のデータ受信処理の実装が入る
    return 0; // 受信バイト数を返す
}

// ----------------------------------------------------
// U128固有のユーティリティメソッドの実装
// ----------------------------------------------------
MODEM_RESULT UartModemU128::resisterMqttSub_internal()
{ 
    // MQTT接続直後のモデムソケットバッファ安定化待ち
    vTaskDelay(pdMS_TO_TICKS(1000));

    const char *sub_topic_prefix = "AT+SMSUB=\"BIoT/down/";
    const char *unsub_topic_prefix = "AT+SMUNSUB=\"BIoT/down/";

    String subscribe_command = String(sub_topic_prefix) + mState.IMSI + "\",1";
    String unsubscribe_command = String(unsub_topic_prefix) + mState.IMSI + "\"";

    for (int retry = 1; retry <= 3; retry++)
    {
        modemLog(ModemLogLevel::INF, "U128 : Attempting MQTT Subscribe (Attempt %d/3): %s", retry, subscribe_command.c_str());
        MODEM_RESULT subRes = queryU128(subscribe_command, 3000);
        if (subRes == MODEM_RESULT::M_OK)
        {
            modemLog(ModemLogLevel::INF, "U128 : MQTT Subscribe SUCCESS.");
            return MODEM_RESULT::M_OK;
        }

        modemLog(ModemLogLevel::WAR, "Failed to Set MQTT Subscriber (Attempt %d/3). Attempting UNSUB and retry.", retry);
        queryU128(unsubscribe_command, 3000);
        vTaskDelay(pdMS_TO_TICKS(1500));
    }

    modemLog(ModemLogLevel::ERR, "Failed to Set MQTT Subscriber after all retries.");
    return MODEM_RESULT::M_ERROR;
}

MODEM_RESULT UartModemU128::queryU128(const String &command, uint32_t timeoutMs)
{
    MODEM_RESULT finalResult = MODEM_RESULT::M_TIMEOUT;

    if (!lockModem(portMAX_DELAY))
    {
        return MODEM_RESULT::M_SEND_FAIL;
    }

    mState.atResult = MODEM_RESULT::M_TIMEOUT; // 結果を初期化
    mState.isQueryActive = true;

    if (mState.echoDetected)
    {
        mState.echoDetected = false;
        modemLog(ModemLogLevel::WAR, "U128 : Echo-back detected. Sending ATE0 to disable it.");
        sendAtCommand("ATE0");
        vTaskDelay(pdMS_TO_TICKS(100)); // コマンド処理待ちの短いディレイ
    }

    // 3. コマンドの送信
    modemLog(ModemLogLevel::MDBG1, "SND>> : [%s]", command.c_str());
    sendAtCommand(command);

    // 応答を待ち受ける
    TickType_t startTime = xTaskGetTickCount();
    TickType_t ticksToWait = pdMS_TO_TICKS(timeoutMs);
    while (xTaskGetTickCount() - startTime < ticksToWait)
    {
        if (mState.atResult != MODEM_RESULT::M_TIMEOUT)
        {
            finalResult = mState.atResult;
            goto END_QUERY;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    // Timeout処理
    modemLog(ModemLogLevel::ERR, "Command timeout after %u ms: %s", timeoutMs, command.c_str());
    finalResult = MODEM_RESULT::M_TIMEOUT;

    // 同期崩壊およびコマンド入力モード(>)ハングを防ぐための復旧処理
    _uart->write(0x1B); // ESC (0x1B) を送信してコマンド入力モードを強制キャンセル
    vTaskDelay(pdMS_TO_TICKS(100)); // 応答やバッファ到着を待つためのディレイ

    // 物理シリアルバッファの遅延データを破棄
    while (_uart->available() > 0)
    {
        _uart->read();
    }

    // 生データハンドラタスクの受信待ちキューもクリアして遅延応答を完全に無視する
    if (_rawReceiveQueue)
    {
        xQueueReset(_rawReceiveQueue);
    }

END_QUERY:
    mState.isQueryActive = false; // 応答待機フラグをクリア
    unlockModem();
    return finalResult;
}

// 受信データの処理
modemDataPacket UartModemU128::processResponse(const String &response)
{
    // 受信してすぐに処理してよいものはここで処理してしまうこと。
    // Cocobox側に処理を渡したいものは、requiresExecutionフラグをtrueにして本体側処理に送る
    modemDataPacket U128res;
    if (response.isEmpty())
    {
        modemLog(ModemLogLevel::WAR, "Nothing");
        return U128res;
    }
    else
    {
        setString2Char(U128res.message, response, sizeof(U128res.message)); // メッセージの格納
    }
    // U128モデム特有の応答解析ロジックここから
    // OKとエラー
    if (response.startsWith("OK"))
    {
        // modemLog(ModemLogLevel::MDBG3, "U128 : RECEIVE ”OK”.");
        mState.atResult = MODEM_RESULT::M_OK;
    }
    else if (response.indexOf("ERROR") != -1) // ERRORが含まれていたら
    {
        mState.atResult = MODEM_RESULT::M_ERROR;
    }
    else if (response.startsWith("+CEREG:")) // CEREG
    {
        decodeCEREG(response);
    }
    else if (response.indexOf("+SMSUB") != -1) // MQTTからの受信
    {
        modemLog(ModemLogLevel::INF, "U128 : RECEIVE MQTT SUBSCRIBE URC: %s", response.c_str());
        U128res = decodeMqttSub(response);
    }
    else if (response.startsWith("+APP PDP:")) // Slot:0をActiveにしたときのRES。（AT+CNACT=0,1)
    {
        // modemLog(ModemLogLevel::MDBG3, "U128 : RECEIVE APP CONNECTION ANSWER.");
        if (!decodeAPPconnection(response))
        {
            strcpy(U128res.type, "mqttState");
            U128res.mqttstate = MqttConnectType::DISCONNECTED;
            U128res.requiresExecution = true;
        }
    }
    else if (response.startsWith("+CNACT:")) // Slot:0をActiveにしたときのRES。（AT+CNACT=0,1)
    {
        // modemLog(ModemLogLevel::MDBG3, "U128 : RECEIVE APP CONNECTION ANSWER .");
        decodeCNACT(response);
    }
    else if (response.startsWith("+SMSTATE:")) // MQTT接続問い合わせのRES。（AT+SMSTATE?)
    {
        // bx3_log(MDBG3, "U128 : RECEIVE CHECK MQTT CONNECTION ANSWER.");
        if (!decodeSMSTATE(response))
        {
            modemLog(ModemLogLevel::ERR, "U128 : MQTT CONNECTION FAIL DETECTED.");
        }
        strcpy(U128res.type, "mqttState");
        U128res.mqttstate = mState.mqttConnectType;
        U128res.requiresExecution = true;
    }
    else if (response.startsWith(">")) // コマンド送信時のプロンプト
    {
        // modemLog(ModemLogLevel::MDBG3, "U128 : RECEIVE COMMAND INPUT MODE PROMPT.[%s]", response.c_str());
        mState.commandInputMode = true;
    }

    // 1回のみ受信を想定しているもの（起動時受信）
    else if (response.length() == 15 && (response.startsWith(IMSI_PREFIX_DOCOMO) || response.startsWith(IMSI_PREFIX_GLOBAL) || response.startsWith(IMSI_PREFIX_KDDI_1) || response.startsWith(IMSI_PREFIX_KDDI_2))) // KDDIは4405x, 4407x
    {
        mState.IMSI = response;
        // modemLog(ModemLogLevel::MDBG3, "U128 : RECEIVE IMSI.（DOCOMO/GLOBAL/KDDI）: %s", mState.IMSI.c_str());
    }
    else if (response.length() == 15 && response.startsWith(IMSI_PREFIX_JP)) // 日本の事業者なら440か441なので。海外SIM非対応
    {
        mState.IMSI = response;
        // modemLog(ModemLogLevel::MDBG3, "U128 : RECEIVE IMSI.（UNKNOWN JP SIM）: %s", mState.IMSI.c_str());
    }
    else if (response == "RDY") //"RDY"の受信　※BAUDRATEが設定されている場合の起動時に受信
    {
        // modemLog(ModemLogLevel::MDBG3, "U128 : RDY RECEIVED.");
        mState.isU128RDY = true;
    }
    else if (response == "+CFUN: 1") // 起動時応答。"+CFUN : 1"でFULL FUNCTIONARY
    {
        // modemLog(ModemLogLevel::MDBG3, "U128 : FULL FUNCTIONARY");

        mState.isFullFunction = true;
    }
    else if (response.indexOf("PSUTTZ") != -1) // Timecodeの取得（起動時）
    {
        // modemLog(ModemLogLevel::MDBG3, "U128 : RECEIVE TIMECODE(PSUTTZ).");
        // U128res.requiresExecution = true;
        // mState.isGetInitialTimecode = true;
    }
    else if (response.startsWith("+CPIN")) // SIMの状態
    {
        if (response.indexOf("READY") != -1)
        {
            // modemLog(ModemLogLevel::MDBG3, "U128 : SIM READY.");
            mState.isSimState = true;
        }
        else if (response.indexOf("NOT READY") != -1)
        {
            modemLog(ModemLogLevel::ERR, "U128 : SIM NOT READY.");
            mState.isSimState = false;
        }
        else if (response.indexOf("NOT INSERTED") != -1)
        {
            modemLog(ModemLogLevel::ERR, "U128 : SIM NOT INSERTED.");
            mState.isSimState = false;
        }
        else
        {
            modemLog(ModemLogLevel::ERR, "U128 : UNKNOWN SIM STATUS.");
            mState.isSimState = false;
        }
    }
    else if (response == "SMS Ready") //"RDY"の受信　※BAUDRATEが設定されている場合の起動時に受信
    {
        modemLog(ModemLogLevel::MDBG2, "U128 : SMS READY.");
        mState.isSMSready = true;
    }
    else if (response.startsWith("+CPSI")) // Timecodeの取得
    {
        modemLog(ModemLogLevel::MDBG3, "U128 : RECEIVE UE System Information (CPSI).");
        mState.cpsiState = decodeCPSI(response);
        // modemLog(ModemLogLevel::MDBG3, "U128 : MODEMSTATE : %s", mState.cpsiState.rawLine);
        mState.isSystemInfo = true;
    }
    else if (response.startsWith("+CCLK")) // Timecodeの取得
    {
        modemLog(ModemLogLevel::MDBG2, "U128 : RECEIVE TIMECODE(CCLK).");
        strcpy(U128res.type, "timecode");
        U128res.requiresExecution = true;
    }
    else if (response.startsWith("+CSQ")) // Timecodeの取得
    {
        mState.RSSIValue = decodeCSQ(response);
        modemLog(ModemLogLevel::MDBG2, "U128 : RECEIVE Signal Quality(RSSI). : %d[dBm]", mState.RSSIValue);
        U128res.requiresExecution = true;
    }
    else if (response.startsWith("ATE"))
    {
        if (response.indexOf("1") != -1)
        {
            // modemLog(ModemLogLevel::MDBG3, "U128 : SET ECHO ON.");
        }
        else if (response.indexOf("0") != -1)
        {
            // modemLog(ModemLogLevel::MDBG3, "U128 : SET ECHO OFF.");
        }
        else
        {
            modemLog(ModemLogLevel::ERR, "U128 : UNKNOWN ECHO MODE.");
        }
    }
    else if (response == "SIMCOM_SIM7080") // デバイス名
    {
        mState.deviceName = response;
    }
    // U128特有のネットワーク登録通知
    // else if (response.startsWith("+CEREG: "))
    // {
    //     // ... CEREGの解析 ...
    // }
    else if (response.startsWith("+COPS:"))
    {
        modemLog(ModemLogLevel::MDBG2, "U128 : NETWORK OPERATOR INFO : %s", response.c_str());
    }
    else if (response.startsWith("AT") || response.startsWith("at"))
    {
        mState.echoDetected = true;
    }
    else
    {
        // 処理できなかった応答のログ
        modemLog(ModemLogLevel::WAR, "U128 : UNKNOWN RESPONSE [%s]", response.c_str());
    }
    return U128res;
}

////Setter
bool UartModemU128::setMqttConfigration(void) // MQTTのセットアップ（SMCONF）
{

    modemLog(ModemLogLevel::MDBG2, "U128 : SETUP MQTT PARAMETER");

    modemLog(ModemLogLevel::MDBG2, "U128 : SET MQTT SERVER");
    if (queryU128("AT+SMCONF=\"URL\",\"" MQTT_BROKER "\",1883", DEFAULT_TIMEOUT) != MODEM_RESULT::M_OK)
    {
        modemLog(ModemLogLevel::ERR, "Failed to set MQTT Server.");
        return false;
    }

    modemLog(ModemLogLevel::MDBG2, "U128 : SET MQTT CLIENT ID");
    String smconfCommand = "AT+SMCONF=\"CLIENTID\",\"" + mState.IMSI + "\"";
    if (queryU128(smconfCommand, DEFAULT_TIMEOUT) != MODEM_RESULT::M_OK)
    {
        modemLog(ModemLogLevel::ERR, "Failed to set MQTT CLIENT ID.");
        return false;
    }
    return true;
}

////Checker////
bool UartModemU128::checkU128Version()
{
    // リビジョン確認コマンド (AT+GMR) を送信
    // U128は応答に特定の文字列を含むと仮定
    // if (sendCommand("AT+GMR", "U128R", 2000))
    // {
    //     // Serial.println("U128 Version OK.");
    //     return true;
    // }
    return false;
}

// 接続状態をチェックする (AT+SMSTATE? を実行)
MODEM_RESULT UartModemU128::chkMqtt_internal()
{ 
    // modemLog(ModemLogLevel::MDBG3, "U128 : Checking MQTT connection status (AT+SMSTATE?).");

    // ATコマンドを送信し、応答(OK)を待つ
    if (queryU128("AT+SMSTATE?", 10000) != MODEM_RESULT::M_OK)
    {
        // ATコマンドの送信自体が失敗した場合、接続状態の確認ができていないため、
        // 今回の呼び出しは失敗としてfalseを返し、ステートをDISCONNECTEDに更新する
        modemLog(ModemLogLevel::ERR, "Failed to send AT+SMSTATE? command. Cannot verify state.");
        mState.mqttConnectType = MqttConnectType::DISCONNECTED;
        return MODEM_RESULT::M_SEND_FAIL;
    }

    // この時点でAT+SMSTATE? の結果（+SMSTATE: X）は、URC/非同期応答として処理され、
    // メンバ変数 mState.isMqttConnected を更新している
    if (mState.mqttConnectType == MqttConnectType::CONNECTED ||
        mState.mqttConnectType == MqttConnectType::CONNECTED_SP)
    {
        return MODEM_RESULT::M_OK;
    }
    else
    {
        return MODEM_RESULT::M_ERROR;
    }
}

// 信号状態をチェックする (AT+SMSTATE? を実行)
bool UartModemU128::checkSIM()
{
    if (queryU128("AT+CPIN?", 10000) != MODEM_RESULT::M_OK)
    {
        modemLog(ModemLogLevel::ERR, "Failed to send AT+CPIN? command. Cannot verify state.");
        return false;
    }

    if (mState.isSimState)
    {
        return true;
    }
    else
    {
        return false;
    }
}

bool UartModemU128::chkPdpConnection()
{
    if (queryU128("AT+CNACT?", 10000) != MODEM_RESULT::M_OK)
    {
        modemLog(ModemLogLevel::ERR, "Failed to send AT+CNACT? command.");
        return false;
    }

    return mState.isPdpConnection;
}

CpsiState_t UartModemU128::chkSignal()
{
    if (queryU128("AT+CPSI?", 10000) != MODEM_RESULT::M_OK)
    {
        modemLog(ModemLogLevel::ERR, "Failed to send AT+CPSI? command. Cannot verify state.");
        mState.cpsiState.isDataValid = false;
        return mState.cpsiState;
    }
    return mState.cpsiState;
}

bool UartModemU128::chkWakeupState() // 起動時のログからU128の起動完了を読み取る
{
    uint32_t timeoutMs = 20000;
    // タイムアウト管理のための開始時刻
    TickType_t startTime = xTaskGetTickCount();
    TickType_t ticksToWait;

    while (true)
    {
        TickType_t elapsedTicks = xTaskGetTickCount() - startTime;
        TickType_t totalTicks = pdMS_TO_TICKS(timeoutMs);

        if (elapsedTicks >= totalTicks) // Timeout
        {
            modemLog(ModemLogLevel::WAR, "U128: U128 WAKEUP TIMEOUT.[%d][%d][%d][%d]", mState.isU128RDY, mState.isFullFunction, mState.isSimState, mState.isSMSready);
            return false;
        }
        else if (
            mState.isU128RDY &&
            mState.isFullFunction &&
            mState.isSimState &&
            mState.isSMSready)
        {
            // modemLog(ModemLogLevel::MDBG3, "U128: U128 WAKEUP OK.");
            return true;
        }
    }
}

char *UartModemU128::chkSystemInformation_internal()
{ 
    mState.isSystemInfo = false;
    mState.cpsiState.rawLine[0] = '\0';        // 実行前に必ず空にしておく
    mState.atResult = MODEM_RESULT::M_UNKNOWN; // 実行状態を初期化

    modemLog(ModemLogLevel::MDBG2, "U128 : CHK SYSTEM INFORMATION");
    sendAtCommand("AT+CPSI?");

    int counter = 0;
    // 1. +CPSI: レスポンスのパース待ち
    while (1)
    {
        if (mState.isSystemInfo)
        {
            break;
        }
        else if (counter > 10)
        {
            modemLog(ModemLogLevel::ERR, "U128 : Get System Info Response Timeout");
            return mState.cpsiState.rawLine; // 空文字列を返す
        }
        vTaskDelay(pdMS_TO_TICKS(100)); // CPU負荷軽減のための短い待ち
        counter++;
    }

    counter = 0;
    // 2. 最終的な OK / ERROR 判定待ち
    while (1)
    {
        if (mState.atResult == MODEM_RESULT::M_OK)
        {
            vTaskDelay(pdMS_TO_TICKS(500)); // ログ表示乱れ対策
            // modemLog(ModemLogLevel::MDBG3, "U128 : Get System Info success");
            // modemLog(ModemLogLevel::MDBG3, "U128 : MODEMSTATE : %s", mState.cpsiState.rawLine);
            return mState.cpsiState.rawLine; // 成功したデータを返す
        }
        else if (mState.atResult == MODEM_RESULT::M_ERROR)
        {
            modemLog(ModemLogLevel::ERR, "U128 : Could Not Get System Info.");
            mState.cpsiState.rawLine[0] = '\0';
            return mState.cpsiState.rawLine;
        }
        else if (counter > 10)
        {
            modemLog(ModemLogLevel::ERR, "U128 : Get System Info Result TIMEOUT");
            mState.cpsiState.rawLine[0] = '\0';
            return mState.cpsiState.rawLine;
        }
        else
        {
            vTaskDelay(pdMS_TO_TICKS(1000));
            counter++;
        }
    }
}

String UartModemU128::getSimStateInformation()
{
    // モデムから最新の系統情報(CPSI)を取得
    chkSystemInformation();

    int simMode = 0;
    int simSel = 0;
    int bandSel = 0;
    if (CocoboxConfigManager != nullptr)
    {
        simMode = CocoboxConfigManager->getValueByAccessKey("SIMMODE");
        simSel = CocoboxConfigManager->getValueByAccessKey("SIMSEL");
        bandSel = CocoboxConfigManager->getValueByAccessKey("BANDSEL");
    }

    // キャリア名の判定 (mccMnc: 44010 -> DOCOMO, 44020 -> SOFTBANK)
    String carrierStr = "UNKNOWN";
    if (mState.cpsiState.mccMnc == 44010)
    {
        carrierStr = "DOCOMO";
    }
    else if (mState.cpsiState.mccMnc == 44020)
    {
        carrierStr = "SOFTBANK";
    }
    else if (mState.cpsiState.mccMnc == 44050 || mState.cpsiState.mccMnc == 44070)
    {
        carrierStr = "KDDI";
    }
    else if (mState.cpsiState.mccMnc > 0)
    {
        carrierStr = String(mState.cpsiState.mccMnc);
    }

    // バンド名の整形 (例: band=1 -> "B1")
    String bandStr = "UNKNOWN";
    if (mState.cpsiState.band > 0)
    {
        bandStr = "B" + String(mState.cpsiState.band);
    }

    // 要求フォーマット: SIMMODE=0,SIMSEL=0,BANDSEL=0,CARRIER=DOCOMO,BAND=B1
    String result = "SIMMODE=" + String(simMode) +
                    ",SIMSEL=" + String(simSel) +
                    ",BANDSEL=" + String(bandSel) +
                    ",CARRIER=" + carrierStr +
                    ",BAND=" + bandStr;

    return result;
}

/////Decorder////
static String hexToAsciiString(const String &hexInput)
{
    if (hexInput.length() == 0 || hexInput.length() % 2 != 0)
    {
        return hexInput;
    }
    for (size_t i = 0; i < hexInput.length(); i++)
    {
        char c = hexInput.charAt(i);
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f')))
        {
            return hexInput;
        }
    }
    String asciiResult = "";
    asciiResult.reserve(hexInput.length() / 2);
    for (size_t i = 0; i < hexInput.length(); i += 2)
    {
        String byteStr = hexInput.substring(i, i + 2);
        char byteVal = (char)strtol(byteStr.c_str(), NULL, 16);
        asciiResult += byteVal;
    }
    return asciiResult;
}

// Jsonデータのパース
bool UartModemU128::parseJsonPayload(const String &jsonPayload, modemDataPacket &packet)
{
    // modemLog(ModemLogLevel::INF, "DECODED JSON2 : %s", jsonPayload.c_str());
    // JSON解析のためのJsonDocumentの準備
    JsonDocument doc;

    // JSONをデシリアライズ
    DeserializationError error = deserializeJson(doc, jsonPayload);

    if (error)
    {
        // デシリアライズ失敗時の処理
        return false;
    }

    // --------------------------------------------------------
    // 💡 修正箇所: JSON抽出結果 (String) を char[] に安全にコピー
    // --------------------------------------------------------

    // 1. type の格納
    String typeString = doc["type"].as<String>();
    setString2Char(packet.type, typeString, sizeof(packet.type));

    // 2. imsi の格納
    String imsiString = doc["imsi"].as<String>();
    setString2Char(packet.imsi, imsiString, sizeof(packet.imsi));

    // 3. message の格納 (HEXエンコード時は自動デコード)
    String messageString = doc["msg"].as<String>();
    messageString = hexToAsciiString(messageString);
    setString2Char(packet.message, messageString, sizeof(packet.message));

    // 4. cclk の格納
    String cclkString = doc["cclk"].as<String>();
    // タイムゾーンを削除して格納
    int plusIndex = cclkString.indexOf('+');
    if (plusIndex != -1)
    {
        cclkString = cclkString.substring(0, plusIndex);
    }
    setString2Char(packet.cclk, cclkString, sizeof(packet.cclk));

    // 5. UT の格納
    String utString = doc["ut"].as<String>(); // または doc["msg"]
    setString2Char(packet.ut, utString, sizeof(packet.ut));

    // 4. topic の格納 (必要であれば)
    // 通常 topic は decodeMqttSub で既に設定されているが、ここでは JSON 内の値のコピーを想定
    if (doc["topic"].is<JsonVariant>())
    {
        String topicString = doc["topic"].as<String>();
        setString2Char(packet.topic, topicString, sizeof(packet.topic));
    }

    // --------------------------------------------------------

    return true;
}

// MQTT SUBをデコード
modemDataPacket UartModemU128::decodeMqttSub(const String &subResponse)
{
    modemDataPacket packet;
    packet.requiresExecution = true; // 実行要求を与えてCOCOBOX側の処理Queueに回す

    // 1. "+SMSUB: " の後の最初のダブルクォーテーションの位置を探す
    int firstQuote = subResponse.indexOf('"');
    if (firstQuote == -1)
        return packet;

    // 2. 2番目のダブルクォーテーション（トピックの終端）を探す
    int secondQuote = subResponse.indexOf('"', firstQuote + 1);
    if (secondQuote == -1)
        return packet;

    // 3. トピックを抽出 (Stringで一時的に保持)
    String topicString = subResponse.substring(firstQuote + 1, secondQuote);
    setString2Char(packet.topic, topicString, sizeof(packet.topic));

    // 4. JSON文字列の開始位置を探す（トピックの次の "）
    int jsonStart = subResponse.indexOf('"', secondQuote + 1);
    if (jsonStart == -1)
        return packet;

    // 5. JSON文字列の終端を探す（最後の "）
    // RTL (右から左) 検索で最後の " を探す
    int jsonEnd = subResponse.lastIndexOf('"');
    if (jsonEnd == jsonStart)
        return packet;

    // 6. JSONペイロードを抽出
    String jsonPayload = subResponse.substring(jsonStart + 1, jsonEnd);
    // modemLog(ModemLogLevel::INF, "DECODED JSON : %s", jsonPayload.c_str());

    // 7. JSONを解析し、構造体に格納 (ステップ bへ)
    if (jsonPayload.length() > 0)
    {
        if (parseJsonPayload(jsonPayload, packet))
        {
            return packet;
        }
    }

    return packet;
}

bool UartModemU128::decodeAPPconnection(const String &Response)
{
    // 1. 接頭辞 "+APP PDP: " 以降のデータ部分を抽出
    int pdpIndex;
    int dataStartIndex = Response.indexOf("+APP PDP: ");
    if (dataStartIndex == -1)
    {
        pdpIndex = -1;
        return false;
    }
    // " " の直後から開始
    dataStartIndex += String("+APP PDP: ").length();

    // 2. カンマの位置を探す
    int commaIndex = Response.indexOf(',', dataStartIndex);
    if (commaIndex == -1)
    {
        pdpIndex = -1;
        return false;
    }

    // 3. 1番目の値（PDP Index）を抽出・変換
    String indexStr = Response.substring(dataStartIndex, commaIndex);
    indexStr.trim();
    pdpIndex = indexStr.toInt();

    // 4. 2番目の値（State）を抽出
    String stateStr = Response.substring(commaIndex + 1);
    stateStr.trim();

    if ((pdpIndex == 0) && (stateStr == "ACTIVE")) // スロットゼロしか使う予定がないのでこうしておく
    {
        modemLog(ModemLogLevel::INF, "U128 : PDP CONNECTION OK: %s", Response.c_str());
        mState.isPdpConnection = true;
        this->isPdpConnected = true;
        return true;
    }
    else
    {
        modemLog(ModemLogLevel::ERR, "U128 : PDP CONNECTION FAIL : %s", Response.c_str());
        if (pdpIndex == 0)
        {
            mState.isPdpConnection = false;
            if (this->isPdpConnected)
            {
                this->isPdpConnected = false;
                this->wasPdpReset = true;
            }
        }
        return false;
    }
}

// CSQで得たデータから、電波強度をdbmで取得する
int UartModemU128::decodeCSQ(const String &csqResponse)
{
    // 1. 接頭辞 "+CSQ: " を確認し、それ以降の数値部分を探す
    int dataStartIndex = csqResponse.indexOf("+CSQ: ");
    if (dataStartIndex == -1) // 応答形式が不正
    {
        return 0;
    }

    dataStartIndex += String("+CSQ: ").length(); // 数値部分の開始位置を特定

    int commaIndex = csqResponse.indexOf(',', dataStartIndex); // カンマ ',' の位置を探す (RSSIとBERの区切り)
    if (commaIndex == -1)                                      // カンマが見つからない（形式不正）
    {
        return 0;
    }

    String rssiStr = csqResponse.substring(dataStartIndex, commaIndex); // SSI値 (最初の数値) を抽出
    rssiStr.trim();                                                     // 前後の空白を除去
    int rssiValue = rssiStr.toInt();                                    // 抽出した文字列を整数に変換

    if (rssiValue >= 0 && rssiValue < 31) // dBmへの変換とチェック　※31はアンテナなしで31になるので不正とする
    {
        int dbm = (2 * rssiValue) - 113;
        return dbm;
    }
    else if (rssiValue == 99)
    {
        // 99は信号未検出または適用外
        return 0;
    }
    // その他の不正な値
    return 0;
}

// CSPIのデコード（UE System Information）
CpsiState_t UartModemU128::decodeCPSI(const String &cpsiResponse)
{
    // 戻り値となる構造体をローカルで宣言し、初期化
    CpsiState_t resultState = {};
    resultState.isDataValid = false;

    int dataStartIndex = cpsiResponse.indexOf(":") + 1;
    if (dataStartIndex <= 0) // ':' が見つからないか、プレフィックスがない
    {
        modemLog(ModemLogLevel::ERR, "CPSI: Invalid response format or prefix missing.");
        return resultState;
    }
    String dataPart = cpsiResponse.substring(dataStartIndex);
    dataPart.trim();

    // +CPSI: NO SERVICE, Online の場合の処理
    if (dataPart.startsWith("NO SERVICE"))
    {
        modemLog(ModemLogLevel::WAR, "CPSI: NO SERVICE.");
        return resultState;
    }

    // 応答のフィールド数は多くて14個 (RSRQ以降も含む)
    String values[14];
    int prevIndex = -1;
    for (int i = 0; i < 14; i++)
    {
        int commaIndex = dataPart.indexOf(',', prevIndex + 1);

        if (commaIndex == -1)
        {
            values[i] = dataPart.substring(prevIndex + 1);
        }
        else
        {
            values[i] = dataPart.substring(prevIndex + 1, commaIndex);
        }
        values[i].trim();

        if (commaIndex == -1)
            break;

        prevIndex = commaIndex;
    }

    // --- 構造体 (resultState) への格納 ---

    // 0. System Mode (INDEX 0) -> Enum に変換
    if (values[0] == "LTE CAT-M1")
    {
        resultState.systemMode = CPSI_MODE_LTM1;
    }
    else if (values[0] == "NB-IOT")
    {
        resultState.systemMode = CPSI_MODE_NBIOT;
    }
    else if (values[0] == "NO SERVICE" || values[0].length() == 0)
    {
        resultState.systemMode = CPSI_MODE_NONE;
    }
    else
    {
        resultState.systemMode = CPSI_MODE_UNKNOWN;
    }

    if (resultState.systemMode != CPSI_MODE_NONE)
    {
        resultState.isDataValid = true;
    }

    // 1. Operation Mode (INDEX 1) -> Enum に変換
    if (values[1] == "Online")
    {
        resultState.operationMode = CPSI_OP_ONLINE;
    }
    else if (values[1] == "Low Power")
    {
        resultState.operationMode = CPSI_OP_LOWPOWER;
    }
    else
    {
        resultState.operationMode = CPSI_OP_UNKNOWN;
    }

    // 2. MobileCountryCode/MobileNetworkCode (INDEX 2) -> Int に変換
    // "440-10" のハイフンを削除して int に
    String mccMncStr = values[2];
    mccMncStr.replace("-", "");
    resultState.mccMnc = (unsigned int)mccMncStr.toInt();

    // 3. Tracing Area Code (INDEX 3) -> 16進数として Int に変換
    // TACは16進数の文字列として返されるため、strtolで基数16としてパース
    resultState.tracingAreaCode = (unsigned int)strtol(values[3].c_str(), NULL, 16);

    // 4. S-CELLID (INDEX 4) -> Int に変換
    resultState.servingCellId = (unsigned int)values[4].toInt();

    if (values[6].length() > 0)
    {
        String b = values[6];
        // よくある接頭辞を削除して数字だけにする
        b.replace("EUTRAN-BAND", "");
        b.replace("LTE-BAND", "");
        b.replace("BAND", "");

        resultState.band = (unsigned int)b.toInt();
    }

    // --- 信号品質 (RSRQ: INDEX 10, RSRP: INDEX 11, RSSI: INDEX 12, RSSNR: INDEX 13) ---
    // (値の有無をチェックしてから変換)

    // RSRQ (INDEX 10)
    if (values[10].length() > 0)
    {
        resultState.rsrq = values[10].toInt();
    }

    // RSRP (INDEX 11)
    if (values[11].length() > 0)
    {
        resultState.rsrp = values[11].toInt();
    }

    // RSSI (INDEX 12)
    if (values[12].length() > 0)
    {
        resultState.rssi = values[12].toInt();
    }

    // RSSNR (INDEX 13)
    if (values[13].length() > 0)
    {
        resultState.rssnr = values[13].toInt();
    }

    const char *opModeStr = getOperationModeString(resultState.operationMode);
    snprintf(resultState.rawLine, sizeof(resultState.rawLine),
             "MODE=%s,OP=%s,CARRIER=%u,TAC=0x%X,CELL_ID=0x%X,BAND=B%u,RSRQ=%d,RSRP=%d,RSSI=%d,SNR=%d",
             getSystemModeString(resultState.systemMode),
             opModeStr,
             resultState.mccMnc,
             resultState.tracingAreaCode,
             resultState.servingCellId,
             resultState.band,
             resultState.rsrq,
             resultState.rsrp,
             resultState.rssi,
             resultState.rssnr);

    // printCPSI(resultState);
    // 構造体を呼び出し元に返す
    return resultState;
}

// CpsiSystemMode_t を文字列に変換
const char *UartModemU128::getSystemModeString(CpsiSystemMode_t mode)
{
    switch (mode)
    {
    case CPSI_MODE_LTM1:
        return "LTE CAT-M1";
    case CPSI_MODE_NBIOT:
        return "NB-IOT";
    case CPSI_MODE_NONE:
        return "NONE/NO SERVICE";
    case CPSI_MODE_UNKNOWN:
    default:
        return "UNKNOWN";
    }
}

// CpsiOperationMode_t を文字列に変換
const char *UartModemU128::getOperationModeString(CpsiOperationMode_t mode)
{
    switch (mode)
    {
    case CPSI_OP_ONLINE:
        return "Online";
    case CPSI_OP_LOWPOWER:
        return "Low Power Mode";
    case CPSI_OP_NONE:
        return "NONE";
    case CPSI_OP_UNKNOWN:
    default:
        return "UNKNOWN";
    }
}

// CPSI を表示
void UartModemU128::printCPSI(const CpsiState_t &state)
{
    if (!state.isDataValid)
    {
        modemLog(ModemLogLevel::ERR, "CPSI: Data is invalid or NO SERVICE.");
        return;
    }

    // SYSTEM MODE
    const char *sysModeStr = getSystemModeString(state.systemMode);
    modemLog(ModemLogLevel::INF, "--CPSI : SYSTEM MODE           : %s", sysModeStr);

    // OPERATION MODE
    const char *opModeStr = getOperationModeString(state.operationMode);
    modemLog(ModemLogLevel::INF, "--CPSI : OPERATION MODE        : %s", opModeStr);

    // MCC-MNC CODE
    modemLog(ModemLogLevel::INF, "--CPSI : MCC-MNC CODE          : %u", state.mccMnc);

    // TRACING AREA CODE (16進数で出力)
    modemLog(ModemLogLevel::INF, "--CPSI : TRACING AREA CODE     : 0x%X", state.tracingAreaCode);

    // SERVING CELL-ID (16進数で出力)
    modemLog(ModemLogLevel::INF, "--CPSI : SERVING CELL-ID       : 0x%X", state.servingCellId);

    // RSRQ (Index値として出力)
    modemLog(ModemLogLevel::INF, "--CPSI : RSRQ                  : %d", state.rsrq);

    // RSRP (dBm Index値として出力)
    modemLog(ModemLogLevel::INF, "--CPSI : RSRP                  : %d[dBm index]", state.rsrp);

    // RSSI (dBm Index値として出力)
    modemLog(ModemLogLevel::INF, "--CPSI : RSSI                  : %d[dBm index]", state.rssi);

    // RSSNR (値として出力)
    modemLog(ModemLogLevel::INF, "--CPSI : RSSNR                 : %d", state.rssnr);
}

bool UartModemU128::decodeCEREG(const String &response)
{
    // 1. "+CEREG:" のプレフィックスを検索
    if (!response.startsWith("+CEREG:"))
    {
        modemLog(ModemLogLevel::ERR, "decodeCEREG : Bad Response (Missing +CEREG: prefix).");
        return false;
    }

    // データ部分の開始位置を調整 ("+CEREG: " = 8文字)
    int dataStart = 8;
    int currentPos = dataStart;

    // --- フィールド 1: <n> (レポートモード) の抽出 ---
    int firstComma = response.indexOf(',', currentPos);
    if (firstComma == -1)
    {
        // カンマが無い場合（非同期通知 URC の '+CEREG: <stat>' の形式）
        String stat_str = response.substring(currentPos);
        stat_str.trim();
        int stat_val = stat_str.toInt();
        
        mState.LteStatus = stat_val;
        // modemLog(ModemLogLevel::MDBG2, "U128 : CEREG Status (URC) : [%d].", mState.LteStatus);
        return true;
    }

    // <n> の値 (通常 0, 1, 2) を抽出
    String n_str = response.substring(currentPos, firstComma);
    int n_val = n_str.toInt();
    // modemLog(ModemLogLevel::MDBG3, "decodeCEREG : report Mode : %d", n_val);

    // --- フィールド 2: <stat> (登録状態) の抽出 ---
    currentPos = firstComma + 1;
    int secondComma = response.indexOf(',', currentPos);

    // 2番目のカンマがない場合（応答が '+CEREG: n,stat' のシンプルな形式）
    if (secondComma == -1)
    {
        // 最後のフィールド <stat> を抽出
        String stat_str = response.substring(currentPos);

        // スペースや改行を削除して数値化
        stat_str.trim();
        int stat_val = stat_str.toInt();

        // 状態をグローバルステートに格納
        mState.LteStatus = stat_val;
        // modemLog(ModemLogLevel::MDBG2, "U128 : CEREG Status (Simple) : [%d].", mState.LteStatus);

        return true;
    }
    // 2番目のカンマがある場合（応答が '+CEREG: n,stat,tac,...' の詳細な形式）
    else
    {
        // <stat> の値 (登録状態) を抽出
        String stat_str = response.substring(currentPos, secondComma);
        stat_str.trim();
        int stat_val = stat_str.toInt();

        // 状態をグローバルステートに格納
        mState.LteStatus = stat_val;
        // modemLog(ModemLogLevel::MDBG2, "U128 : CEREG Status (Detail) : [%d].", mState.LteStatus);

        // 必要であれば、tac や ci のデコードをここに追加できますが、今回はステータスのみ
        return true;
    }

    return false; // 終了のダブルクォートが見つからない
}

bool UartModemU128::decodeCNACT(const String &response)
{
    // 1. "+CNACT: " のプレフィックスを検索してスキップ
    if (!response.startsWith("+CNACT:"))
    {
        modemLog(ModemLogLevel::ERR, "decodeCNACT : Bad Response (Missing +CNACT: prefix).");
        return false;
    }

    // データ部分の開始位置を調整
    int dataStart = 8;

    // 最初のフィールド（コンテキストID）が '0' であることを確認
    int firstComma = response.indexOf(',', dataStart);
    if (firstComma == -1)
    {
        modemLog(ModemLogLevel::ERR, "decodeCNACT : Coud not find comma.");
        return false; // カンマが見つからない
    }
    // データ部分の最初の文字が '0' で、かつその後にカンマが続くことをチェック
    if (response.charAt(dataStart) != '0' || firstComma != dataStart + 1)
    {
        // modemLog(ModemLogLevel::MDBG3, "decodeCNACT : CID NOT ZERO : SKIPPED.");
        return false; // CIDが0ではないため処理をスキップ
    }

    // Stateが '1' である(Active)ことを確認 (最初のカンマの次から2番目のカンマの前まで)
    int secondComma = response.indexOf(',', firstComma + 1);
    if (secondComma == -1)
    {
        modemLog(ModemLogLevel::ERR, "decodeCNACT : Coud not find comma.");
        mState.isPdpConnection = false;
        return false; // 2番目のカンマが見つからない
    }
    if (response.charAt(firstComma + 1) != '1' || secondComma != firstComma + 2)
    {
        modemLog(ModemLogLevel::ERR, "decodeCNACT : Slot:0 not activated.");
        mState.isPdpConnection = false;
        return false; // 状態が '1' ではないため、エラーとみなしスキップ
    }

    //    IPアドレスは3番目のフィールドとしてダブルクォートで囲まれている
    int ipStart = response.indexOf('"', secondComma);

    if (ipStart == -1)
    {
        modemLog(ModemLogLevel::ERR, "decodeCNACT : could not find IP-Address.");
        mState.isPdpConnection = false;
        return false; // 開始のダブルクォートが見つからない
    }
    int ipEnd = response.indexOf('"', ipStart + 1);
    if (ipEnd != -1)
    {
        mState.IPAddress = response.substring(ipStart + 1, ipEnd);
        modemLog(ModemLogLevel::INF, "U128 : IP-Address Found : [%s].", mState.IPAddress.c_str());
        mState.isPdpConnection = true;
        return true;
    }
    mState.isPdpConnection = false;
    return false; // 終了のダブルクォートが見つからない
}

// SMSTATEのデコード
bool UartModemU128::decodeSMSTATE(const String &response)
{
    if (!response.startsWith("+SMSTATE:"))
    {
        modemLog(ModemLogLevel::ERR, "decodeSMSTATE : Bad Response (Missing +SMSTATE: prefix).");
        mState.mqttConnectType = MqttConnectType::UNKNOWN;
        return false;
        // return;
    }

    if (response.endsWith("1"))
    {
        // MQTT CONNECTED.
        // modemLog(ModemLogLevel::MDBG3, "MQTT CONNECTED.");
        mState.mqttConnectType = MqttConnectType::CONNECTED;
        return true;
        // return;
    }
    else if (response.endsWith("2"))
    {
        // MQTT CONNECTED.
        modemLog(ModemLogLevel::MDBG2, "MQTT CONNECTED.(SP-FLG ON)");
        mState.mqttConnectType = MqttConnectType::CONNECTED_SP;
        return true;
        // return;
    }
    else if (response.endsWith("0"))
    {
        // MQTT DISCONNECTED.
        modemLog(ModemLogLevel::WAR, "U128:MQTT NOT CONNECTED.(SMSTATE FAIL)");
        mState.mqttConnectType = MqttConnectType::DISCONNECTED;
        return false;
        // return;
    }
    else
    {
        modemLog(ModemLogLevel::ERR, "decodeSMSTATE : Bad Parameter.");
        mState.mqttConnectType = MqttConnectType::UNKNOWN;
        return false;
        // return;
    }
}

bool UartModemU128::requestTimecode(void)
{
    if (queryU128("AT+CCLK?", DEFAULT_TIMEOUT) != MODEM_RESULT::M_OK)
    {
        modemLog(ModemLogLevel::ERR, "TIMECODE REQUEST ERROR.(AT+CCLK?)");
        return false;
    }
    return true;
}

// 旧FreeSpace電文の送信
bool UartModemU128::sendFsMessage(const String &message)
{
    int hexMode = 1; // デフォルトはON (HEX変換)
    if (CocoboxConfigManager != nullptr)
    {
        int val = CocoboxConfigManager->getValueByAccessKey("HEXMODE");
        if (val != -1)
        {
            hexMode = val;
        }
    }

    String msgPayload;
    if (hexMode != 0)
    {
        // 受け取ったメッセージを16進数ASCIIに変換
        for (int i = 0; i < message.length(); i++)
        {
            char hexBuffer[3];
            sprintf(hexBuffer, "%02X", message.charAt(i));
            msgPayload += hexBuffer;
        }
    }
    else
    {
        // HEXMODE=0 (OFF) の場合はメッセージをそのまま使用
        msgPayload = message;
    }

    // JSONペイロードの構築
    JsonDocument doc; // メモリを確保 (ArduinoJson v7)

    doc["type"] = "fs";
    doc["imsi"] = mState.IMSI;
    doc["msg"]  = msgPayload;

    String plainPayload;
    serializeJson(doc, plainPayload); // JSON文字列に変換
    size_t payloadLength = plainPayload.length();

    // 0. MQTT接続チェック
    if (mState.mqttConnectType != MqttConnectType::CONNECTED &&
        mState.mqttConnectType != MqttConnectType::CONNECTED_SP)
    {
        modemLog(ModemLogLevel::ERR, "Cannot send FS-Message: MQTT is not connected (State: %d)", (int)mState.mqttConnectType);
        return false;
    }

    // AT+SMPUB コマンド文字列を構築
    String smpubCommand = "AT+SMPUB=\"BIoT/up\"," + String(payloadLength) + ",1,1";

    modemLog(ModemLogLevel::MDBG3, "DBG>> : [%s]", smpubCommand.c_str());

    // プロンプトフラグを初期化
    mState.commandInputMode = false;

    // AT+SMPUB コマンドの送信
    sendAtCommand(smpubCommand.c_str());

    // コマンドプロンプト (>) 待ち
    int counter = 0;
    while (1)
    {
        if (mState.commandInputMode)
        {
            // プロンプトを受信したらここにくる
            mState.commandInputMode = false; // 次のためにフラグをリセット
            break;
        }
        else if (mState.mqttConnectType != MqttConnectType::CONNECTED &&
                 mState.mqttConnectType != MqttConnectType::CONNECTED_SP)
        {
            modemLog(ModemLogLevel::ERR, "MQTT disconnected while waiting for command prompt (>).");
            _uart->write(0x1B);
            vTaskDelay(pdMS_TO_TICKS(50));
            while (_uart->available() > 0) { _uart->read(); }
            if (_rawReceiveQueue) { xQueueReset(_rawReceiveQueue); }
            return false;
        }
        else if (mState.atResult == MODEM_RESULT::M_ERROR)
        {
            modemLog(ModemLogLevel::ERR, "AT+SMPUB returned ERROR while waiting for prompt (>).");
            _uart->write(0x1B);
            vTaskDelay(pdMS_TO_TICKS(50));
            while (_uart->available() > 0) { _uart->read(); }
            if (_rawReceiveQueue) { xQueueReset(_rawReceiveQueue); }
            mState.mqttConnectType = MqttConnectType::DISCONNECTED;
            return false;
        }
        else if (counter > 50) // 5秒 (50 * 100ms)
        {
            modemLog(ModemLogLevel::ERR, "Timeout waiting for command prompt (>).");
            _uart->write(0x1B); // ESC送信でプロンプト入力状態をキャンセル
            vTaskDelay(pdMS_TO_TICKS(100));
            while (_uart->available() > 0) { _uart->read(); }
            if (_rawReceiveQueue) { xQueueReset(_rawReceiveQueue); }
            mState.mqttConnectType = MqttConnectType::DISCONNECTED;
            return false;
        }
        else
        {
            vTaskDelay(pdMS_TO_TICKS(100));
            counter++;
        }
    }

    // ペイロードデータ本体の送信
    if (queryU128(plainPayload, 10000) != MODEM_RESULT::M_OK)
    {
        modemLog(ModemLogLevel::ERR, "Failed to Send FS-Message.");
        mState.mqttConnectType = MqttConnectType::DISCONNECTED;
        return false;
    }

    // modemLog(ModemLogLevel::INF, "FS data sent successfully.");
    return true;
}

SimCarrier UartModemU128::chkSimCarrier(String IMSI)
{
    // 1. 桁数チェック
    // 一般的にIMSIは15桁（稀に短いものもあるが、国内キャリアなら15桁）
    if (IMSI.length() < 5)
    {
        modemLog(ModemLogLevel::ERR, "chkSimCarrier : Invalid IMSI length (%d)", IMSI.length());
        return SimCarrier::UNKNOWN;
    }

    // 2. キャリア判定 (MCC+MNC)
    // 440 10 -> docomo
    if (IMSI.startsWith("44010"))
    {
        modemLog(ModemLogLevel::INF, "chkSimCarrier : DETECTED DOCOMO SIM");
        return SimCarrier::DOCOMO;
    }
    // 440 20 -> SoftBank
    else if (IMSI.startsWith("44020"))
    {
        modemLog(ModemLogLevel::INF, "chkSimCarrier : DETECTED SOFTBANK SIM");
        return SimCarrier::SOFTBANK;
    }
    // 440 5x / 440 7x -> KDDI
    else if (IMSI.startsWith(IMSI_PREFIX_KDDI_1) || IMSI.startsWith(IMSI_PREFIX_KDDI_2))
    {
        modemLog(ModemLogLevel::INF, "chkSimCarrier : DETECTED KDDI SIM");
        return SimCarrier::KDDI;
    }
    // 901 -> Global SIM (Soracomなどのローミング用)
    // 901は特定の国に属さない「国際共有コード」
    else if (IMSI.startsWith("901"))
    {
        modemLog(ModemLogLevel::INF, "chkSimCarrier : DETECTED GLOBAL SIM");
        return SimCarrier::GLOBAL;
    }

    // いずれにも当てはまらない場合
    modemLog(ModemLogLevel::WAR, "chkSimCarrier : UNKNOWN CARRIER (IMSI:%s)", IMSI.c_str());
    return SimCarrier::UNKNOWN;
}

void UartModemU128::setCarrierSW(bool sw)
{
    this->carrierSW = sw;
    // ログを出しておくとデバッグが捗ります
    modemLog(ModemLogLevel::INF, "CarrierSW set to: %s", sw ? "TRUE" : "FALSE");

    // 必要に応じて、ここで SimCarrier の状態をリセットするなどの処理を今後追加できます
}

void UartModemU128::setPlatinumBandSW(bool sw)
{
    this->platinumBandOnlySW = sw;
    modemLog(ModemLogLevel::INF, "PlatinumBandOnlySW set to: %s", sw ? "TRUE" : "FALSE");
}

void UartModemU128::ModemDisconnectTest(int flg)
{
    modemLog(ModemLogLevel::WAR, "[TEST] ModemDisconnectTest called with flg = %d", flg);
    switch (flg)
    {
    case 0:
        // 復旧用コマンド
        modemLog(ModemLogLevel::INF, "[TEST] Restoring network connection (AT+CGATT=1)...");
        queryU128("AT+CGATT=1", 10000);
        break;
    case 1:
        // ソフトウェア的なモック URC の注入
        modemLog(ModemLogLevel::INF, "[TEST] Injecting mock +SMSTATE: 0 URC...");
        {
            modemDataPacket mockPkt = {};
            strcpy(mockPkt.type, "mqttState");
            mockPkt.mqttstate = MqttConnectType::DISCONNECTED;
            mockPkt.requiresExecution = true;
            if (_urcQueue)
            {
                xQueueSend(_urcQueue, &mockPkt, 0);
            }
        }
        break;
    case 2:
        // PDPコンテキスト切断のシミュレート (AT+CNACT=0,0)
        modemLog(ModemLogLevel::INF, "[TEST] Simulating PDP disconnect (AT+CNACT=0,0)...");
        queryU128("AT+CNACT=0,0", 10000);
        break;
    case 3:
        // キャリア回線切断のシミュレート (AT+CGATT=0)
        modemLog(ModemLogLevel::INF, "[TEST] Simulating Carrier disconnect (AT+CGATT=0)...");
        queryU128("AT+CGATT=0", 10000);
        break;
    case 4:
        // モデム自律再起動のシミュレート (AT+CFUN=1,1)
        modemLog(ModemLogLevel::INF, "[TEST] Simulating Modem Reset (AT+CFUN=1,1)...");
        queryU128("AT+CFUN=1,1", 10000);
        break;
    default:
        modemLog(ModemLogLevel::WAR, "[TEST] Unknown test pattern flg = %d", flg);
        break;
    }
}

bool UartModemU128::isGlobalSim() const
{
    return mState.simCarrier == SimCarrier::GLOBAL;
}

String UartModemU128::getSimCarrierString() const
{
    if (mState.simCarrier == SimCarrier::GLOBAL)
    {
        int simMode = 0;
        int simSel = 0;
        if (CocoboxConfigManager != nullptr)
        {
            simMode = CocoboxConfigManager->getValueByAccessKey("SIMMODE");
            simSel = CocoboxConfigManager->getValueByAccessKey("SIMSEL");
        }

        if (simMode == 3)
        {
            return "GAUTO_A";
        }
        else if (simMode == 2)
        {
            return (simSel == 1) ? "GAUTO_S" : "GAUTO_D";
        }
        else if (simMode == 1)
        {
            return (simSel == 1) ? "GFIX_S" : "GFIX_D";
        }
        else
        {
            return carrierSW ? "GMAN_D" : "GMAN_S";
        }
    }

    switch (mState.simCarrier)
    {
    case SimCarrier::DOCOMO:
        return "DOCOMO";
    case SimCarrier::SOFTBANK:
        return "SBANK";
    case SimCarrier::KDDI:
        return "KDDI";
    default:
        return "UNKNOWN";
    }
}

/////////////
