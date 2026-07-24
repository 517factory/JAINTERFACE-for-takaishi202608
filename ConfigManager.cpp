// ConfigManager.cpp
#include "ConfigManager.hpp"
#include <Arduino.h> // cbx3_log のため
#include "Debug.h"   // cbx3_log のため
#include <cstring>   // strcmp のため

// --- コンストラクタの実装 ---
// ファイルシステムの引数を削除
ConfigManager::ConfigManager()
{
    // コンストラクタでは、定義からの初期化のみ行い、NVSのオープンはbegin()で行う
    initializeFromDefinitions();
}

// --- NVSManagerをセットし、NVSからロードする ---
bool ConfigManager::begin(NVSManager *nvs, const char *nvs_namespace)
{
    if (!nvs)
        return false;
    _nvs = nvs;
    _nvsNamespace = nvs_namespace;

    if (configItems.empty())
    {
        initializeFromDefinitions();
    }

    // NVS Manager のオープン
    if (!_nvs->begin(_nvsNamespace.c_str()))
    {
        cbx3_log(LOG_ERR, "ConfigManager: Failed to open NVS namespace '%s'.", _nvsNamespace.c_str());
        return false;
    }

    // NVSからのロードを試みる
    if (!loadConfigFromNVS())
    {
        cbx3_log(LOG_WAR, "ConfigManager: Failed to load some settings from NVS. Saving defaults...");
        // ロードに失敗した場合、現在のRAM上のデフォルト値をNVSに書き込む
        if (!saveAllConfigToNVS())
        {
            cbx3_log(LOG_ERR, "ConfigManager: Failed to save initial config to NVS.");
            // NVSへの書き込みが致命的に失敗した場合もエラーとして扱う
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

// NVSキーの生成 (objectNameを直接使用。NVSの最大キー長15文字に注意)
std::string ConfigManager::createNVSKey(const std::string &objectName)
{
    // NVSのキー長制限（15文字）に注意してください。
    return objectName;
}

// --- パブリックメソッドの実装 ---

/**
 * @brief initialConfigDefinitionsに基づいてConfigItemを初期化します。
 */
void ConfigManager::initializeFromDefinitions()
{
    cbx3_log(LOG_INF, "ConfigManager: Initializing config items.");
    for (const auto &def : initialConfigDefinitions)
    {
        // ConfigItem::ConfigItem(const char *objName, ...) ではなく、
        // ConfigItem::ConfigItem(const std::string &objName, ...) を想定
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

/**
 * @brief ConfigItemをマネージャーに追加します。
 */
void ConfigManager::addConfigItem(std::unique_ptr<ConfigItem> item)
{
    if (!item)
        return;
    std::string key = item->objectName;
    if (configItems.count(key))
    {
        cbx3_log(LOG_WAR, "ConfigManager: Config item '%s' already exists. Overwriting.", key.c_str());
    }
    configItems[key] = std::move(item);
}

/**
 * @brief objectNameに基づいてConfigItemを取得します。
 */
ConfigItem *ConfigManager::getConfigItem(const std::string &objName)
{
    auto it = configItems.find(objName);
    if (it != configItems.end())
    {
        return it->second.get();
    }
    return nullptr;
}

// --- NVS関連のプライベートメソッド ---

/**
 * @brief NVSから全ての設定値をロードします。
 */
bool ConfigManager::loadConfigFromNVS()
{
    if (!_nvs)
        return false;
    cbx3_log(LOG_INF, "ConfigManager: Loading config values from NVS...");

    int loaded_count = 0;
    // 全てのConfigItemをループし、NVSから値を読み込む
    for (const auto &pair : configItems)
    {
        ConfigItem *item = pair.second.get();
        std::string nvsKey = createNVSKey(item->objectName);

        // NVSから現在の値を読み込む。キーが存在しない場合は、item->value (デフォルト値) が返る
        int storedValue = _nvs->getInt(nvsKey.c_str(), item->value);

        // 読み込んだ値を検証付きで設定 (NVSの値が範囲外でないかチェック)
        ConfigItem::SetValueResult setResult = item->setValue(storedValue);
        if (setResult == ConfigItem::SetValueResult::SET_OK)
        {
            loaded_count++;
        }
        else
        {
            // NVSの値が範囲外だった場合は、デフォルト値を再設定して警告
            cbx3_log(LOG_WAR, "ConfigManager: NVS value for '%s' (%d) out of range. Using default: %d.",
                     item->objectName.c_str(), storedValue, item->defaultValue);
            item->value = item->defaultValue; // デフォルト値に戻す (RAM上)
        }
    }

    cbx3_log(LOG_INF, "ConfigManager: Loaded/verified %d items from NVS.", loaded_count);
    return true; // NVSManager::getIntが失敗してもデフォルト値を返すため、処理としては成功とする
}

/**
 * @brief 特定の設定値をNVSに保存します。
 */
bool ConfigManager::saveConfigToNVS(const std::string &objName, int newValue)
{
    if (!_nvs)
        return false;

    std::string nvsKey = createNVSKey(objName);

    // NVSに保存
    if (!_nvs->putInt(nvsKey.c_str(), newValue))
    {
        cbx3_log(LOG_ERR, "ConfigManager: Failed to save key '%s' to NVS.", nvsKey.c_str());
        return false;
    }
    return true;
}

/**
 * @brief 全ての設定値をNVSに保存します。
 */
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

// --- 値の取得（getValue系）の実装 ---

// 値の取得(defaultValue あり)
int ConfigManager::getValue(const std::string &objName, int defaultValue)
{
    ConfigItem *item = getConfigItem(objName);
    if (item)
    {
        return item->getValue();
    }
    cbx3_log(LOG_WAR, "ConfigManager: Item '%s' not found. Returning specified default: %d", objName.c_str(), defaultValue);
    return defaultValue;
}

// 値の取得(defaultValue なし)
int ConfigManager::getValue(const std::string &objName)
{
    ConfigItem *item = getConfigItem(objName);
    if (item)
    {
        return item->getValue();
    }
    cbx3_log(LOG_WAR, "ConfigManager: Item '%s' not found. Returning -1.", objName.c_str());
    return -1;
}

// accessKey を使って設定値を取得するメソッド
int ConfigManager::getValueByAccessKey(const std::string &accessKey)
{
    ConfigItem *item = getConfigItemByAccessKey(accessKey);
    if (item)
    {
        return item->getValue();
    }
    cbx3_log(LOG_WAR, "ConfigManager: Item with accessKey '%s' not found for getValueByAccessKey. Returning -1.", accessKey.c_str());
    return -1;
}

// --- 値の設定（setValue系）の実装 ---
ConfigManager::ConfigManagerResult ConfigManager::setValue(const std::string &objName, int newValue, const std::string &accessKey)
{
    ConfigItem *item = getConfigItem(objName);
    if (!item)
    {
        cbx3_log(LOG_ERR, "ConfigManager: Config item '%s' not found for setValue.", objName.c_str());
        return CONFIG_ERR_ITEM_NOT_FOUND;
    }

    // 1. ConfigItemの値を設定
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
        // 2. NVSに保存
        if (saveConfigToNVS(objName, newValue))
        {
            return CONFIG_OK;
        }
        else
        {
            cbx3_log(LOG_ERR, "ConfigManager: Failed to save config to NVS for objName '%s'.", objName.c_str());
            return CONFIG_ERR_NVS_FAILED; // NVS保存失敗
        }
    }

    // 3. エラーマッピング
    switch (itemSetResult)
    {
    case ConfigItem::SetValueResult::ERR_OUT_OF_RANGE:
        return CONFIG_ERR_INVALID_VALUE_RANGE;
    case ConfigItem::SetValueResult::ERR_INVALID_ACCESS_KEY:
        return CONFIG_ERR_INVALID_ACCESS_KEY;
    default:
        cbx3_log(LOG_ERR, "ConfigManager: Unknown SetValueResult from ConfigItem for '%s': %d", objName.c_str(), itemSetResult);
        return CONFIG_ERR_UNKNOWN;
    }
}

ConfigManager::ConfigManagerResult ConfigManager::setValueByAccessKey(const std::string &accessKey, int newValue)
{
    if (accessKey.empty())
    {
        cbx3_log(LOG_ERR, "ConfigManager: setValueByAccessKey called with empty accessKey.");
        return CONFIG_ERR_INVALID_ACCESS_KEY;
    }

    ConfigItem *foundItem = getConfigItemByAccessKey(accessKey);

    if (!foundItem)
    {
        cbx3_log(LOG_WAR, "ConfigManager: Item with accessKey '%s' not found for setting value.", accessKey.c_str());
        return CONFIG_ERR_ITEM_NOT_FOUND;
    }

    // 1. ConfigItemの値を設定（アクセスキーと範囲チェック）
    ConfigItem::SetValueResult itemSetResult = foundItem->setValueFromExternal(newValue, accessKey);

    if (itemSetResult == ConfigItem::SetValueResult::SET_OK)
    {
        // 2. NVSに保存
        if (saveConfigToNVS(foundItem->objectName, newValue))
        {
            return CONFIG_OK;
        }
        else
        {
            cbx3_log(LOG_ERR, "ConfigManager: Failed to save config to NVS after update.");
            return CONFIG_ERR_NVS_FAILED; // NVS保存失敗エラー
        }
    }
    else
    {
        // 3. エラーマッピング
        cbx3_log(LOG_WAR, "ConfigManager: Failed to update value for accessKey '%s' to %d. Result: %d", accessKey.c_str(), newValue, itemSetResult);
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

// --- ユーティリティ/ヘルパーメソッドの実装 ---

// accessKey で ConfigItem を検索するヘルパーメソッド
ConfigItem *ConfigManager::getConfigItemByAccessKey(const std::string &accessKey)
{
    for (auto const &[objName, u_ptr_item] : configItems) // C++17 構造化束縛を使用
    {
        if (u_ptr_item->accessKey == accessKey)
        {
            return u_ptr_item.get(); // unique_ptr から生ポインタを取得
        }
    }
    return nullptr; // 見つからなかった場合
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

// 全てのConfigItemのobjectNameと現在の値をシリアル出力する
void ConfigManager::printAllConfigValues() const
{
    cbx3_log(LOG_INF, "--- All Config Items List ---");
    if (configItems.empty())
    {
        cbx3_log(LOG_WAR, "No config items defined.");
        return;
    }
    for (const auto &pair : configItems)
    {
        // ConfigItem クラスの print() メソッドを呼び出す
        pair.second->print();
    }
    cbx3_log(LOG_INF, "--- End of List---------------");
}
