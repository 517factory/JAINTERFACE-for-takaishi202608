// config_definitions.h
//
// このファイルは、システムの全設定項目の定義をコンパイル時に組み込むためのものです。
// 開発中に設定項目を追加、削除、変更する際は、このファイルのみを編集します。
//
// 注意: このファイルに定義された値は、プログラムのバイナリに組み込まれます。
// 実行中にこれらの定義（minValue, maxValue, accessKeyなど）を動的に変更することはできません。
// 実行中に変更可能なのは、ConfigItemクラスの'value'メンバーのみです。

#pragma once

#include "header.h"
#include <vector>
#include <string>

struct ConfigItemDefinition // 各設定項目を定義するための構造体
{
    const char *objectName;  // この設定項目が属する大きなグループ名。JSONキーとしても使う
    const char *description; // この設定項目の説明
    int defaultValue;        // この設定項目のデフォルト値。
    int minValue;            // 設定可能な値の下限
    int maxValue;            // 設定可能な値の上限
    const char *accessKey;   //// 設定値の変更に必要なアクセスキー。SETコマンドの電文と一致させる
};

// --- 全ての設定項目の定義リスト ---
const std::vector<ConfigItemDefinition> initialConfigDefinitions = {
    // POLL送出間隔（MIN）
    {
        "polltimer",               // objectName
        "Poll Send Interval(min)", // description
        10,                        // defaultValue
        // 1,     // defaultValue
        0,     // minValue
        1440,  // maxValue
        "POLL" // accessKey
    },
    // TimeCodeUpdate感覚値（DAY）
    {
        "tcupdate",                      // objectName
        "TimeCode Update Interval(day)", // description
        1,                               // defaultValue
        1,                               // minValue
        7,                               // maxValue
        "TCUPDATE"                       // accessKey
    },
    // AUTOLOCK開始までの遅延（秒）
    {
        "autolock",                 // objectName
        "Autolock Delay Time(sec)", // description
        0,                          // defaultValue
        0,                          // minValue
        300,                        // maxValue
        "AUTOLOCK"                  // accessKey
    },
    // 電圧読み取り地の校正(プラス側校正)　（mV）
    {
        "vcalp",                            // objectName
        "Volt Callibration Setter(+) (mV)", // description
        0,                                  // defaultValue
        0,                                  // minValue
        1000,                               // maxValue
        "VCALP"                             // accessKey
    },
    // 電圧読み取り地の校正(マイナス側校正)　（mV）
    {
        "vcalm",                             // objectName
        "Volt Callibration Setter (-) (mV)", // description
        0,                                   // defaultValue
        0,                                   // minValue
        1000,                                // maxValue
        "VCALM"                              // accessKey
    },
    // 電圧読み取り地の校正(実際に使う値)　（mV）
    {
        "vcal",                        // objectName
        "Volt Callibration Value(mV)", // description
        0,                             // defaultValue
        -3000,                         // minValue
        3000,                          // maxValue
        "VCAL"                         // accessKey
    },
    // EQ検知方式（TYPE選択）
    {
        "eqtype",                     // objectName
        "EQ Detection Method (type)", // description
        0,                            // defaultValue
        0,                            // minValue
        2,                            // maxValue
        "EQTYPE"                      // accessKey
    },
    // EQ検知ブロック長（ｍSec）
    {
        "eqbt",                           // objectName
        "EQ Detection Block Size (mSec)", // description
        1000,                             // defaultValue
        200,                              // minValue
        5000,                             // maxValue
        "EQBT"                            // accessKey
    },
    // EQ検知ブロック個数（counter）
    {
        "eqct",                            // objectName
        "EQ Detection Block Num (blocks)", // description
        5,                                 // defaultValue
        2,                                 // minValue
        50,                                // maxValue
        "EQCT"                             // accessKey
    },
    // EQ検知インターバル（mSec）
    {
        "eqint",                        // objectName
        "EQ Detection Interval (mSec)", // description
        100,                            // defaultValue
        10,                             // minValue
        2000,                           // maxValue
        "EQINT"                         // accessKey
    },
    // 起動Reason
    {
        "BootReason",                       // objectName
        "BOOT REASON(0:NORMAL OTHER:FAIL)", // description
        0,                                  // defaultValue
        0,                                  // minValue
        999,                                // maxValue
        "BTRSN"                             // accessKey
    },
    // ModemSetupMode   ※EnableでModemの初期設定シーケンスを実行する
    {
        "ModemSetupMode",                     // objectName
        "ModemSetupMode(0:DISABLE 1:ENABLE)", // description
        0,                                    // defaultValue
        0,                                    // minValue
        1,                                    // maxValue
        "MSET"                                // accessKey
    },
    // EQの自動リセットのEnable   ※Enableで自動リセットあり
    {
        "EqAutoResetMode",                     // objectName
        "EqAutoResetMode(0:DISABLE 1:ENABLE)", // description
        1,                                     // defaultValue
        0,                                     // minValue
        1,                                     // maxValue
        "EQARST"                               // accessKey
    },
        // Global SIMでのSIMモード
    {
        "SimMode",                              // objectName
#if ENABLE_DIP_SWITCH
        "SimMode(0:MANUAL 1:FIX 2:AUTO 3:FULL_AUTO)", // description
        0,                                             // defaultValue
#else
        "SimMode(1:FIX 2:AUTO 3:FULL_AUTO)",           // description
        1,                                             // defaultValue
#endif
        0,                                             // minValue
        3,                                             // maxValue
        "SIMMODE"                                    // accessKey
    },
        // Global SIMでのキャリア選択
    {
        "SimSelectMode",                      // objectName
        "SimSelectMode(0:DOCOMO 1:SOFTBANK)", // description
        0,                                     // defaultValue
        0,                                     // minValue
        1,                                     // maxValue
        "SIMSEL"                               // accessKey
    },
        // LTE Band優先順位選択
    {
        "BandSelectMode",                     // objectName
        "BandSelectMode(0:MB_PRI 1:PB_PRI)",  // description
        0,                                     // defaultValue
        0,                                     // minValue
        1,                                     // maxValue
        "BANDSEL"                              // accessKey
    },
    {
        "HEXMODE",                                 // objectName
        "HEXMODE(0:OFF 1:ON)",                     // description
        1,                                          // defaultValue
        0,                                          // minValue
        1,                                          // maxValue
        "HEXMODE"                                 // accessKey
    },

    // --- ここに、追加の設定項目を記述します ---
    // 例:
    // {
    //     "Sensor",
    //     "sampling_interval_ms",
    //     1000,                      // デフォルト値 (ミリ秒)
    //     100,                       // 最小値
    //     5000,                      // 最大値
    //     ""                         // accessKey: なし
    // },
    // {
    //     "Display",
    //     "brightness",
    //     80,                        // デフォルト値 (%)
    //     10,                        // 最小値
    //     100,                       // 最大値
    //     ""                         // accessKey: なし
    // },
};
