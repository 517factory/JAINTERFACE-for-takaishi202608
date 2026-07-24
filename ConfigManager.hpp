#pragma once

#include <vector>
#include <map>
#include <memory>
#include <string>

#include "config_definitions.h"
#include "NVSManager.hpp"
#include "ConfigItem.hpp"

class ConfigManager
{
public:
  enum ConfigManagerResult
  {
    CONFIG_OK = 0,
    CONFIG_ERR_ITEM_NOT_FOUND,
    CONFIG_ERR_INVALID_VALUE_RANGE,
    CONFIG_ERR_INVALID_ACCESS_KEY,
    CONFIG_ERR_NVS_FAILED,
    CONFIG_ERR_UNKNOWN
  };

  ConfigManager();

  bool begin(NVSManager *nvs, const char *nvs_namespace = "config");
  void end();

  void initializeFromDefinitions();
  void addConfigItem(std::unique_ptr<ConfigItem> item);
  ConfigItem *getConfigItem(const std::string &objName);
  ConfigItem *getConfigItemByAccessKey(const std::string &accessKey);
  const std::map<std::string, std::unique_ptr<ConfigItem>> &getAllConfigItems() const { return configItems; }

  int getValue(const std::string &objName, int defaultValue);
  int getValue(const std::string &objName);
  ConfigManagerResult setValue(const std::string &objName, int newValue, const std::string &accessKey = "");
  int getValueByAccessKey(const std::string &accessKey);
  ConfigManagerResult setValueByAccessKey(const std::string &accessKey, int newValue);
  int getMinValueByAccessKey(const std::string &accessKey);
  int getMaxValueByAccessKey(const std::string &accessKey);
  void printAllConfigValues() const;

private:
  NVSManager *_nvs = nullptr;
  std::string _nvsNamespace;

  std::map<std::string, std::unique_ptr<ConfigItem>> configItems;

  std::string createNVSKey(const std::string &objectName);

  bool loadConfigFromNVS();
  bool saveConfigToNVS(const std::string &objName, int newValue);
  bool saveAllConfigToNVS();
};
