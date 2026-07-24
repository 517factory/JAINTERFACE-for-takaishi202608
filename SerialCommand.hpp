// SerialCommand.hpp
#ifndef SERIALCOMMAND_HPP
#define SERIALCOMMAND_HPP

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>   // キューサポート

class SerialCommand {
public:
    SerialCommand();
    ~SerialCommand();

    // メイン側が取得できるキューへのアクセサ
    static QueueHandle_t getQueue();

    // 現在は未使用（コマンド処理はメイン側で行う）
    void process();
    static void executeSerialCommand(const String& cmd);

private:
    TaskHandle_t taskHandle;
    static QueueHandle_t serialCmdQueue; // キューはファイルスコープで保持
    static void taskLoop(void* param);
};


void serialCommandCallback(const char *cmd);
#endif // SERIALCOMMAND_HPP
