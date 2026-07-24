#pragma once

#include <string>
#include <memory>

class ConfigItem
{
public:
  enum SetValueResult
  {
    SET_OK = 0,
    ERR_OUT_OF_RANGE,
    ERR_INVALID_ACCESS_KEY,
    ERR_UNKNOWN
  };

  const std::string objectName;
  const std::string description;
  const int defaultValue;
  const int minValue;
  const int maxValue;
  const std::string accessKey;

  int value;

  ConfigItem(const std::string &objName, const std::string &desc, int val, int minVal, int maxVal, const std::string &accKey);

  int getValue() const;
  SetValueResult setValue(int newValue);
  SetValueResult setValueFromExternal(int newValue, const std::string &receivedAccessKey);
  void print() const;

private:
  bool checkRange(int val) const;
};
