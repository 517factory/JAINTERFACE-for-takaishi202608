#include "ConfigManager.hpp"
#include <Arduino.h>
#include "Debug.h"
#include <cstring>

ConfigManager::ConfigManager()
{
    // Constructor is too early for static definition initialization in some cases.
    // Initialization is now handled in begin().
}

bool ConfigManager::begin(NVSManager *nvs, const char *nvs_namespace)
{
    // Ensure items are initialized from definitions before anything else
    initializeFromDefinitions();

    if (!nvs)
        return false;
    _nvs = nvs;
    _nvsNamespace = nvs_namespace;

    if (!_nvs->begin(_nvsNamespace.c_str()))
    {
        cbx3_log(LOG_ERR, "ConfigManager: Failed to open NVS namespace '%s'.", _nvsNamespace.c_str());
        return false;
    }

    if (!loadConfigFromNVS())
    {
        cbx3_log(LOG_WAR, "ConfigManager: Failed to load some settings from NVS. Saving defaults...");
        if (!saveAllConfigToNVS())
        {
            cbx3_log(LOG_ERR, "ConfigManager: Failed to save initial config to NVS.");
            _nvs->end();
            return false;
        }
    }

    cbx3_log(LOG_INF, "ConfigManager: NVS initialized and settings loaded/checked.");
    return true;
}

void ConfigManager::end()
{
    if (_nvs)
    {
        _nvs->end();
        _nvs = nullptr;
    }
}

std::string ConfigManager::createNVSKey(const std::string &objectName)
{
    // NVS Key limit is 15 chars.
    if (objectName.length() > 15) {
        return objectName.substr(0, 15);
    }
    return objectName;
}

void ConfigManager::initializeFromDefinitions()
{
    if (!configItems.empty()) {
        return; // Already initialized
    }
    cbx3_log(LOG_INF, "ConfigManager: Initializing config items.");
    for (const auto &def : initialConfigDefinitions)
    {
        std::unique_ptr<ConfigItem> item = std::make_unique<ConfigItem>(
            def.objectName,
            def.description,
            def.defaultValue,
            def.minValue,
            def.maxValue,
            def.accessKey);
        addConfigItem(std::move(item));
    }
    cbx3_log(LOG_INF, "ConfigManager: Initialized %zu config items from definitions.", configItems.size());
}

void ConfigManager::addConfigItem(std::unique_ptr<ConfigItem> item)
{
    if (!item)
        return;
    std::string key = item->objectName;
    configItems[key] = std::move(item);
}

ConfigItem *ConfigManager::getConfigItem(const std::string &objName)
{
    auto it = configItems.find(objName);
    if (it != configItems.end())
    {
        return it->second.get();
    }
    return nullptr;
}

bool ConfigManager::loadConfigFromNVS()
{
    if (!_nvs)
        return false;
    cbx3_log(LOG_INF, "ConfigManager: Loading config values from NVS...");

    int loaded_count = 0;
    for (const auto &pair : configItems)
    {
        ConfigItem *item = pair.second.get();
        std::string nvsKey = createNVSKey(item->objectName);
        int storedValue = _nvs->getInt(nvsKey.c_str(), item->defaultValue);

        ConfigItem::SetValueResult setResult = item->setValue(storedValue);
        if (setResult == ConfigItem::SetValueResult::SET_OK)
        {
            loaded_count++;
        }
        else
        {
            cbx3_log(LOG_WAR, "ConfigManager: NVS value for '%s' (%d) out of range. Using default: %d.",
                     item->objectName.c_str(), storedValue, item->defaultValue);
            item->value = item->defaultValue;
        }
    }

    cbx3_log(LOG_INF, "ConfigManager: Loaded/verified %d items from NVS.", loaded_count);
    return true;
}

bool ConfigManager::saveConfigToNVS(const std::string &objName, int newValue)
{
    if (!_nvs)
        return false;

    std::string nvsKey = createNVSKey(objName);
    if (!_nvs->putInt(nvsKey.c_str(), newValue))
    {
        cbx3_log(LOG_ERR, "ConfigManager: Failed to save key '%s' to NVS.", nvsKey.c_str());
        return false;
    }
    return true;
}

bool ConfigManager::saveAllConfigToNVS()
{
    if (!_nvs)
        return false;

    bool success = true;
    for (const auto &pair : configItems)
    {
        const ConfigItem *item = pair.second.get();
        if (!saveConfigToNVS(item->objectName, item->value))
        {
            success = false;
        }
    }
    return success;
}

int ConfigManager::getValue(const std::string &objName, int defaultValue)
{
    ConfigItem *item = getConfigItem(objName);
    if (item)
    {
        return item->getValue();
    }
    return defaultValue;
}

int ConfigManager::getValue(const std::string &objName)
{
    ConfigItem *item = getConfigItem(objName);
    if (item)
    {
        return item->getValue();
    }
    return -1;
}

int ConfigManager::getValueByAccessKey(const std::string &accessKey)
{
    ConfigItem *item = getConfigItemByAccessKey(accessKey);
    if (item)
    {
        return item->getValue();
    }
    return -1;
}

ConfigManager::ConfigManagerResult ConfigManager::setValue(const std::string &objName, int newValue, const std::string &accessKey)
{
    ConfigItem *item = getConfigItem(objName);
    if (!item)
    {
        return CONFIG_ERR_ITEM_NOT_FOUND;
    }

    ConfigItem::SetValueResult itemSetResult;
    if (!accessKey.empty())
    {
        itemSetResult = item->setValueFromExternal(newValue, accessKey);
    }
    else
    {
        itemSetResult = item->setValue(newValue);
    }

    if (itemSetResult == ConfigItem::SetValueResult::SET_OK)
    {
        if (saveConfigToNVS(objName, newValue))
        {
            return CONFIG_OK;
        }
        else
        {
            return CONFIG_ERR_NVS_FAILED;
        }
    }

    switch (itemSetResult)
    {
    case ConfigItem::SetValueResult::ERR_OUT_OF_RANGE:
        return CONFIG_ERR_INVALID_VALUE_RANGE;
    case ConfigItem::SetValueResult::ERR_INVALID_ACCESS_KEY:
        return CONFIG_ERR_INVALID_ACCESS_KEY;
    default:
        return CONFIG_ERR_UNKNOWN;
    }
}

ConfigManager::ConfigManagerResult ConfigManager::setValueByAccessKey(const std::string &accessKey, int newValue)
{
    if (accessKey.empty())
    {
        return CONFIG_ERR_INVALID_ACCESS_KEY;
    }

    ConfigItem *foundItem = getConfigItemByAccessKey(accessKey);
    if (!foundItem)
    {
        return CONFIG_ERR_ITEM_NOT_FOUND;
    }

    ConfigItem::SetValueResult itemSetResult = foundItem->setValueFromExternal(newValue, accessKey);
    if (itemSetResult == ConfigItem::SetValueResult::SET_OK)
    {
        if (saveConfigToNVS(foundItem->objectName, newValue))
        {
            return CONFIG_OK;
        }
        else
        {
            return CONFIG_ERR_NVS_FAILED;
        }
    }
    else
    {
        switch (itemSetResult)
        {
        case ConfigItem::SetValueResult::ERR_OUT_OF_RANGE:
            return CONFIG_ERR_INVALID_VALUE_RANGE;
        case ConfigItem::SetValueResult::ERR_INVALID_ACCESS_KEY:
            return CONFIG_ERR_INVALID_ACCESS_KEY;
        default:
            return CONFIG_ERR_UNKNOWN;
        }
    }
}

ConfigItem *ConfigManager::getConfigItemByAccessKey(const std::string &accessKey)
{
    for (auto const &[objName, u_ptr_item] : configItems)
    {
        if (u_ptr_item->accessKey == accessKey)
        {
            return u_ptr_item.get();
        }
    }
    return nullptr;
}

int ConfigManager::getMinValueByAccessKey(const std::string &accessKey)
{
    ConfigItem *item = getConfigItemByAccessKey(accessKey);
    if (item)
    {
        return item->minValue;
    }
    return -1;
}

int ConfigManager::getMaxValueByAccessKey(const std::string &accessKey)
{
    ConfigItem *item = getConfigItemByAccessKey(accessKey);
    if (item)
    {
        return item->maxValue;
    }
    return -1;
}

void ConfigManager::printAllConfigValues() const
{
    cbx3_log(LOG_INF, "--- All Config Items List ---");
    for (const auto &pair : configItems)
    {
        pair.second->print();
    }
    cbx3_log(LOG_INF, "--- End of List---------------");
}
