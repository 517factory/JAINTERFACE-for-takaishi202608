/*
Debug用関数

    517Factory
*/
#include "Debug.h"

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
}

void spDBGln(char *fmt, ...)
{
    char buf[LOGBUFSIZE]; // --- 展開文字列長に注意のこと
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, LOGBUFSIZE, fmt, args);
    SerialDebug.println(buf);
}

void cbx3_log(LogLevel level, const char *fmt, ...)
{
    char buf[LOGBUFSIZE];
    char log_level[4];
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
    case MDBG1:
        strcpy(log_level, "MD1");
        break;
    case MDBG2:
        strcpy(log_level, "MD2");
        break;
    case MDBG3:
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
    }
}

bool cbx3_errchk(LogLevel level, esp_err_t e, const char *txt)
{
    // char buf[LOGBUFSIZE]; // --- 展開文字列長に注意のこと
    char log_level[4];
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

void cbx3_memory_print(void)
{
    size_t heap_free = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    UBaseType_t stack_remaining = uxTaskGetStackHighWaterMark(NULL);

    String log_message = "[INF][HEAP_FREE : " + String(heap_free) + " / STACK_REMAINING : " + String(stack_remaining) + "]";
    SerialDebug.println(log_message);
}

char *replaceData4Disp(const char *buf)
{
    char *tmp_buf;
    tmp_buf = (char *)malloc(sizeof(char) * 256);

    // NULL終端文字でバッファを初期化する
    memset(tmp_buf, 0, 256);

    for (int i = 0; i < 256; i++)
    {
        if (buf[i] == 0x0a) // LF
            tmp_buf[i] = '$';
        else if (buf[i] == 0x0d) // CR
            tmp_buf[i] = '#';
        else if (buf[i] == 0x00) // NULL
        {
            tmp_buf[i] = '%';
            tmp_buf[i + 1] = '\0';
            break;
        }
        else if (buf[i] == '0' && buf[i + 1] == '0' && buf[i + 2] == '0' && buf[i + 3] == '0' && buf[i + 4] == '0') // ゼロ埋め
        {
            strcpy(&tmp_buf[i], "[ZERO PADDING]");
            break;
        }
        else
        {
            tmp_buf[i] = buf[i];
        }
    }
    return tmp_buf;
}

void cbx_sleep(const char *reason)
{
    cbx3_log(LOG_WAR, "DEEP SLEEP : [REASON] %s", reason);
    Serial.flush();
    esp_deep_sleep_start();
}

void setString2Char(char *dest, const String &src, size_t destSize)
{
    if (destSize == 0) return;
    strncpy(dest, src.c_str(), destSize - 1);
    dest[destSize - 1] = '\0';
}
