// SerialCommand.cpp
#include "SerialCommand.hpp"
#include "Debug.h"
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>   // キューサポート
#include "FreeRTOS_cbx.hpp"

// ------------------------------------------------------------
// Static members
// ------------------------------------------------------------
QueueHandle_t SerialCommand::serialCmdQueue = nullptr;

// -----------------------------------------------------------------
// キュー取得インターフェイス（メイン側で使用）
// -----------------------------------------------------------------
QueueHandle_t SerialCommand::getQueue() {
    return serialCmdQueue;
}

// -----------------------------------------------------------------
// コンストラクタ – キュー生成 + タスク生成
// -----------------------------------------------------------------
SerialCommand::SerialCommand() {
    // キュー作成（まだ作成されていなければ）
    if (serialCmdQueue == nullptr) {
        // 10 メッセージ、各 64 文字 + '\0' (65 バイト)
        serialCmdQueue = xQueueCreate(10, 65);
    }
    // 低優先度タスクでシリアル受信処理を開始
    xTaskCreateUniversal(
        &SerialCommand::taskLoop,
        "SerialCmdTask",
        4096,
        this,
        SERIALCMD_PRIORITY,
        &taskHandle,
        tskNO_AFFINITY);
}

// -----------------------------------------------------------------
// デストラクタ – タスク・キューのクリーンアップ
// -----------------------------------------------------------------
SerialCommand::~SerialCommand() {
    if (taskHandle != nullptr) {
        vTaskDelete(taskHandle);
        taskHandle = nullptr;
    }
    if (serialCmdQueue != nullptr) {
        vQueueDelete(serialCmdQueue);
        serialCmdQueue = nullptr;
    }
}

// -----------------------------------------------------------------
// 現在は使用しない（コマンドはメイン側で処理）
// -----------------------------------------------------------------
void SerialCommand::process() {
    // intentionally left empty
}

// -----------------------------------------------------------------
// タスク本体 – 受信文字列をキューへ送信
// -----------------------------------------------------------------
void SerialCommand::taskLoop(void* param) {
    SerialCommand* self = static_cast<SerialCommand*>(param);
    String inputBuffer = "";
    uint32_t lastCharTimeMs = 0;
    char lastChar = 0;

    while (true) {
        bool charRead = false;
        // 受信文字をすべて読む
        while (SerialDebug.available() > 0) {
            char c = SerialDebug.read();
            charRead = true;
            lastCharTimeMs = millis();
            // バックスペース処理 (BS / DEL)
            if (c == 0x08 || c == 0x7F) {
                if (inputBuffer.length() > 0) {
                    inputBuffer.remove(inputBuffer.length() - 1);
                    SerialDebug.print("\b \b"); // モニタ上で文字削除
                }
                lastChar = c;
                continue;
            }

            // 行終端 (CR, LF, CRLF) の処理
            if (c == '\n' || c == '\r') {
                // CRLF の LF を無視
                if (c == '\n' && lastChar == '\r') {
                    lastChar = c;
                    continue;
                }
                // 入力バッファをトリムし、コマンドを正規化してからキューへ送信
                inputBuffer.trim();
                if (inputBuffer.length() > 0) {
                    // 余計な空白や大文字を除去し、コマンド名を小文字統一
                    String tempCmd = String(inputBuffer);
                    tempCmd.trim();
                    tempCmd.toLowerCase();

                    if (serialCmdQueue != nullptr) {
                        char msg[65] = {0};
                        tempCmd.toCharArray(msg, 65);
                        xQueueSend(serialCmdQueue, msg, 0);
                    }
                    inputBuffer = "";
                }
            } else {
                // 通常文字 – バッファが満杯でなければ追加
                if (inputBuffer.length() < 64) {
                    inputBuffer += c;
                } else {
                    SerialDebug.println("\n[ERROR] Command Buffer Overflow (>64 chars). Buffer cleared.");
                    inputBuffer = "";
                    lastChar = 0;
                }
            }
            lastChar = c;
        }

        // タイムアウト処理（CR/LF が無いまま 200ms 待ち）
        if (!charRead && inputBuffer.length() > 0) {
            if (millis() - lastCharTimeMs > 200) {
                cbx3_log(LOG_ERR, "Command Timeout (no CR/LF) - Dropped buffer: \"%s\"\n", inputBuffer.c_str());
                inputBuffer = "";
                lastChar = 0;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    vTaskDelete(nullptr);
}
