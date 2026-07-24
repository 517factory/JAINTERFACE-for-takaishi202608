#pragma once

#include <Arduino.h>
#include <Preferences.h>

class NVSManager
{
public:
  bool begin(const char *name);
  void end();

  int32_t getInt(const char *key, int32_t defaultValue = 0);
  bool putInt(const char *key, int32_t value);

  size_t getString(const char *key, char *buffer, size_t maxLen);
  bool putString(const char *key, const char *value);

private:
  Preferences _preferences;
};
