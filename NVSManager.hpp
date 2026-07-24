#pragma once // ヘッダファイル全体を一度だけインクルードすることを保証する

#include <Arduino.h>
#include <Preferences.h> // ESP32 NVSライブラリ

// NVSManagerクラス
class NVSManager
{
public:
  // 初期化と終了
  bool begin(const char *name); // 通常の文字列 ("namespace") で呼び出す
  void end();

  // 整数値 (int32_t) の読み書き
  int32_t getInt(const char *key, int32_t defaultValue = 0);
  bool putInt(const char *key, int32_t value);

  // 文字列の読み書き
  // 戻り値: 読み込んだバイト数 (0はエラーまたは空)
  size_t getString(const char *key, char *buffer, size_t maxLen);
  bool putString(const char *key, const char *value);

private:
  Preferences _preferences;
};
