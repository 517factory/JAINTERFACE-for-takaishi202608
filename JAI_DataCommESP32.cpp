/*
Data送受信クラス for ESP32-S3

    517Factory
*/

#include "JAI_DataCommESP32.h"

void DataCommESP32::EncodeSndData(const SendDatas &dataSet)
{
    // cbx3_log(LOG_DBG, "Encord Command %d", data.cmd_type);
    switch (dataSet.cmd_type)
    {
    // case WMSG:
    //     EncodeSndDataWifiMsg(dataSet);
    //     break;
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
    char deviceID_buffer[7]; // 6桁の16進数 + 終端
    std::snprintf(deviceID_buffer, sizeof(deviceID_buffer), "%06X", dataSet.deviceID);
    String deviceID_str = "ID=" + String(deviceID_buffer);

    String cmd_main = String(CommandTypeToString(dataSet.cmd_type));

    // ダミー付加 (互換性のため)
    cmd_main += ",TMP=0.00";
    cmd_main += ",HMY=0.0";
    cmd_main += ",VBT=0.0";

    cmd_main += ",JA=" + dataSet.JAflgs;

    if (DID_ENABLE)
    {
        cmd_main += "," + deviceID_str;
    }

    cmd_main += "," + dataSet.counter; // timecode

    cbx3_log(LOG_INF, "SND_CMD: %s", cmd_main.c_str());

    if (cmd_main.length() >= sizeof(DataBuff))
    {
        Serial.println("EncodeSndData buffer over");
    }
    else
    {
        cmd_main.toCharArray(DataBuff, sizeof(DataBuff));
    }
}

// void DataCommESP32::EncodeSndDataWifiMsg(const SendDatas &dataSet)
// {
//     String cmd_main = String(CommandTypeToString(dataSet.cmd_type)) + "[" + dataSet.rcvd_cmd + "]";
//     cmd_main += "," + dataSet.counter;

//     cbx3_log(LOG_INF, "SND_CMD: %s", cmd_main.c_str());

//     AsciiToHexFromString(cmd_main);

//     String dataBuffStr = String(SEND_COMMAND_FREE) + charHex + "\r";
//     strncpy(DataBuff, dataBuffStr.c_str(), sizeof(DataBuff) - 1);
//     DataBuff[sizeof(DataBuff) - 1] = '\0'; // Ensure null-termination

//     cbx3_log(LOG_INF, "SND_CMD: %s", DataBuff);

//     if (dataBuffStr.length() >= sizeof(DataBuff))
//     {
//         Serial.println("SetSndData buffer over");
//     }
// }

// void DataCommESP32::EncodeSndDataKeyState(const SendDatas &dataSet)
// {
//     String cmd_main = String(CommandTypeToString(dataSet.cmd_type)) + "[" + dataSet.UIDs + "]," + dataSet.counter;
//     cbx3_log(LOG_INF, "SND_CMD: %s", cmd_main.c_str());

//     AsciiToHexFromString(cmd_main);

//     String dataBuffStr = String(SEND_COMMAND_FREE) + charHex + "\r";

//     if (dataBuffStr.length() >= sizeof(DataBuff))
//     {
//         Serial.println("SetSndData buffer over");
//     }
//     else
//     {
//         dataBuffStr.toCharArray(DataBuff, sizeof(DataBuff));
//     }
// }

void DataCommESP32::EncodeSndDataWhatTheTime(const SendDatas &dataSet)
{
    String cmd_main = "WHAT_THE_TIME";
    cbx3_log(LOG_INF, "SND_CMD: %s", cmd_main.c_str());

    if (cmd_main.length() >= sizeof(DataBuff))
    {
        Serial.println("SetSndData buffer over");
    }
    else
    {
        cmd_main.toCharArray(DataBuff, sizeof(DataBuff));
    }
}

void DataCommESP32::EncodeSndDataLogMsg(const SendDatas &dataSet)
{
    String cmd_main = String(CommandTypeToString(dataSet.cmd_type)) + "[" + dataSet.LogMsg + "]," + dataSet.counter;
    cbx3_log(LOG_INF, "SND_CMD: %s", cmd_main.c_str());

    if (cmd_main.length() >= sizeof(DataBuff))
    {
        Serial.println("SetSndData buffer over");
    }
    else
    {
        cmd_main.toCharArray(DataBuff, sizeof(DataBuff));
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

// (AsciiToHex removed)

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
        if (lines[i].indexOf(RCV_COMMAND_JRST) != -1)
        {
            results.push_back(CocoBoxControlCommands{LTE_JRST, lines[i]});
        }
        else if (lines[i].indexOf(RCV_COMMAND_CHECK) != -1)
        {
            results.push_back(CocoBoxControlCommands{LTE_CHECK, lines[i]});
        }
        else if (lines[i].indexOf(RCV_COMMAND_MODEMSTATE) != -1)
        {
            results.push_back(CocoBoxControlCommands{LTE_MODEMSTATE, lines[i]});
        }
        else if (lines[i].indexOf(RCV_COMMAND_RESET) != -1)
        {
            results.push_back(CocoBoxControlCommands{LTE_RESET, lines[i]});
        }
        else if (lines[i].indexOf(RCV_COMMAND_SET) != -1)
        {
            results.push_back(CocoBoxControlCommands{LTE_SET, lines[i]});
        }
        else if (lines[i].substring(0, 2) == RCV_ANSWERBACK)
        {
            results.push_back(CocoBoxControlCommands{LTE_ANSWERBACK, lines[i]});
        }
        else if (lines[i].indexOf("+CCLK:") != -1)
        {
            results.push_back(CocoBoxControlCommands{LTE_GPSTIME, lines[i]});
        }
        else if (lines[i].indexOf(RCV_SERVERTIME) != -1)
        {
            results.push_back(CocoBoxControlCommands{LTE_SERVERTIME, lines[i]});
        }
        else if (lines[i].indexOf(RCV_OK) != -1)
        {
            results.push_back(CocoBoxControlCommands{LTE_OK, lines[i]});
        }
        else if (lines[i].indexOf(RCV_ERROR) != -1)
        {
            results.push_back(CocoBoxControlCommands{LTE_ERROR, lines[i]});
        }
        else
        {
            results.push_back(CocoBoxControlCommands{LTE_UNKNOWN, lines[i]});
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

    // 「：」から前を切り落とす（存在する場合のみ）
    int colonIndex = input.indexOf(':');
    if (colonIndex != -1)
    {
        input = input.substring(colonIndex + 1);
    }
    // コロンがない場合はそのまま続行（MQTTペイロードなどプレフィックスがない場合に対応）

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
            configSetting.command == "VCALP" ||
            configSetting.command == "VCALM" ||
            configSetting.command == "EQTYPE" ||
            configSetting.command == "EQBT" ||
            configSetting.command == "EQCT" ||
            configSetting.command == "EQINT" ||
            configSetting.command == "EQARST" ||
            configSetting.command == "SIMMODE" ||
            configSetting.command == "SIMSEL" ||
            configSetting.command == "BANDSEL" ||
            configSetting.command == "HEXMODE")
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
    NSI_Type tokenType = UNKNOWN;

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
        return SRV_NONE;
    }
    else if (token == "NO SRV")
    {
        return NO_SRV;
    }
    else if (token == "LIMITED")
    {
        return LIMITED;
    }
    else if (token == "IN SRV")
    {
        return IN_SRV;
    }
    return UNKNOWN;
}
