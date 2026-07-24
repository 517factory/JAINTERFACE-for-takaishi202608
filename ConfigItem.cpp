// ConfigItem.cpp
#include "ConfigItem.hpp"
#include <Arduino.h> // Serial.print() のために必要
#include "Debug.h"

// コンストラクタの実装
ConfigItem::ConfigItem(const std::string &objName, const std::string &desc, int val, int minVal, int maxVal, const std::string &accKey)
    : objectName(objName), description(desc), defaultValue(val), minValue(minVal), maxValue(maxVal), accessKey(accKey)
{
    // コンストラクタで渡された値が有効範囲内かチェックし、valueに設定
    if (checkRange(val))
    {
        this->value = val;
    }
    else
    {
        // デフォルト値が無効な場合は、minValueを使用
        this->value = minVal;
        cbx3_log(LOG_WAR, "Default value %d for '%s' is out of range [%d, %d]. Using minValue %d.", val, objectName.c_str(), minValue, maxValue, minVal);
    }
}

// その他のメソッドの実装
int ConfigItem::getValue() const
{
    return value;
}

ConfigItem::SetValueResult ConfigItem::setValue(int newValue)
{
    if (checkRange(newValue))
    {
        value = newValue;
        return ConfigItem::SetValueResult::SET_OK;
    }
    // cbx3_log(LOG_ERR, "ConfigItem: Value %d for '%s' is out of range [%d, %d]. Not set.",newValue, objectName.c_str(), minValue, maxValue);
    return ConfigItem::SetValueResult::ERR_OUT_OF_RANGE;
}

ConfigItem::SetValueResult ConfigItem::setValueFromExternal(int newValue, const std::string &receivedAccessKey)
{
    // まず、アクセスキーが合っているかチェック
    if (this->accessKey != receivedAccessKey)
    {
        // cbx3_log(LOG_WAR, "ConfigItem '%s': Invalid accessKey '%s' for setting value. Expected '%s'.", objectName.c_str(), receivedAccessKey.c_str(), this->accessKey.c_str());
        return ConfigItem::SetValueResult::ERR_INVALID_ACCESS_KEY;
    }

    // アクセスキーが合っていれば、通常の setValue を呼び出して値の範囲チェックを行う
    return setValue(newValue);
}

bool ConfigItem::checkRange(int val) const
{
    return (val >= minValue && val <= maxValue);
}

void ConfigItem::print() const
{
    cbx3_log(LOG_INF, "  %s [%s]: %d (Default: %d, Range: %d-%d) (%s)", description.c_str(), accessKey.c_str(), value, defaultValue, minValue, maxValue, objectName.c_str());
}
