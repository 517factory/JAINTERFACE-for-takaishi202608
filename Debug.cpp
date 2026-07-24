/*
Debug用関数

    517Factory
*/
#include "Debug.h"
#include "cbx3_gpio.h"
#include "LEDCont.h"
#include <LittleFS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <Preferences.h>
#include <vector>
#include "TimeCode.hpp"

extern TimeCode timecode;
static int startLogIndex = 0;

#define LOG_QUEUE_SIZE 128
struct LogMessage {
    char text[LOGBUFSIZE];
};

static QueueHandle_t logQueue = NULL;
static SemaphoreHandle_t fileLogMutex = NULL;
static TaskHandle_t logWriteTaskHandle = NULL;
// static TaskHandle_t serialCmdTaskHandle = NULL;

static volatile bool loopStarted = false;
static volatile uint32_t loopStartTimeMs = 0;
static int lastLogIndex = -2;

void enqueueLogToFile(const char *logMsg)
{
    if (logQueue == NULL) return;
    
    LogMessage msg;
    strncpy(msg.text, logMsg, sizeof(msg.text) - 1);
    msg.text[sizeof(msg.text) - 1] = '\0';
    
    if (xQueueSend(logQueue, &msg, pdMS_TO_TICKS(50)) != pdTRUE)
    {
        SerialDebug.print("[LOG] Error: logQueue full, message dropped: ");
        SerialDebug.print(logMsg);
    }
}

int writeIndex = 0;
int readIndex = 0;


void spDBG(char *fmt, ...)
{
    char buf[LOGBUFSIZE]; // --- 展開文字列長に注意のこと
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, LOGBUFSIZE, fmt, args);
    va_end(args);
    SerialDebug.print(buf);
    enqueueLogToFile(buf);
}

void spDBGln(char *fmt, ...)
{
    char buf[LOGBUFSIZE]; // --- 展開文字列長に注意のこと
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, LOGBUFSIZE, fmt, args);
    va_end(args);
    SerialDebug.println(buf);
    
    char bufWithLf[LOGBUFSIZE + 2];
    snprintf(bufWithLf, sizeof(bufWithLf), "%s\n", buf);
    enqueueLogToFile(bufWithLf);
}

void cbx3_log(LogLevel level, const char *fmt, ...)
{
    // return;  //ログを全部消すとき
    char buf[LOGBUFSIZE];
    char log_level[6];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, LOGBUFSIZE, fmt, args);
    va_end(args);

    switch (level)
    {
    case LOG_ERR:
        strcpy(log_level, "ERR");
        break;
    case LOG_WAR:
        strcpy(log_level, "WAR");
        break;
    case LOG_INF:
        strcpy(log_level, "INF");
        break;
    case LOG_DBG:
        strcpy(log_level, "DBG");
        break;
    case LOG_TST:
        strcpy(log_level, "TST");
        break;
    case LogLevel::MDBG1:
        strcpy(log_level, "MD1");
        break;
    case LogLevel::MDBG2:
        strcpy(log_level, "MD2");
        break;
    case LogLevel::MDBG3:
        strcpy(log_level, "MD3");
        break;
    default:
        strcpy(log_level, "XXX");
        break;
    }

    if (level <= CBX3_LOG_LEVEL)
    {
        String log_message = "[" + String(log_level) + "] [" + String(buf) + "]";
        SerialDebug.println(log_message);
        cbx3_memory_check(); // メモリーチェック
        
        String file_msg = log_message + "\n";
        enqueueLogToFile(file_msg.c_str());
    }
}

bool cbx3_errchk(LogLevel level, esp_err_t e, const char *txt)
{
    // char buf[LOGBUFSIZE]; // --- 展開文字列長に注意のこと
    char log_level[6];
    char errbuf[LOGBUFSIZE];

    if (e != ESP_OK)
    {
        level = LOG_ERR;
    }

    switch (level)
    {
    case LOG_ERR:
        strcpy(log_level, "ERR");
        break;
    case LOG_WAR:
        strcpy(log_level, "WAR");
        break;
    case LOG_INF:
        strcpy(log_level, "INF");
        break;
    case LOG_DBG:
        strcpy(log_level, "DBG");
        break;
    case LOG_TST:
        strcpy(log_level, "TST");
        break;
    default:
        strcpy(log_level, "XXX");
        break;
    }

    esp_err_to_name_r(e, errbuf, sizeof(errbuf));

    if (level <= CBX3_LOG_LEVEL)
    {
        String log_message = "[" + String(log_level) + "] [" + String(txt) + "] [" + String(errbuf) + "]";
        SerialDebug.println(log_message);
        
        String file_msg = log_message + "\n";
        enqueueLogToFile(file_msg.c_str());
    }

    if (e)
    {
        return true;
    }
    else
    {
        return false;
    }
}

void cbx3_memory_check(void)
{
    size_t heap_free = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    UBaseType_t stack_remaining = uxTaskGetStackHighWaterMark(NULL);
    if (heap_free < HEAP_MEM_MIN || stack_remaining < STACK_MEM_MIN)
    {
        String log_message = "[WAR][HEAP_FREE : " + String(heap_free) + " / STACK_REMAINING : " + String(stack_remaining) + "]";
        SerialDebug.println(log_message);
    }
}

void cbx3_memory_check_always(void)
{
    size_t heap_free = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    UBaseType_t stack_remaining = uxTaskGetStackHighWaterMark(NULL);

    String log_message = "[WAR][HEAP_FREE : " + String(heap_free) + " / STACK_REMAINING : " + String(stack_remaining) + "]";
    SerialDebug.println(log_message);
}

void cbx3_memory_print(void)
{
    size_t heap_free = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    UBaseType_t stack_remaining = uxTaskGetStackHighWaterMark(NULL);

    String log_message = "[INF][HEAP_FREE : " + String(heap_free) + " / STACK_REMAINING : " + String(stack_remaining) + "]";
    SerialDebug.println(log_message);
}

String replaceData4Disp(const char *buf)
{
    if (!buf) return "";
    String result = "";
    result.reserve(256);

    for (int i = 0; i < 256; i++)
    {
        char c = buf[i];
        if (c == 0x00) // NULL
        {
            result += '%';
            break;
        }
        else if (c == 0x0a) // LF
            result += '$';
        else if (c == 0x0d) // CR
            result += '#';
        else if (i + 4 < 256 && buf[i] == '0' && buf[i + 1] == '0' && buf[i + 2] == '0' && buf[i + 3] == '0' && buf[i + 4] == '0') // ゼロ埋め
        {
            result += "[ZERO PADDING]";
            break;
        }
        else
        {
            result += c;
        }
    }
    return result;
}



// String型をCharに代入するためのヘルパーメソッド（あちこちで使いそうなのでここに置いておく）
void setString2Char(char *dest, const String &src, size_t destSize)
{
    if (destSize == 0)
        return;

    // srcの内容をC文字列として取得し、destSize-1までコピー
    // strncpy は自動でヌル終端しないため、サイズより1小さい分だけコピーする
    strncpy(dest, src.c_str(), destSize - 1);

    // 最後にヌル終端文字を必ず付加する
    dest[destSize - 1] = '\0';
}

static char currentLogPath[64] = "";
static bool currentLogPathIsUnsynced = false;
static size_t currentLogSize = 0;
static String logWriteBuffer = "";
static bool hasMqttConnectedDuringInterval = false;

// Forward declarations
void removeOldStartLog(int index);
void removeOldLogByIndex(int index);
void cleanupLogSpace();


void flushLogBuffer()
{
    if (logWriteBuffer.length() > 0 && currentLogPath[0] != '\0')
    {
        File f = LittleFS.open(currentLogPath, "a");
        if (f)
        {
            size_t written = f.print(logWriteBuffer);
            currentLogSize += written;
            f.close();
        }
        logWriteBuffer = "";
    }
}

void checkAndRotateLogFile()
{
    int currentLogIndex = -1;
    
    if (loopStarted)
    {
        uint32_t elapsedMs = millis() - loopStartTimeMs;
        uint32_t elapsedIntervals = elapsedMs / (FILE_LOG_INTERVAL_MIN * 60000);
        currentLogIndex = elapsedIntervals % MAX_ROTATION_LOGS;
    }
    else
    {
        currentLogIndex = -1;
    }
    
    if (currentLogIndex != lastLogIndex || currentLogPath[0] == '\0')
    {
        // 1. ファイルが切り替わる前に現在溜まっているバッファを書き出す
        flushLogBuffer();

        // 2. 切り替わる前のファイルでMQTT接続が一度も確立していなかった場合、一行だけのログに上書きする
        if (currentLogPath[0] != '\0' && strstr(currentLogPath, "_Start_") == NULL)
        {
            if (!hasMqttConnectedDuringInterval)
            {
                File f = LittleFS.open(currentLogPath, "w"); // Truncate and overwrite
                if (f)
                {
                    f.println("[ERR] [MQTT NOT CONNECTED]");
                    f.close();
                    SerialDebug.printf("[LOG] Overwrote disconnected log file %s with single line.\n", currentLogPath);
                }
            }
        }

        // フラグをクリア
        hasMqttConnectedDuringInterval = false;
        
        timecode.updateTime();
        int yr = timecode.currentTime.year % 100;
        int mo = timecode.currentTime.month;
        int dy = timecode.currentTime.day;
        int hr = timecode.currentTime.hour;
        int mn = timecode.currentTime.minute;
        
        char timeStr[16];
        snprintf(timeStr, sizeof(timeStr), "%02d%02d%02d%02d%02d", yr, mo, dy, hr, mn);
        
        if (currentLogIndex == -1)
        {
            removeOldStartLog(startLogIndex);
            snprintf(currentLogPath, sizeof(currentLogPath), "/Log%s_Start_%02d.log", timeStr, startLogIndex);
            currentLogPathIsUnsynced = (timecode.timeMode == ELPS_TIME);
        }
        else
        {
            removeOldLogByIndex(currentLogIndex);
            snprintf(currentLogPath, sizeof(currentLogPath), "/Log%s_%02d.log", timeStr, currentLogIndex);
        }
        
        // 新しいファイルを作成する前に空き容量のクリーンアップを実行
        cleanupLogSpace();
        
        File f = LittleFS.open(currentLogPath, "w");
        if (f)
        {
            f.close();
            SerialDebug.printf("[LOG] Created/Truncated log file: %s (unsynced=%d)\n", currentLogPath, currentLogPathIsUnsynced ? 1 : 0);
        }
        else
        {
            SerialDebug.printf("[LOG] Failed to create/truncate log file: %s\n", currentLogPath);
        }
        lastLogIndex = currentLogIndex;
        currentLogSize = 0;
    }
}


void removeOldStartLog(int index)
{
    char suffix[32];
    snprintf(suffix, sizeof(suffix), "_Start_%02d.log", index);
    
    while (true)
    {
        File root = LittleFS.open("/", "r");
        if (!root) return;
        
        String fileToDelete = "";
        File file = root.openNextFile();
        while (file)
        {
            String fileName = file.name();
            if (fileName.endsWith(suffix))
            {
                fileToDelete = fileName;
                if (!fileToDelete.startsWith("/"))
                {
                    fileToDelete = "/" + fileToDelete;
                }
                file.close();
                break;
            }
            file.close(); // 明示的にクローズしてリソース漏れを防止
            file = root.openNextFile();
        }
        root.close();
        
        if (fileToDelete != "")
        {
            LittleFS.remove(fileToDelete);
        }
        else
        {
            break;
        }
    }
}

void removeOldLogByIndex(int index)
{
    char suffix[16];
    snprintf(suffix, sizeof(suffix), "_%02d.log", index);
    
    while (true)
    {
        File root = LittleFS.open("/", "r");
        if (!root) return;
        
        String fileToDelete = "";
        File file = root.openNextFile();
        while (file)
        {
            String fileName = file.name();
            if (fileName.endsWith(suffix) && fileName.indexOf("Start") == -1)
            {
                fileToDelete = fileName;
                if (!fileToDelete.startsWith("/"))
                {
                    fileToDelete = "/" + fileToDelete;
                }
                file.close();
                break;
            }
            file.close(); // 明示的にクローズしてリソース漏れを防止
            file = root.openNextFile();
        }
        root.close();
        
        if (fileToDelete != "")
        {
            LittleFS.remove(fileToDelete);
        }
        else
        {
            break;
        }
    }
}

void cleanupLogSpace()
{
    const size_t lowSpaceThreshold = 150 * 1024; 
    
    while (true)
    {
        size_t total = LittleFS.totalBytes();
        size_t used = LittleFS.usedBytes();
        size_t freeSpace = (total > used) ? (total - used) : 0;
        
        size_t totalLogSize = 0;
        String oldestFileName = "";
        
        File root = LittleFS.open("/", "r");
        if (!root) break;
        
        File file = root.openNextFile();
        while (file)
        {
            if (!file.isDirectory())
            {
                String name = file.name();
                if (!name.startsWith("/"))
                {
                    name = "/" + name;
                }
                
                if (name.startsWith("/Log") && name.endsWith(".log"))
                {
                    totalLogSize += file.size();
                    
                    // 現在書き込み中のファイルは除外
                    if (name != currentLogPath)
                    {
                        if (oldestFileName == "" || name < oldestFileName)
                        {
                            oldestFileName = name;
                        }
                    }
                }
            }
            file.close(); // 明示的にクローズしてリソース漏れを防止
            file = root.openNextFile();
        }
        root.close();
        
        // 空き容量が十分あり、かつ、ログファイルの合計サイズが上限以下ならクリーンアップ終了
        if (freeSpace >= lowSpaceThreshold && totalLogSize <= MAX_LOG_TOTAL_SIZE)
        {
            break;
        }
        
        if (oldestFileName != "")
        {
            SerialDebug.printf("[LOG] Cleanup: freeSpace=%u, totalLogSize=%u/%u. Deleting oldest log file: %s\n", 
                               freeSpace, totalLogSize, MAX_LOG_TOTAL_SIZE, oldestFileName.c_str());
            LittleFS.remove(oldestFileName);
        }
        else
        {
            SerialDebug.printf("[LOG] No more log files to delete. freeSpace=%u, totalLogSize=%u/%u\n", 
                               freeSpace, totalLogSize, MAX_LOG_TOTAL_SIZE);
            break;
        }
    }
}

void logWriteTask(void *pvParameters)
{
    LogMessage msg;
    while (1)
    {
        if (xQueueReceive(logQueue, &msg, portMAX_DELAY) == pdTRUE)
        {
            if (fileLogMutex != NULL && xSemaphoreTake(fileLogMutex, portMAX_DELAY) == pdTRUE)
            {
                // 現在の時刻に基づいてローテーションチェックとバッファ書き出しを行う
                checkAndRotateLogFile();
                
                // If we are writing to the start log, and the timecode has become synchronized,
                // and the current path still uses the un-synchronized time prefix, rename the file.
                if (lastLogIndex == -1 && currentLogPathIsUnsynced && timecode.timeMode != ELPS_TIME)
                {
                    // リネーム前に現在溜まっているバッファを一旦古いファイル名に書き出す
                    flushLogBuffer();
                    
                    timecode.updateTime();
                    int yr = timecode.currentTime.year % 100;
                    int mo = timecode.currentTime.month;
                    int dy = timecode.currentTime.day;
                    int hr = timecode.currentTime.hour;
                    int mn = timecode.currentTime.minute;
                    
                    char timeStr[16];
                    snprintf(timeStr, sizeof(timeStr), "%02d%02d%02d%02d%02d", yr, mo, dy, hr, mn);
                    
                    char newLogPath[64];
                    snprintf(newLogPath, sizeof(newLogPath), "/Log%s_Start_%02d.log", timeStr, startLogIndex);
                    
                    if (timecode.currentTime.year > 0 && strcmp(currentLogPath, newLogPath) != 0)
                    {
                        SerialDebug.printf("[LOG] Syncing start log name. Renaming from %s to %s\n", currentLogPath, newLogPath);
                        if (LittleFS.exists(currentLogPath))
                        {
                            if (LittleFS.exists(newLogPath))
                            {
                                SerialDebug.printf("[LOG] Removing existing destination file: %s\n", newLogPath);
                                LittleFS.remove(newLogPath);
                            }
                            if (LittleFS.rename(currentLogPath, newLogPath))
                            {
                                SerialDebug.printf("[LOG] Rename successful.\n");
                                strncpy(currentLogPath, newLogPath, sizeof(currentLogPath) - 1);
                                currentLogPath[sizeof(currentLogPath) - 1] = '\0';
                                currentLogPathIsUnsynced = false;
                            }
                            else
                            {
                                SerialDebug.printf("[LOG] Rename failed!\n");
                            }
                        }
                        else
                        {
                            SerialDebug.printf("[LOG] Rename skipped: source file %s does not exist!\n", currentLogPath);
                        }
                    }
                }
                
                if (currentLogPath[0] != '\0')
                {
                    if (currentLogSize + logWriteBuffer.length() < MAX_SINGLE_LOG_SIZE)
                    {
                        logWriteBuffer += msg.text;
                        
                        // バッファサイズが4KB（4096バイト）を超えたらファイルへフラッシュする
                        if (logWriteBuffer.length() >= 4096)
                        {
                            flushLogBuffer();
                            
                            if (currentLogSize >= MAX_SINGLE_LOG_SIZE)
                            {
                                File f = LittleFS.open(currentLogPath, "a");
                                if (f)
                                {
                                    const char* warningFooter = "\n[LOG] Warning: Single log file size limit exceeded. Logging suspended for this interval.\n";
                                    f.print(warningFooter);
                                    currentLogSize += strlen(warningFooter);
                                    f.close();
                                }
                                SerialDebug.printf("[LOG] Warning: Log file %s reached size limit. Logging suspended for this interval.\n", currentLogPath);
                            }
                        }
                    }
                }
                
                xSemaphoreGive(fileLogMutex);
            }
        }
    }
}

void readLogFile(const String& fileName)
{
    if (fileLogMutex != NULL && xSemaphoreTake(fileLogMutex, portMAX_DELAY) == pdTRUE)
    {
        checkAndRotateLogFile();
        flushLogBuffer();
        if (LittleFS.exists(fileName))
        {
            File f = LittleFS.open(fileName, "r");
            if (f)
            {
                SerialDebug.printf("--- START OF FILE: %s (%d bytes) ---\n", fileName.c_str(), f.size());
                char readBuf[128];
                while (f.available() > 0)
                {
                    int bytesRead = f.readBytes(readBuf, sizeof(readBuf));
                    SerialDebug.write((uint8_t*)readBuf, bytesRead);
                    vTaskDelay(pdMS_TO_TICKS(5));
                }
                SerialDebug.printf("\n--- END OF FILE: %s ---\n", fileName.c_str());
                f.close();
            }
            else
            {
                SerialDebug.printf("Failed to open file: %s\n", fileName.c_str());
            }
        }
        else
        {
            SerialDebug.printf("File not found: %s\n", fileName.c_str());
        }
        xSemaphoreGive(fileLogMutex);
    }
}

bool executeSerialCommand(const String& cmd)
{
    String tempCmd = cmd;
    tempCmd.trim();
    bool ret = true;
    
    int spaceIndex = tempCmd.indexOf(' ');
    String commandName = (spaceIndex == -1) ? tempCmd : tempCmd.substring(0, spaceIndex);
    String arguments = (spaceIndex == -1) ? "" : tempCmd.substring(spaceIndex + 1);
    arguments.trim();

    commandName.toLowerCase();

    if (commandName == "logls")  //ファイルリストの表示
    {
        if (fileLogMutex != NULL && xSemaphoreTake(fileLogMutex, portMAX_DELAY) == pdTRUE)
        {
            checkAndRotateLogFile();
            flushLogBuffer();
            SerialDebug.println("--- Log File List ---");
            File root = LittleFS.open("/", "r");
            if (root)
            {
                struct LogFileInfo {
                    String name;
                    size_t size;
                };
                std::vector<LogFileInfo> files;
                files.reserve(MAX_LOG_FILE_LIST_SIZE);
                
                File file = root.openNextFile();
                while (file && files.size() < MAX_LOG_FILE_LIST_SIZE)
                {
                    String name = file.name();
                    if (!name.startsWith("/"))
                    {
                        name = "/" + name;
                    }
                    
                    if (name.startsWith("/Log") && name.endsWith(".log"))
                    {
                        files.push_back({name, file.size()});
                    }
                    file = root.openNextFile();
                }
                
                if (!files.empty())
                {
                    for (size_t i = 0; i < files.size() - 1; i++)
                    {
                        for (size_t j = 0; j < files.size() - i - 1; j++)
                        {
                            if (files[j].name > files[j+1].name)
                            {
                                LogFileInfo temp = files[j];
                                files[j] = files[j+1];
                                files[j+1] = temp;
                            }
                        }
                    }
                }
                
                for (size_t i = 0; i < files.size(); i++)
                {
                    SerialDebug.printf("  %s : %d bytes\n", files[i].name.c_str(), files[i].size);
                }
            }
            SerialDebug.println("---------------------");
            xSemaphoreGive(fileLogMutex);
        }
    }
    else if (commandName == "logdelall") //全ファイル削除
    {
        if (fileLogMutex != NULL && xSemaphoreTake(fileLogMutex, portMAX_DELAY) == pdTRUE)
        {
            SerialDebug.println("--- Deleting All Log Files ---");
            File root = LittleFS.open("/", "r");
            if (root)
            {
                std::vector<String> filesToDelete;
                filesToDelete.reserve(MAX_LOG_FILE_LIST_SIZE);
                
                File file = root.openNextFile();
                while (file && filesToDelete.size() < MAX_LOG_FILE_LIST_SIZE)
                {
                    String name = file.name();
                    if (!name.startsWith("/"))
                    {
                        name = "/" + name;
                    }
                    
                    if (name.startsWith("/Log") && name.endsWith(".log"))
                    {
                        filesToDelete.push_back(name);
                    }
                    file = root.openNextFile();
                }
                
                for (size_t i = 0; i < filesToDelete.size(); i++)
                {
                    if (LittleFS.remove(filesToDelete[i]))
                    {
                        SerialDebug.printf("  Deleted: %s\n", filesToDelete[i].c_str());
                    }
                    else
                    {
                        SerialDebug.printf("  Failed to delete: %s\n", filesToDelete[i].c_str());
                    }
                }
            }
            currentLogPath[0] = '\0';
            lastLogIndex = -2;
            logWriteBuffer = "";
            SerialDebug.println("------------------------------");
            xSemaphoreGive(fileLogMutex);
        }
    }
    else if (commandName == "logcat")   //Logの表示
    {
        if (arguments.length() == 0)
        {
            SerialDebug.println("[ERROR] Usage: logcat <filename>");
            return false;
        }
        
        String fileName = arguments;
        if (!fileName.startsWith("/"))
        {
            fileName = "/" + fileName;
        }
        readLogFile(fileName);
    }
    else if (commandName == "logdel")   //Logの削除
    {
        if (arguments.length() == 0)
        {
            SerialDebug.println("[ERROR] Usage: logdel <filename>");
            return false;
        }
        
        String fileName = arguments;
        if (!fileName.startsWith("/"))
        {
            fileName = "/" + fileName;
        }
        
        if (fileLogMutex != NULL && xSemaphoreTake(fileLogMutex, portMAX_DELAY) == pdTRUE)
        {
            if (LittleFS.exists(fileName))
            {
                if (LittleFS.remove(fileName))
                {
                    SerialDebug.printf("  Deleted: %s\n", fileName.c_str());
                    // 削除したファイルが現在書き込み中のログファイルの場合、パス情報を初期化
                    if (fileName.equalsIgnoreCase(currentLogPath))
                    {
                        currentLogPath[0] = '\0';
                        lastLogIndex = -2;
                        logWriteBuffer = "";
                    }
                }
                else
                {
                    SerialDebug.printf("  Failed to delete: %s\n", fileName.c_str());
                    ret = false;
                }
            }
            else
            {
                SerialDebug.printf("  File not found: %s\n", fileName.c_str());
                ret = false;
            }
            xSemaphoreGive(fileLogMutex);
        }
    }
    else if (commandName == "loghelp")   //ヘルプの表示
    {
        SerialDebug.println("--- Log Commands Help ---");
        SerialDebug.println("  logls             : List all log files");
        SerialDebug.println("  logcat <filename> : Display the content of a specific log file");
        SerialDebug.println("  logdel <filename> : Delete a specific log file");
        SerialDebug.println("  logdelall         : Delete all log files");
        SerialDebug.println("  loghelp           : Show this help message");
        SerialDebug.println("-------------------------");
    }
    else
    {
        // SerialDebug.printf("[ERROR] Unknown Command: %s\n", commandName.c_str());
        ret = false;
    }
    return ret;
}


// void serialCmdTask(void *pvParameters)
// {
//     String inputBuffer = "";
//     uint32_t lastCharTimeMs = 0;
//     char lastChar = 0;
    
//     while (1)
//     {
//         bool charRead = false;
//         while (SerialDebug.available() > 0)
//         {
//             char c = SerialDebug.read();
//             charRead = true;
//             lastCharTimeMs = millis();
            
//             // バックスペース (BS: 0x08 / DEL: 0x7F) の処理
//             if (c == 0x08 || c == 0x7F)
//             {
//                 if (inputBuffer.length() > 0)
//                 {
//                     inputBuffer.remove(inputBuffer.length() - 1);
//                     SerialDebug.print("\b \b"); // モニター上で1文字消すエコーバック
//                 }
//                 lastChar = c;
//                 continue;
//             }
            
//             if (c == '\n' || c == '\r')
//             {
//                 // CR, LF, CRLFどれでもOKにするため、CRLFペアの場合は2つ目のLFを無視する
//                 if (c == '\n' && lastChar == '\r')
//                 {
//                     lastChar = c;
//                     continue;
//                 }
                
//                 inputBuffer.trim();
//                 if (inputBuffer.length() > 0)
//                 {
//                     executeSerialCommand(inputBuffer);
//                     inputBuffer = "";
//                 }
//             }
//             else
//             {
//                 if (inputBuffer.length() < 64)
//                 {
//                     inputBuffer += c;
//                 }
//                 else
//                 {
//                     SerialDebug.println("\n[ERROR] Command Buffer Overflow (>64 chars). Buffer cleared.");
//                     inputBuffer = "";
//                     lastChar = 0;
//                 }
//             }
//             lastChar = c;
//         }
        
//         // 終了コード（CR/LF）が来ずに無入力の状態が200ms続いた場合はタイムアウトエラーとする
//         if (!charRead && inputBuffer.length() > 0)
//         {
//             if (millis() - lastCharTimeMs > 200)
//             {
//                 SerialDebug.printf("[ERROR] Command Timeout (no CR/LF) - Dropped buffer: \"%s\"\n", inputBuffer.c_str());
//                 inputBuffer = "";
//                 lastChar = 0;
//             }
//         }
        
//         vTaskDelay(pdMS_TO_TICKS(50));
//     }
// }

void cbx3_file_log_init(void)
{
    if (logQueue != NULL) return;
    
    logWriteBuffer.reserve(4096 + 256);
    
    // 起動時にディスク容量のクリーンアップを実行してゾンビファイルなどを一掃する
    cleanupLogSpace();
    
    Preferences prefs;
    prefs.begin("file_log", false);
    startLogIndex = prefs.getInt("start_idx", 0);
    if (startLogIndex < 0 || startLogIndex >= 3)
    {
        startLogIndex = 0;
    }
    prefs.putInt("start_idx", (startLogIndex + 1) % 3);
    prefs.end();
    
    fileLogMutex = xSemaphoreCreateMutex();
    logQueue = xQueueCreate(LOG_QUEUE_SIZE, sizeof(LogMessage));
    
    if (logQueue != NULL && fileLogMutex != NULL)
    {
        xTaskCreatePinnedToCore(
            logWriteTask,
            "logWriteTask",
            4096,
            NULL,
            2,
            &logWriteTaskHandle,
            APP_CPU_NUM
        );
        lastLogIndex = -1;
    }
}

// void cbx3_file_log_start_loop(void)
// {
//     loopStartTimeMs = millis();
//     loopStarted = true;
    
//     // if (serialCmdTaskHandle == NULL)
//     // {
//     //     xTaskCreatePinnedToCore(
//     //         serialCmdTask,
//     //         "serialCmdTask",
//     //         8192,
//     //         NULL,
//     //         1,
//     //         &serialCmdTaskHandle,
//     //         APP_CPU_NUM
//     //     );
//     // }
// }

void cbx3_file_log_flush(void)
{
    if (logQueue == NULL || fileLogMutex == NULL) return;
    
    // キューが空になるのを待つ（最大1秒）
    int timeout = 100; // 10ms * 100 = 1s
    while (uxQueueMessagesWaiting(logQueue) > 0 && timeout-- > 0)
    {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    // バッファをフラッシュ
    if (xSemaphoreTake(fileLogMutex, pdMS_TO_TICKS(500)) == pdTRUE)
    {
        flushLogBuffer();
        xSemaphoreGive(fileLogMutex);
    }
}

void setMqttConnectedFlag(bool connected)
{
    if (connected)
    {
        hasMqttConnectedDuringInterval = true;
    }
}

