/*
Data送受信クラス for ESP32-S3

    517Factory
*/

/*
Data送受信クラス for ESP32-S3

    517Factory
*/

#include "DataCommESP32.h"

void DataCommESP32::EncodeSndData(const SendDatas &dataSet)
{
    // cbx3_log(LOG_DBG, "Encord Command %d", data.cmd_type);
    switch (dataSet.cmd_type)
    {
    case WMSG:
        EncodeSndDataWifiMsg(dataSet);
        break;
    case KBOX:
        EncodeSndDataKeyState(dataSet);
        break;
    case ATCOM:
        EncodeSndDataATCommand(dataSet);
        break;
    case LOG:
        EncodeSndDataLogMsg(dataSet);
        break;
    case WHAT_THE_TIME:
        EncodeSndDataWhatTheTime(dataSet);
        break;
    default:
        EncodeSndDataMain(dataSet);
        break;
    }
}

void DataCommESP32::EncodeSndDataMain(const SendDatas &dataSet)
{
    String key_st;
    String door_st;
    String EQ_st;
    String PWRmsg;
    String wl_st;
    char deviceID_buffer[7]; // 6桁の16進数 + 終端

    std::snprintf(deviceID_buffer, sizeof(deviceID_buffer), "%06X", dataSet.deviceID);
    String deviceID_str = "ID=" + String(deviceID_buffer);
    // cbx3_log(LOG_DBG, "deviceID_str=%s", deviceID_str.c_str());

    if (dataSet.st_isPwrAC)
    {
        PWRmsg = SEND_COMMAND_PWR_AC;
    }
    else
    {
        PWRmsg = SEND_COMMAND_PWR_BT;
    }

    if (dataSet.st_isDoorClosed)
    {
        door_st = SEND_COMMAND_DR_CLOSE;
    }
    else
    {
        door_st = SEND_COMMAND_DR_OPEN;
    }

    if (dataSet.st_isEqOn)
    {
        EQ_st = SEND_COMMAND_EQ_ON;
    }
    else
    {
        EQ_st = SEND_COMMAND_EQ_OF;
    }

    if (!dataSet.st_isKeyLocked)
    {
        key_st = SEND_COMMAND_KEY_UNLOCKED;
    }
    else if (dataSet.st_isKeyLocked)
    {
        key_st = SEND_COMMAND_KEY_LOCKED;
    }

    if (WIFI_ENABLE)
    {
        if (dataSet.st_isWiFiOn)
        {
            wl_st = SEND_COMMAND_WON;
        }
        else
        {
            wl_st = SEND_COMMAND_WOF;
        }
    }

    String cmd_main = String(CommandTypeToString(dataSet.cmd_type)) + "," + key_st + "," + door_st + "," + EQ_st + "," + PWRmsg;

    if (WIFI_ENABLE)
    {
        cmd_main += "," + wl_st;
    }

    if (DHT22_ENABLE)
    {
        cmd_main += "," + dataSet.DHT22msg;
    }

    if (VBAT_ENABLE)
    {
        cmd_main += ",VBT=" + String(dataSet.vbat, 2);
    }

    if (DID_ENABLE)
    {
        cmd_main += "," + deviceID_str;
    }

    cmd_main += "," + dataSet.counter; // timecode

    cbx3_log(LOG_INF, "SND_CMD: %s", cmd_main.c_str());

    AsciiToHexFromString(cmd_main);

    String finalCmd = SEND_COMMAND_FREE + String(charHex) + "\r";

    if (finalCmd.length() >= sizeof(DataBuff))
    {
        Serial.println("EncodeSndData buffer over");
    }
    else
    {
        finalCmd.toCharArray(DataBuff, sizeof(DataBuff));
    }
}

void DataCommESP32::EncodeSndDataWifiMsg(const SendDatas &dataSet)
{
    String cmd_main = String(CommandTypeToString(dataSet.cmd_type)) + "[" + dataSet.rcvd_cmd + "]";
    cmd_main += "," + dataSet.counter;

    cbx3_log(LOG_INF, "SND_CMD: %s", cmd_main.c_str());

    AsciiToHexFromString(cmd_main);

    String dataBuffStr = String(SEND_COMMAND_FREE) + charHex + "\r";
    strncpy(DataBuff, dataBuffStr.c_str(), sizeof(DataBuff) - 1);
    DataBuff[sizeof(DataBuff) - 1] = '\0'; // Ensure null-termination

    cbx3_log(LOG_INF, "SND_CMD: %s", DataBuff);

    if (dataBuffStr.length() >= sizeof(DataBuff))
    {
        Serial.println("SetSndData buffer over");
    }
}

void DataCommESP32::EncodeSndDataKeyState(const SendDatas &dataSet)
{
    String cmd_main = String(CommandTypeToString(dataSet.cmd_type)) + "[" + dataSet.UIDs + "]," + dataSet.counter;
    cbx3_log(LOG_INF, "SND_CMD: %s", cmd_main.c_str());

    AsciiToHexFromString(cmd_main);

    String dataBuffStr = String(SEND_COMMAND_FREE) + charHex + "\r";

    if (dataBuffStr.length() >= sizeof(DataBuff))
    {
        Serial.println("SetSndData buffer over");
    }
    else
    {
        dataBuffStr.toCharArray(DataBuff, sizeof(DataBuff));
    }
}

void DataCommESP32::EncodeSndDataWhatTheTime(const SendDatas &dataSet)
{
    String cmd_main = "WHAT_THE_TIME";
    cbx3_log(LOG_INF, "SND_CMD: %s", cmd_main.c_str());

    AsciiToHexFromString(cmd_main);

    String dataBuffStr = String(SEND_COMMAND_FREE) + charHex + "\r";

    if (dataBuffStr.length() >= sizeof(DataBuff))
    {
        Serial.println("SetSndData buffer over");
    }
    else
    {
        dataBuffStr.toCharArray(DataBuff, sizeof(DataBuff));
    }
}

void DataCommESP32::EncodeSndDataLogMsg(const SendDatas &dataSet)
{
    String cmd_main = String(CommandTypeToString(dataSet.cmd_type)) + "[" + dataSet.LogMsg + "]," + dataSet.counter;
    cbx3_log(LOG_INF, "SND_CMD: %s", cmd_main.c_str());

    AsciiToHexFromString(cmd_main);

    String dataBuffStr = String(SEND_COMMAND_FREE) + charHex + "\r";

    if (dataBuffStr.length() >= sizeof(DataBuff))
    {
        Serial.println("SetSndData buffer over");
    }
    else
    {
        dataBuffStr.toCharArray(DataBuff, sizeof(DataBuff));
    }
}

void DataCommESP32::EncodeSndDataATCommand(const SendDatas &dataSet)
{
    String cmd = dataSet.AT_cmd_main + "\r";
    if (cmd.length() >= sizeof(DataBuff))
    {
        Serial.println("EncodeSndDataATCommand buffer over");
        // 出力が切り捨てられた
    }
    else
    {
        cmd.toCharArray(DataBuff, sizeof(DataBuff));
    }
}

// 文字列→HEX変換
void DataCommESP32::AsciiToHex(char *strSrc)
{
    int length = strlen(strSrc);
    memset(charHex, '\0', sizeof(charHex));

    char tmpstr[3];

    for (int i = 0; i < length; i++)
    {
        int result = snprintf(tmpstr, sizeof(tmpstr), "%x", strSrc[i]);
        if (result >= sizeof(tmpstr))
        {
            cbx3_log(LOG_ERR, "AsciiToHex buffer over");
        }
        strcat(charHex, tmpstr);
    }
}

void DataCommESP32::AsciiToHexFromString(const String &strSrc)
{
    int length = strSrc.length();
    memset(charHex, '\0', sizeof(charHex));

    char tmpstr[3];

    for (int i = 0; i < length; i++)
    {
        int result = snprintf(tmpstr, sizeof(tmpstr), "%x", strSrc[i]);
        if (result >= sizeof(tmpstr))
        {
            cbx3_log(LOG_ERR, "AsciiToHex buffer over");
        }
        strcat(charHex, tmpstr);
    }
}

// 改行コードで文字列を分割して配列に格納する関数
int DataCommESP32::splitString(const String &str, String result[])
{
    int startIndex = 0;
    int endIndex = str.indexOf("\n");
    int lineCount = 0;

    while (endIndex != -1 && lineCount < MAX_DEVIDE_LINES)
    {
        String line = str.substring(startIndex, endIndex);
        // 改行コードや空文字列を無視
        if (line.length() > 0 && line != "\r" && line != "\n")
        {
            result[lineCount++] = line;
        }
        startIndex = endIndex + 1;
        endIndex = str.indexOf("\n", startIndex);
    }

    // 最後の行を追加
    if (lineCount < MAX_DEVIDE_LINES)
    {
        String line = str.substring(startIndex);
        if (line.length() > 0 && line != "\r" && line != "\n")
        {
            result[lineCount++] = line;
        }
    }

    return lineCount;
}

std::vector<CocoBoxControlCommands> DataCommESP32::ChkRcvData(const char *buff)
{
    String tmpstr = buff;
    String lines[MAX_DEVIDE_LINES];
    int lineCount = splitString(tmpstr, lines);

    std::vector<CocoBoxControlCommands> results;
    // // 分割したデータを表示
    // cbx3_log(LOG_INF, "Split data:");
    // for (int i = 0; i < lineCount; ++i)
    // {
    //     cbx3_log(LOG_INF, "Line %d: %s", i, replaceData4Disp(lines[i].c_str()));
    // }

    for (int i = 0; i < lineCount; ++i)
    {
        if (lines[i].indexOf(RCV_COMMAND_DRLOCK) != -1)
        {
            results.push_back(CocoBoxControlCommands::LTE_LOCK);
        }
        else if (lines[i].indexOf(RCV_COMMAND_DRATLOCK) != -1)
        {
            results.push_back(CocoBoxControlCommands::LTE_AUTOLOCK);
        }
        else if (lines[i].indexOf(RCV_COMMAND_DRUNLOCK) != -1)
        {
            results.push_back(CocoBoxControlCommands::LTE_UNLOCK);
        }
        else if (lines[i].indexOf(RCV_COMMAND_CHECK) != -1)
        {
            results.push_back(CocoBoxControlCommands::LTE_CHECK);
        }
        else if (lines[i].indexOf(RCV_COMMAND_RESET) != -1)
        {
            results.push_back(CocoBoxControlCommands::LTE_RESET);
        }
        else if (lines[i].indexOf(RCV_COMMAND_WIFION) != -1)
        {
            results.push_back(CocoBoxControlCommands::LTE_WIFION);
        }
        else if (lines[i].indexOf(RCV_COMMAND_WIFIOFF) != -1)
        {
            results.push_back(CocoBoxControlCommands::LTE_WIFIOFF);
        }
        else if (lines[i].indexOf(RCV_COMMAND_KEYCHECK) != -1)
        {
            results.push_back(CocoBoxControlCommands::LTE_KCHK);
        }
        else if (lines[i].indexOf(RCV_COMMAND_SET) != -1)
        {
            results.push_back(CocoBoxControlCommands::LTE_SET);
        }
        else if (lines[i].substring(0, 2) == RCV_ANSWERBACK)
        {
            results.push_back(CocoBoxControlCommands::LTE_ANSWERBACK);
        }
        else if (lines[i].indexOf(RCV_AMT5102) != -1)
        {
            results.push_back(CocoBoxControlCommands::LTE_AMT5102OK);
        }
        else if (lines[i].indexOf(RCV_AMT5102_NSI) != -1)
        {
            NSI_buff = lines[i];
            results.push_back(CocoBoxControlCommands::LTE_AMT5102_NSI);
        }
        else if (lines[i].indexOf(RCV_GPSTIME) != -1)
        {
            results.push_back(CocoBoxControlCommands::LTE_GPSTIME);
        }
        else if (lines[i].indexOf(RCV_SERVERTIME) != -1)
        {
            results.push_back(CocoBoxControlCommands::LTE_SERVERTIME);
        }
        else if (lines[i].indexOf(RCV_OK) != -1)
        {
            results.push_back(CocoBoxControlCommands::LTE_OK);
        }
        else if (lines[i].indexOf(RCV_ERROR) != -1)
        {
            results.push_back(CocoBoxControlCommands::LTE_ERROR);
        }
        else
        {
            results.push_back(CocoBoxControlCommands::LTE_UNKNOWN);
        }
    }

    return results;
}

configSetting DataCommESP32::decodeConfigSetting(const char *buff)
{
    String input(buff);
    cbx3_log(LOG_DBG, "DECORDING SET DATA");

    configSetting configSetting = {"", -1};

    cbx3_log(LOG_DBG, "INPUT_DATA=%s", replaceData4Disp(input.c_str()));

    // 「：」から前を切り落とす
    int colonIndex = input.indexOf(':');
    if (colonIndex != -1)
    {
        input = input.substring(colonIndex + 1);
    }
    else
    {
        cbx3_log(LOG_ERR, "Error: Missing colon ':'");
        return configSetting;
    }

    // 「XX」が存在するか確認、「XX」から後ろを切り落とす
    int xxIndex = input.indexOf("XX");
    if (xxIndex != -1)
    {
        input = input.substring(0, xxIndex);
    }
    else
    {
        cbx3_log(LOG_ERR, "Error: Missing 'XX'");
        configSetting.command = "ERROR";
        return configSetting;
    }
    // cbx3_log(LOG_INF, "INPUT_DATA=%s", replaceData4Disp(input.c_str()));

    // 「_」で区切られた要素を取り出す
    int firstUnderscoreIndex = input.indexOf('_');
    int secondUnderscoreIndex = input.indexOf('_', firstUnderscoreIndex + 1);
    int thirdUnderscoreIndex = input.indexOf('_', secondUnderscoreIndex + 1);

    // 要素が0の場合にエラーを出力
    if (firstUnderscoreIndex == -1)
    {
        cbx3_log(LOG_ERR, "Error: Input does not contain any underscores");
        configSetting.command = "ERROR";
        return configSetting;
    }

    // 要素が3つでない場合にエラーを出力
    if (firstUnderscoreIndex == -1 || secondUnderscoreIndex == -1 || thirdUnderscoreIndex != -1)
    {
        cbx3_log(LOG_ERR, "Error: DATA FORMAT ERROR (item !=3)");
        configSetting.command = "ERROR";
        return configSetting;
    }

    String part1 = input.substring(0, firstUnderscoreIndex);
    String part2 = input.substring(firstUnderscoreIndex + 1, secondUnderscoreIndex);
    String part3 = input.substring(secondUnderscoreIndex + 1);

    configSetting.command = part2;
    configSetting.value = part3.toInt();

    // cbx3_log(LOG_INF, "Part1: %s", part1.c_str());
    // cbx3_log(LOG_INF, "Part2: %s", part2.c_str());
    // cbx3_log(LOG_INF, "Part3: %d", part3Value);

    // 最初の要素が"SET"でなかった場合にエラーを出力
    if (part1 != "SET")
    {
        cbx3_log(LOG_ERR, "Error: First part is not 'SET'");
        configSetting.command = "ERROR";
        return configSetting;
    }
    else // 正常だった場合
    {
        if (configSetting.command == "POLL" ||
            configSetting.command == "TCUPDATE" ||
            configSetting.command == "AUTOLOCK" ||
            configSetting.command == "VTH" ||
            configSetting.command == "VCALP" ||
            configSetting.command == "VCALM")
        {
            return configSetting;
        }
        else
        {
            configSetting.command = "ERROR";
            return configSetting;
        }
    }
}

NSI_Type DataCommESP32::decodeNSI(const String &input)
{
    String cleanedString = input;
    cleanedString.replace("\r", ""); // CRを取り除く
    cleanedString.replace("\n", ""); // LFを取り除く

    // カンマで分割
    int startIndex = 0;
    int endIndex = cleanedString.indexOf(',');
    int tokenIndex = 0;
    NSI_Type tokenType = NSI_Type::UNKNOWN;

    while (endIndex != -1)
    {
        String token = cleanedString.substring(startIndex, endIndex);
        token.trim();            // トークンから前後の空白と引用符を取り除く
        token.replace("\"", ""); // 引用符を取り除く
        // cbx3_log(LOG_DBG, "TOKEN = %s", token.c_str());

        if (tokenIndex == 1)
        {
            tokenType = parseNsiToken(token);
        }

        startIndex = endIndex + 1;
        endIndex = cleanedString.indexOf(',', startIndex);
        tokenIndex++;
    }

    // 最後のトークンを処理
    String token = cleanedString.substring(startIndex);
    token.trim();            // トークンから前後の空白と引用符を取り除く
    token.replace("\"", ""); // 引用符を取り除く
    // cbx3_log(LOG_DBG, "TOKEN = %s", token.c_str());

    if (tokenIndex == 1)
    {
        tokenType = parseNsiToken(token);
    }

    return tokenType;
}

NSI_Type DataCommESP32::parseNsiToken(const String &token)
{
    if (token == "SRV NONE")
    {
        return NSI_Type::SRV_NONE;
    }
    else if (token == "NO SRV")
    {
        return NSI_Type::NO_SRV;
    }
    else if (token == "LIMITED")
    {
        return NSI_Type::LIMITED;
    }
    else if (token == "IN SRV")
    {
        return NSI_Type::IN_SRV;
    }
    return NSI_Type::UNKNOWN;
}
