// UARTモデムの共通基盤クラス
// 517Factory

// #include "ModemController.hpp"
#include "BaseUartModem.hpp"
#include "Debug.h"

// extern ModemController modem;

// コンストラクタ
BaseUartModem::BaseUartModem(HardwareSerial *uart, uint16_t sendQueueLength, uint16_t rawReceiveQueueLength) : _uart(uart)
{
    // 受信キューの作成 modemDataPacket のサイズと必要なキュー長を指定
    _resQueue = xQueueCreate(10, sizeof(modemDataPacket));
    _urcQueue = xQueueCreate(10, sizeof(modemDataPacket));
    _sendQueue = xQueueCreate(sendQueueLength, sizeof(char *));
    _rawReceiveQueue = xQueueCreate(rawReceiveQueueLength, sizeof(RawDataItem_t));
    _modemSemaphore = xSemaphoreCreateRecursiveMutex(); // モデム排他制御用セマフォ

    if (_resQueue == nullptr || _urcQueue == nullptr)
    {
        // キュー作成失敗時
        cbx3_log(LOG_ERR, "FATAL: Failed to create FreeRTOS Queues!");
        cbx_restart(BootReason::QUEUE_FAIL);
    }
}

bool BaseUartModem::connectNetwork()
{
    // return true; // バイパスTEST

    cbx3_log(MDBG3, "↓↓↓↓↓↓↓↓↓↓↓CHECK CONNECTION↓↓↓↓↓↓↓↓↓↓");
    // LTE接続確認(CEREG)
    if (!this->chkLteConnection())
    {
        cbx3_log(LOG_WAR, "LTE NETWORK FAIL(CEREG)");
        if (this->isLteConnected) // 接続→未接続に変わった時
        {
            this->isLteConnected = false;
            // SendDataLogMsg("LTE DISCONNECTED.");
        }
        return false; // 一旦falseを返す。自動再接続されるはず。
    }
    else
    {
        if (!this->isLteConnected) // 未接続→接続に変わった時
        {
            this->isLteConnected = true;
            // SendDataLogMsg("LTE CONNECTED.");
        }
    }

    // PDP Connectionの確認
    if (!this->chkPdpConnection())
    {
        cbx3_log(LOG_WAR, "PDP CONNECTION FAIL(CNACT)");
        if (this->isPdpConnected) // 接続→未接続に変わった時
        {
            this->isPdpConnected = false;
            this->wasPdpReset = true;
            // SendDataLogMsg("PDP DISCONNECTED.");
        }
        if (this->activatePdpConnection())
        {
            this->isPdpConnected = true;
        }
        else
        {
            return false;
        }
    }
    else
    {
        if (!this->isPdpConnected) // 未接続→接続に変わった時
        {
            this->isPdpConnected = true;
            // SendDataLogMsg("PDP CONNECTED.");
        }
    }

    // MQTT接続確認(SMSTATE)
    MODEM_RESULT result = this->chkMqtt();
    // cbx3_log(MDBG3, "CHECK MQTT CONNECTION.[%d]", (int)result);
    if (result != MODEM_RESULT::M_OK)
    {
        if (this->connectMqttNetwork())
        {
            cbx3_log(LOG_INF, "MQTT Connected successfully inside connectNetwork.");
            return true;
        }
        return false;
    }
    // ここでのtrueは問い合わせの成功のみ。接続の成功ではない。
    // ここでは問い合わせのみ行う。結果に対する処理はURCから行う（URCでも通知が来る場合があるため）
    cbx3_log(MDBG3, "↑↑↑↑↑↑↑↑↑↑↑CHECK CONNECTION↑↑↑↑↑↑↑↑↑↑");
    return true;
}

bool BaseUartModem::checkNetwork()
{
    // ここではMQTTの接続確認のみ行う

    // return true; // バイパスTEST

    // cbx3_log(MDBG3, "↓↓↓↓↓↓↓↓↓↓↓CHECK CONNECTION↓↓↓↓↓↓↓↓↓↓");

    // MQTT接続確認(SMSTATE)
    MODEM_RESULT result = this->chkMqtt();
    // cbx3_log(MDBG3, "CHECK MQTT CONNECTION.[%d]", (int)result);
    if (result == MODEM_RESULT::M_OK)
    {
        cbx3_log(MDBG3, "MQTT:OK");
        return true;
    }
    else
    {
        cbx3_log(MDBG3, "MQTT:FAIL");
        return false;
    }
}

// シンプルにATコマンドを送るだけのヘルパーメソッド
void BaseUartModem::sendAtCommand(const String &cmd)
{
    _uart->print(cmd + "\r\n");
}

// シンプルに生データを受信する
RawDataItem_t BaseUartModem::readRawData()
{
    RawDataItem_t item = {}; // 構造体を初期化

    if (!_uart || !_uart->available()) //_uart->available() が 0 の場合は即座にリターン
    {
        return item; // 空の構造体を返す
    }

    size_t availableBytes = _uart->available();
    size_t maxSizeToRead = MAX_UART_READ_SIZE - 1;

    if (availableBytes > maxSizeToRead) // 受信サイズオーバー警告
    {
        cbx3_log(LOG_WAR, "UART buffer overflow warning! Available: %u bytes, Reading only: %u bytes.", availableBytes, maxSizeToRead);
        // 警告が出ても処理は続行し、最大サイズ分だけ読み込む
    }

    size_t len = _uart->readBytes(item.data, maxSizeToRead);
    item.data[len] = '\0';
    item.len = len;

    return item;
}

String BaseUartModem::cleanSegment(const String &raw_response)
{
    String processed_response = raw_response;

    //{}内のCR/LFを'%'に置換(JSON内部で分割させないため)
    int start_brace = -1;
    // 文字列を走査
    for (int i = 0; i < processed_response.length(); i++)
    {
        char current_char = processed_response.charAt(i);

        if (current_char == '{')
        {
            // 開始ブレースを記録
            start_brace = i;
        }
        else if (current_char == '}')
        {
            // 終了ブレース。start_braceをリセット
            start_brace = -1;
        }
        else if (start_brace != -1)
        {
            // {} の内側にある場合のみ置換処理を実行
            if (current_char == '\r' || current_char == '\n')
            {
                processed_response.setCharAt(i, '%');
            }
        }
    }

    processed_response.replace("\r\n", "$"); // CRLFを$に置換
    // processed_response.replace("\r", "#");
    processed_response.replace("\r", ""); // LFは消す
    processed_response.replace("\n", "$");
    processed_response.trim(); // 最後にスペースがある場合の処理としてTrimしておく
    return processed_response;
}

// CRでつながっているRAWメッセージをCR分割し、各メッセージをQueueに流す
void BaseUartModem::splitAndQueueMessage(const RawDataItem_t &item)
{
    String incoming = String(item.data, item.len);
    if (_pendingPartialLine.length() > 0)
    {
        incoming = _pendingPartialLine + incoming;
        _pendingPartialLine = "";
    }

    String processed_response = cleanSegment(incoming);

    if (processed_response.length() == 0)
    {
        return; // データが空なら終了
    }

    int start_index = 0; // 分割文字列開始位置
    int end_index = 0;   // 分割文字列終了位置

    // 最後の文字が$もしくは#かどうかを確認する
    char last_char = processed_response.charAt(processed_response.length() - 1);
    if (last_char != '$' && last_char != '#') // きちんと終端されていない（最終文字が$でも#でもない）
    {
        if (processed_response.endsWith(">"))
        {
            // プロンプトの場合、後の分割処理に乗せるため、デリミタ($)を付加して通常処理へ
            processed_response += "$";
        }
        else
        {
            // 終端デリミタなし -> 次回のデータ受信時まで末尾の未完成行を保持する
            int lastDollar = processed_response.lastIndexOf('$');
            if (lastDollar != -1)
            {
                _pendingPartialLine = processed_response.substring(lastDollar + 1);
                processed_response = processed_response.substring(0, lastDollar + 1);
            }
            else
            {
                _pendingPartialLine = processed_response;
                return;
            }
        }
    }

    //$で分割してQueueに投入
    while ((end_index = processed_response.indexOf('$', start_index)) != -1) // CRを探す
    {
        String segment = processed_response.substring(start_index, end_index); // CR位置で分割
        // String trimmed_line = trimResponse2(segment);                          // Trim（$/#削除）※このTrimいらないかもしれない
        String trimmed_line = segment;
        if (!trimmed_line.isEmpty()) // 分割した電文をQueに送る
        {
            cbx3_log(MDBG1, "RCV>> : [%s]", trimmed_line.c_str());
            modemDataPacket pkt = this->processResponse(trimmed_line); // 電文ごとに処理してmodemDataPacket構造体に格納

            if (pkt.requiresExecution) // 実行要求があったら次のQueueに送る
            {
                // QueueHandle_t targetQueue = (pkt.ResType == ResponseType::URC) ? _urcQueue : _resQueue; // URCとRESでターゲットQUEを分ける
                // const char *queueName = (pkt.ResType == ResponseType::URC) ? "URC Queue" : "RES Queue";

                if (xQueueSend(_urcQueue, &pkt, 0) != pdPASS)
                {
                    // 投入失敗時
                    cbx3_log(LOG_WAR, "UrcQueue full. Packet lost: %s", pkt.message);
                }
                else
                {
                    UBaseType_t currentCount = uxQueueMessagesWaiting(_urcQueue);
                    // cbx3_log(LOG_DBG, "Send UrcQueue Success. CT : %u/10. Msg: %s", currentCount, pkt.message);
                }
            }
            else
            {
                // cbx3_log(LOG_INF, "NO-EXEXC");
            }
        }
        else
        {
            // cbx3_log(LOG_WAR, "EmptyLine");
        }
        start_index = end_index + 1;
    }
}

bool BaseUartModem::lockModem(uint32_t ticksToWait)
{
    if (_modemSemaphore) {
        return xSemaphoreTakeRecursive(_modemSemaphore, ticksToWait) == pdTRUE;
    }
    return false;
}

void BaseUartModem::unlockModem()
{
    if (_modemSemaphore) {
        xSemaphoreGiveRecursive(_modemSemaphore);
    }
}

void BaseUartModem::startMonitorTask(uint8_t priority, uint8_t cpu_core)
{
    _monitorPriority = priority;
    xTaskCreatePinnedToCore(
        modemMonitorTaskWrapper,
        "modemMonitorTask",
        4096,
        this,
        _monitorPriority,
        &_modemMonitorTask_hdl,
        cpu_core
    );
}

void BaseUartModem::modemMonitorTaskWrapper(void *pvParameters)
{
    BaseUartModem *instance = static_cast<BaseUartModem *>(pvParameters);
    while (true)
    {
        vTaskDelay(pdMS_TO_TICKS(MODEM_RECHECK_DELAY_MS_DEFAULT)); // 監視間隔
        
        while (true)
        {
            bool isConnected = false;
            if (instance->lockModem(portMAX_DELAY))
            {
                isConnected = instance->checkNetwork();
                instance->unlockModem();
            }
            
            if (isConnected)
            {
                break; // 正常なら抜ける
            }
            else // 切断時
            {
                if (instance->_mqttStateCallback) {
                    instance->_mqttStateCallback(MqttConnectType::DISCONNECTED);
                }
                // _mqttStateCallback内で再接続が完了するまでブロックされる想定。
                // 呼び出し中はMutexを解放しているため、別タスクでのURC受信処理が稼働できます。
            }
        }
    }
}

bool BaseUartModem::enqueueSendMessage(const char *msg, uint32_t timeout_ms)
{
    if (!_sendQueue) return false;

    char *dataToSend = new char[strlen(msg) + 1];
    strcpy(dataToSend, msg);

    if (xQueueSend(_sendQueue, &dataToSend, pdMS_TO_TICKS(timeout_ms)) != pdPASS)
    {
        delete[] dataToSend;
        return false;
    }
    return true;
}

uint16_t BaseUartModem::getSendQueueWaitingCount()
{
    if (_sendQueue) {
        return uxQueueMessagesWaiting(_sendQueue);
    }
    return 0;
}

void BaseUartModem::startSendTask(uint8_t priority, uint8_t cpu_core)
{
    _sendPriority = priority;
    xTaskCreatePinnedToCore(
        sendTaskWrapper,
        "sendTask",
        4096,
        this,
        _sendPriority,
        &_sendTask_hdl,
        cpu_core
    );
}

void BaseUartModem::sendTaskWrapper(void *pvParameters)
{
    BaseUartModem *instance = static_cast<BaseUartModem *>(pvParameters);
    char *data;

    while (true)
    {
        if (xQueueReceive(instance->_sendQueue, &data, pdMS_TO_TICKS(100)) == pdFALSE)
        {
            continue;
        }

        const String temp_message(data);

        if (instance->lockModem(portMAX_DELAY))
        {
            if (instance->_txLedCallback) {
                instance->_txLedCallback();
            }

            bool success = instance->sendFsMessage(temp_message);

            if (success)
            {
                delete[] data;
            }
            else
            {
                if (xQueueSendToFront(instance->_sendQueue, &data, 0) != pdPASS)
                {
                    delete[] data;
                }
                
                // コールバックを呼ぶ前に必ずMutexを解放する（connectNetwork()呼び出し等でデッドロックするのを防ぐ）
                instance->unlockModem();

                // 送信失敗したその瞬間の正確なタイムコードで切断ログをキューイングするため、即座にコールバックを実行する
                if (instance->_mqttStateCallback) {
                    instance->_mqttStateCallback(MqttConnectType::DISCONNECTED);
                }

                vTaskDelay(pdMS_TO_TICKS(5000)); // 失敗時は5秒待機して他タスクの割り込み枠を作る
                continue;
            }
            instance->unlockModem();
        }
    }
}

void BaseUartModem::startReceiveTasks(uint8_t receive_priority, uint8_t raw_handler_priority, uint8_t urc_handler_priority, uint8_t cpu_core)
{
    _receivePriority = receive_priority;
    _handlerPriority = raw_handler_priority;
    _urcHandlerPriority = urc_handler_priority;

    xTaskCreatePinnedToCore(
        receiveTaskWrapper,
        "receiveTask",
        8192,
        this,
        _receivePriority,
        &_receiveTask_hdl,
        cpu_core
    );

    xTaskCreatePinnedToCore(
        rawDataHandlerTaskWrapper,
        "rawDataHandlerTask",
        8192,
        this,
        _handlerPriority,
        &_rawDataHandlerTask_hdl,
        cpu_core
    );

    xTaskCreatePinnedToCore(
        urcHandlerTaskWrapper,
        "urcHandlerTask",
        8192,
        this,
        _urcHandlerPriority,
        &_urcHandlerTask_hdl,
        cpu_core
    );
}

void BaseUartModem::receiveTaskWrapper(void *pvParameters)
{
    BaseUartModem *instance = static_cast<BaseUartModem *>(pvParameters);

    while (true)
    {
        RawDataItem_t rawData = instance->readRawData();

        if (rawData.len > 0)
        {
            if (xQueueSend(instance->_rawReceiveQueue, &rawData, 0) != pdPASS)
            {
                cbx3_log(LOG_WAR, "Raw Receive Queue is full. Data lost: [%s]", rawData.data);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void BaseUartModem::rawDataHandlerTaskWrapper(void *pvParameters)
{
    BaseUartModem *instance = static_cast<BaseUartModem *>(pvParameters);
    RawDataItem_t receivedItem;

    while (true)
    {
        if (xQueueReceive(instance->_rawReceiveQueue, &receivedItem, portMAX_DELAY) == pdPASS)
        {
            instance->splitAndQueueMessage(receivedItem);
        }
    }
}

void BaseUartModem::urcHandlerTaskWrapper(void *pvParameters)
{
    BaseUartModem *instance = static_cast<BaseUartModem *>(pvParameters);
    modemDataPacket pkt;

    while (true)
    {
        if (xQueueReceive(instance->_urcQueue, &pkt, portMAX_DELAY) == pdTRUE)
        {
            if (instance->lockModem(portMAX_DELAY))
            {
                if (instance->_rxLedCallback) {
                    instance->_rxLedCallback();
                }
                if (instance->_modemDataReceiveCallback) {
                    instance->_modemDataReceiveCallback(pkt);
                }
                instance->unlockModem();
            }
        }
    }
}

void BaseUartModem::modemLog(ModemLogLevel level, const char* format, ...)
{
    if (_debugLogCallback)
    {
        char buffer[256];
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        _debugLogCallback(level, buffer);
    }
}

// String型をCharに代入するためのヘルパーメソッド
void BaseUartModem::setString2Char(char *dest, const String &src, size_t destSize)
{
    if (destSize == 0) return;
    strncpy(dest, src.c_str(), destSize - 1);
    dest[destSize - 1] = '\0';
}

bool BaseUartModem::wasPdpResetPerformed()
{
    return wasPdpReset;
}

void BaseUartModem::clearPdpResetFlag()
{
    wasPdpReset = false;
}

bool BaseUartModem::wasModemResetPerformed()
{
    return wasModemReset;
}

void BaseUartModem::clearModemResetFlag()
{
    wasModemReset = false;
}