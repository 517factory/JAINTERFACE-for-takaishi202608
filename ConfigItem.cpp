#include "ConfigItem.hpp"
#include <Arduino.h>
#include "Debug.h"

ConfigItem::ConfigItem(const std::string &objName, const std::string &desc, int val, int minVal, int maxVal, const std::string &accKey)
    : objectName(objName), description(desc), defaultValue(val), minValue(minVal), maxValue(maxVal), accessKey(accKey)
{
    if (checkRange(val))
    {
        this->value = val;
    }
    else
    {
        this->value = minVal;
        cbx3_log(LOG_WAR, "Default value %d for '%s' is out of range [%d, %d]. Using minValue %d.", val, objectName.c_str(), minValue, maxValue, minVal);
    }
}

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
    return ConfigItem::SetValueResult::ERR_OUT_OF_RANGE;
}

ConfigItem::SetValueResult ConfigItem::setValueFromExternal(int newValue, const std::string &receivedAccessKey)
{
    if (this->accessKey != receivedAccessKey)
    {
        return ConfigItem::SetValueResult::ERR_INVALID_ACCESS_KEY;
    }
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
