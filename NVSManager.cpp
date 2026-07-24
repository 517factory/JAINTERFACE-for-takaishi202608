#include "NVSManager.hpp"

bool NVSManager::begin(const char *name)
{
    return _preferences.begin(name, false);
}

void NVSManager::end()
{
    _preferences.end();
}

int32_t NVSManager::getInt(const char *key, int32_t defaultValue)
{
    return _preferences.getInt(key, defaultValue);
}

bool NVSManager::putInt(const char *key, int32_t value)
{
    return (_preferences.putInt(key, value) > 0);
}

size_t NVSManager::getString(const char *key, char *buffer, size_t maxLen)
{
    return _preferences.getString(key, buffer, maxLen);
}

bool NVSManager::putString(const char *key, const char *value)
{
    return (_preferences.putString(key, value) > 0);
}
