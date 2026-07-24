#pragma once

#include <vector>
#include <map>
#include <memory>
#include <string>

#include "config_definitions.h" // ConfigItemDefinitionとinitialConfigDefinitions
#include "NVSManager.hpp"       // NVSManagerをインクルード

// ConfigItemクラスは、ConfigManager.h または別のヘッダファイルで定義されている必要があります
// (ここでは、int型の値を持つシンプルなConfigItemを想定)
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

  // 公開メンバー (簡単のため)
  std::string objectName;
  std::string description;
  int defaultValue;
  int minValue;
  int maxValue;
  std::string accessKey;

  // 現在の値
  int value;

  // コンストラクタ
  ConfigItem(const char *objName, const char *desc, int defVal, int minVal, int maxVal, const char *accKey)
      : objectName(objName), description(desc), defaultValue(defVal), minValue(minVal), maxValue(maxVal), accessKey(accKey), value(defVal) {}

  int getValue() const { return value; }

  // 内部的な値の設定 (範囲チェックのみ)
  SetValueResult setValue(int newValue)
  {
    if (newValue < minValue || newValue > maxValue)
    {
      return ERR_OUT_OF_RANGE;
    }
    value = newValue;
    return SET_OK;
  }

  // 外部からの値の設定 (アクセスキーチェックと範囲チェック)
  SetValueResult setValueFromExternal(int newValue, const std::string &key)
  {
    if (!accessKey.empty() && accessKey != key)
    {
      return ERR_INVALID_ACCESS_KEY;
    }
    return setValue(newValue); // 範囲チェックを呼び出す
  }

  // ログ出力用 (Debug.hが必要)
  void print() const; // 実装は .cpp に移動
};

class ConfigManager
{
public:
  // ファイルシステムエラーをNVSエラーに置き換え
  enum ConfigManagerResult
  {
    CONFIG_OK = 0,
    CONFIG_ERR_ITEM_NOT_FOUND,
    CONFIG_ERR_INVALID_VALUE_RANGE,
    CONFIG_ERR_INVALID_ACCESS_KEY,
    CONFIG_ERR_NVS_FAILED, // NVS関連のエラー
    CONFIG_ERR_UNKNOWN
  };

  // コンストラクタはNVSManagerへのポインタを受け取らないようにする (シンプルに)
  ConfigManager();

  // ファイルシステム依存を排除し、NVSManagerをセットして初期化する
  bool begin(NVSManager *nvs, const char *nvs_namespace = "config");

  // NVSをクローズする
  void end();

  // 以前のメソッドを維持
  void initializeFromDefinitions();
  void addConfigItem(std::unique_ptr<ConfigItem> item);
  ConfigItem *getConfigItem(const std::string &objName);
  ConfigItem *getConfigItemByAccessKey(const std::string &accessKey);
  const std::map<std::string, std::unique_ptr<ConfigItem>> &getAllConfigItems() const { return configItems; }

  int getValue(const std::string &objName, int defaultValue);
  int getValue(const std::string &objName);
  ConfigManagerResult setValue(const std::string &objName, int newValue, const std::string &accessKey);
  int getValueByAccessKey(const std::string &accessKey);
  ConfigManagerResult setValueByAccessKey(const std::string &accessKey, int newValue);
  int getMinValueByAccessKey(const std::string &accessKey);
  int getMaxValueByAccessKey(const std::string &accessKey);
  void printAllConfigValues() const;

private:
  // ファイルシステムやパスを削除し、NVS関連のメンバーを追加
  NVSManager *_nvs = nullptr;
  std::string _nvsNamespace;

  std::map<std::string, std::unique_ptr<ConfigItem>> configItems;

  // NVSキーを生成する（例: "polltimer" -> "polltimer"）
  std::string createNVSKey(const std::string &objectName);

  // ファイル読み書きメソッドをNVS読み書きに置き換える
  bool loadConfigFromNVS();
  bool saveConfigToNVS(const std::string &objName, int newValue);
  bool saveAllConfigToNVS();
};
