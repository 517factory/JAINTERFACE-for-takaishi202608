#include "NVSManager.hpp"

bool NVSManager::begin(const char *name)
{
    // name (namespace) は const char* なので、そのまま使用
    return _preferences.begin(name, false);
}

void NVSManager::end()
{
    _preferences.end();
}

// ----------------------------------------------------
// 整数値の読み書き

int32_t NVSManager::getInt(const char *key, int32_t defaultValue)
{
    // key は const char* なので、そのまま使用
    return _preferences.getInt(key, defaultValue);
}

bool NVSManager::putInt(const char *key, int32_t value)
{
    // key は const char* なので、そのまま使用
    // putInt は書き込まれたバイト数を返す
    return (_preferences.putInt(key, value) > 0);
}

// ----------------------------------------------------
// 文字列の読み書き

size_t NVSManager::getString(const char *key, char *buffer, size_t maxLen)
{
    // 読み込んだ文字列を直接RAMバッファに格納
    return _preferences.getString(key, buffer, maxLen);
}

bool NVSManager::putString(const char *key, const char *value)
{
    // key と value は const char* なので、そのまま使用
    // putString は書き込まれたバイト数を返す
    return (_preferences.putString(key, value) > 0);
}
