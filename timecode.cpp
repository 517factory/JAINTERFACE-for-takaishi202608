/*
timecode for ESP32-S3

    517Factory
*/

#include "TimeCode.hpp"

TimeCode::TimeCode(void) : startMillis(0)
{
    // 初期値として年月日と時刻を0に設定
    initialTime.year = 0;
    initialTime.month = 0;
    initialTime.day = 0;
    initialTime.hour = 0;
    initialTime.minute = 0;
    initialTime.second = 0;
    initialTime.timezone = 0;
    currentTime = initialTime;
}

//+CCLKのアンサーからシステム時刻を設定
bool TimeCode::setLteTimeCode(const char *buff)
{
    // バッファから文字列を作成し、トリム
    char tempBuff[strlen(buff) + 1];
    strcpy(tempBuff, buff);
    String buffStr = String(tempBuff); // char * を String に変換
    buffStr.trim();

    // "+CCLK: " の位置を取得
    int startIdx = buffStr.indexOf("+CCLK: ");
    if (startIdx == -1)
    {
        cbx3_log(LOG_ERR, "Time code format error in buffer: %s", buff);
        return false;
    }

    // ダブルクォートで囲まれた部分を取り出す
    int firstQuoteIdx = buffStr.indexOf('"', startIdx);
    int secondQuoteIdx = buffStr.indexOf('"', firstQuoteIdx + 1);
    if (firstQuoteIdx == -1 || secondQuoteIdx == -1)
    {
        cbx3_log(LOG_INF, "Quote not found in buffer: %s", buff);
        return false;
    }

    String timeCode = buffStr.substring(firstQuoteIdx + 1, secondQuoteIdx);

    // タイムコードの形式が "YY/MM/DD,HH:MM:SS+TZ" と仮定します
    int year = timeCode.substring(0, 2).toInt();
    int month = timeCode.substring(3, 5).toInt();
    int day = timeCode.substring(6, 8).toInt();
    int hour = timeCode.substring(9, 11).toInt();
    int minute = timeCode.substring(12, 14).toInt();
    int second = timeCode.substring(15, 17).toInt();
    int timezone = timeCode.substring(18, 20).toInt();

    if (timeMode != ELPS_TIME)
    {
        // 現在時刻を更新
        updateTime();

        // 前回の時刻から計算された現在時刻を秒単位で計算
        int calculatedCurrentTimeInSeconds = currentTime.hour * 3600 + currentTime.minute * 60 + currentTime.second;
        // 新しいタイムコードの時刻を秒単位で計算
        // int newTimeCodeInSeconds = hour * 3600 + minute * 60 + second + timezone * 15 * 60;
        int newTimeCodeInSeconds = hour * 3600 + minute * 60 + second;
        // 差分を計算
        timeDifference = newTimeCodeInSeconds - calculatedCurrentTimeInSeconds;

        // 差分を表示
        cbx3_log(LOG_INF, "UPDATE TIMECODE(LTE TIME) : Timer Difference: %d seconds", timeDifference);
    }

    if (year != 80) // GPSがひろえていないとYEARが80になる
    {
        // cbx3_log(LOG_WAR, "GPS time Received.");
        initialTime.year = year < 100 ? year + 2000 : year;
        initialTime.month = month;
        initialTime.day = day;
        initialTime.hour = hour;
        initialTime.minute = minute;
        initialTime.second = second;
        initialTime.timezone = timezone;

        // タイムゾーンを考慮して時刻を補正
        // int totalMinutes = (initialTime.hour * 60 + initialTime.minute) + (initialTime.timezone * 15);
        int totalMinutes = (initialTime.hour * 60 + initialTime.minute);
        initialTime.hour = (totalMinutes / 60) % 24;
        initialTime.minute = totalMinutes % 60;

        // 計測起点とtimeModeフラグを設定
        currentTime = initialTime;
        startMillis = millis();
        timeMode = LTE_TIME;

        cbx3_log(LOG_INF, "SET TIME CODE(LTE TIME) : %02d/%02d/%02d,%02d:%02d:%02d+%02d",
                 initialTime.year % 100, initialTime.month, initialTime.day,
                 initialTime.hour, initialTime.minute, initialTime.second,
                 initialTime.timezone);
    }

    return true;
}

bool TimeCode::setServerTimeCode(const char *buff)
{
    // バッファから文字列を作成し、トリム
    char tempBuff[strlen(buff) + 1];
    strcpy(tempBuff, buff);
    String buffStr = String(tempBuff); // char * を String に変換
    buffStr.trim();

    buffStr.replace("@FREESPACE:", ""); //@FREESPACE:を削除

    int Index1 = buffStr.indexOf("_E");
    String checkSumStr = buffStr.substring(Index1 + 2, Index1 + 2 + 2); // Checksum 2文字のみ抜き出す
    buffStr = buffStr.substring(0, Index1);                             // 本文からCheckSum部分を削除
    unsigned long crcIn = crcx::crc32(reinterpret_cast<const uint8_t *>(buffStr.c_str()), buffStr.length());
    crcIn = crcIn & 0xff; // 最後の２桁を切り出し
    unsigned long crcRef = strtol(checkSumStr.c_str(), NULL, 16);

    cbx3_log(LOG_DBG, "Main : %s", buffStr.c_str());
    cbx3_log(LOG_DBG, "CheckSumIn : %x", crcIn);
    cbx3_log(LOG_DBG, "CheckSumRef: %x", crcRef);

    if (crcIn == crcRef)
    {
        cbx3_log(LOG_DBG, "CRC OK");

        // ここからデコード
        buffStr.replace("UT_", ""); // UT_を削除
        cbx3_log(LOG_DBG, "TimeCode : %s", buffStr.c_str());

        unsigned int timecode = strtoul(buffStr.c_str(), NULL, 10);
        cbx3_log(LOG_DBG, "TimeCode(uint) : %d", timecode);

        time_t localTime = timecode + TIMEZONE_OFFSET_SECONDS;
        struct tm *timeinfo = localtime(&localTime);

        initialTime.year = timeinfo->tm_year + 1900;
        initialTime.month = timeinfo->tm_mon + 1;
        initialTime.day = timeinfo->tm_mday;
        initialTime.hour = timeinfo->tm_hour;
        initialTime.minute = timeinfo->tm_min;
        initialTime.second = timeinfo->tm_sec;
        // デコードここまで

        // 変換結果を出力
        cbx3_log(LOG_INF, "SET TIME CODE(SERVER TIME) : %02d/%02d/%02d,%02d:%02d:%02d+%02d",
                 initialTime.year, initialTime.month, initialTime.day,
                 initialTime.hour, initialTime.minute, initialTime.second,
                 initialTime.timezone);

        // ここから旧時刻との差分計算
        // if (timeMode != ELPS_TIME) // 旧時刻がサーバー/GPSから取れていない時は計算しない
        // {
        // 現在時刻を更新（旧データで１回更新する）
        updateTime();

        // 前回の時刻から計算された現在時刻を秒単位で計算
        int calculatedCurrentTimeInSeconds = currentTime.hour * 3600 + currentTime.minute * 60 + currentTime.second;
        // 新しいタイムコードの時刻を秒単位で計算
        int newTimeCodeInSeconds = timeinfo->tm_hour * 3600 + timeinfo->tm_min * 60 + timeinfo->tm_sec;
        // 差分を計算
        timeDifference = newTimeCodeInSeconds - calculatedCurrentTimeInSeconds;

        // 差分を表示
        cbx3_log(LOG_INF, "UPDATE TIMECODE(SERVER TIME) : Timer Difference: %d seconds", timeDifference);
        // }

        // 計測起点（新）を設定
        currentTime = initialTime;
        startMillis = millis();
        // Timecode識別
        timeMode = SERVER_TIME;

        return true;
    }
    else
    {
        cbx3_log(LOG_WAR, "SERVERTIME CRC ERROR");
        return false;
    }
}

// 起動からの経過時間（ms）を用いて現在時刻を計算　-> currentTimeを更新
void TimeCode::updateTime()
{
    unsigned long currentMillis = millis();
    unsigned long elapsedTime = (currentMillis - startMillis) / 1000;

    // オーバーフローを考慮
    if (currentMillis < startMillis) // 起点時刻より現在時刻が前になってしまったら（オーバーフローして一周した状態）
    {
        elapsedTime = ((unsigned long)(-1) - startMillis + currentMillis + 1) / 1000;
        startMillis = currentMillis; // オーバーフロー後の新しい起点を設定
    }

    if (elapsedTime > 0)
    {
        startMillis += elapsedTime * 1000;
        if (currentTime.year > 0)
        {
            struct tm timeinfo;
            memset(&timeinfo, 0, sizeof(timeinfo));
            
            int fullYear = currentTime.year;
            if (fullYear < 100)
            {
                fullYear += 2000;
            }
            timeinfo.tm_year = fullYear - 1900;
            timeinfo.tm_mon = currentTime.month - 1; // 0-based
            timeinfo.tm_mday = currentTime.day;
            timeinfo.tm_hour = currentTime.hour;
            timeinfo.tm_min = currentTime.minute;
            timeinfo.tm_sec = currentTime.second + elapsedTime;
            timeinfo.tm_isdst = -1;
            
            mktime(&timeinfo);
            
            currentTime.year = timeinfo.tm_year + 1900;
            currentTime.month = timeinfo.tm_mon + 1;
            currentTime.day = timeinfo.tm_mday;
            currentTime.hour = timeinfo.tm_hour;
            currentTime.minute = timeinfo.tm_min;
            currentTime.second = timeinfo.tm_sec;
        }
        else
        {
            currentTime.second += elapsedTime;
            if (currentTime.second >= 60)
            {
                currentTime.minute += currentTime.second / 60;
                currentTime.second %= 60;
                if (currentTime.minute >= 60)
                {
                    currentTime.hour += currentTime.minute / 60;
                    currentTime.minute %= 60;
                    if (currentTime.hour >= 24)
                    {
                        currentTime.hour %= 24;
                    }
                }
            }
        }
    }
}

String TimeCode::getTimeCode()
{
    updateTime();

    char timeString[11]; // 'R' or 'T' + HHMMSS の形式の文字列（終端のNULL文字を含む）
    char timemodeString;
    if (timeMode == LTE_TIME)
    {
        timemodeString = LTE_TIME_STR;
    }
    else
    {
        timemodeString = ELPS_TIME_STR;
    }

    snprintf(timeString, sizeof(timeString), "%c%02d:%02d:%02d", timemodeString, currentTime.hour, currentTime.minute, currentTime.second);
    return String(timeString);
}

// 数値のHH:MMを２桁のString形式に変換するだけの関数
String TimeCode::time2string(uint8_t hour, uint8_t min)
{
    String rstr = "";

    char bufH[4]; // 2桁表示にするため
    char bufM[4]; // 2桁表示にするため
    snprintf(bufH, sizeof(bufH), "%02u", hour);
    snprintf(bufM, sizeof(bufM), "%02u", min);
    rstr = String(bufH) + ":" + String(bufM);
    return rstr;
}
