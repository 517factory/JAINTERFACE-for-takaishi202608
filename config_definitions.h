#pragma once

#include <vector>
#include <string>

struct ConfigItemDefinition
{
    const char *objectName;
    const char *description;
    int defaultValue;
    int minValue;
    int maxValue;
    const char *accessKey;
};

const std::vector<ConfigItemDefinition> initialConfigDefinitions = {
    {
        "polltimer",
        "Poll Send Interval(min)",
        10,
        0,
        1440,
        "POLL"
    },
    {
        "tcupdate",
        "TimeCode Update Interval(day)",
        1,
        1,
        7,
        "TCUPDATE"
    },
    {
        "autolock",
        "Autolock Delay Time(sec)",
        0,
        0,
        300,
        "AUTOLOCK"
    },
    {
        "voltthreshold",
        "Low Voltage Threshold(mV)",
        6000,
        3000,
        9000,
        "VTH"
    },
    {
        "vcal",
        "Volt Calibration Value(mV)",
        0,
        -3000,
        3000,
        "VCAL"
    },
    {
        "vcalp",
        "Volt Calibration Setter(+) (mV)",
        0,
        0,
        1000,
        "VCALP"
    },
    {
        "vcalm",
        "Volt Calibration Setter(-) (mV)",
        0,
        0,
        1000,
        "VCALM"
    },
    {
        "BootReason",
        "BOOT REASON(0:NORMAL OTHER:FAIL)",
        0,
        0,
        999,
        "BTRSN"
    },
    {
        "ModemSetupMode",
        "ModemSetupMode(0:DISABLE 1:ENABLE)",
        0,
        0,
        1,
        "MSET"
    }
};
