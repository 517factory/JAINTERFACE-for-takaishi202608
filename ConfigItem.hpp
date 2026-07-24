// ConfigItem.hpp
#pragma once

#include <string>
#include <memory>
// #include <vector> // ConfigItem.hpp自体には不要ですが、全体の構造によっては必要

class ConfigItem
{
public:
  // ConfigManagerと通信するための設定結果列挙型
  enum SetValueResult
  {
    SET_OK = 0,
    ERR_OUT_OF_RANGE,
    ERR_INVALID_ACCESS_KEY,
    ERR_UNKNOWN
  };

  // --- 設定項目の定義情報 ---
  const std::string objectName;
  const std::string description;
  const int defaultValue;
  const int minValue;
  const int maxValue;
  const std::string accessKey; // SETコマンドの電文と一致させるキー

  // --- 実行中の現在の値 ---
  int value;

  // コンストラクタ
  ConfigItem(const std::string &objName, const std::string &desc, int val, int minVal, int maxVal, const std::string &accKey);

  // --- パブリックメソッド ---

  /**
   * @brief 現在の設定値を取得します。
   */
  int getValue() const;

  /**
   * @brief 値を設定します。範囲チェックのみを行います。
   * @param newValue 設定する値。
   * @return 結果を示す SetValueResult。
   */
  SetValueResult setValue(int newValue);

  /**
   * @brief 外部からの値設定を受け付けます。アクセスキーと範囲チェックを行います。
   * @param newValue 設定する値。
   * @param receivedAccessKey 受信したアクセスキー。
   * @return 結果を示す SetValueResult。
   */
  SetValueResult setValueFromExternal(int newValue, const std::string &receivedAccessKey);

  /**
   * @brief 設定情報をログに出力します。
   */
  void print() const;

private:
  /**
   * @brief 値が有効範囲内かチェックします。
   */
  bool checkRange(int val) const;
};
