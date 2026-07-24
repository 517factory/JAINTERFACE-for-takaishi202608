/**
 *  ESP32-S3 Wifi-Server(SoftAP) for cocobox3
 *
 *  517Factory
 * */
#include "cbx3_wifi.h"

cbxWiFi::cbxWiFi() : server(80)
{
  initSPIFS();
  wifiRcvd =
      new char[MAX_WIFI_RCVD_LENGTH];
  rcvdFlg = false;
  strcpy(wifiRcvd, "NODATA");
}

cbxWiFi::~cbxWiFi()
{
  if (serverActive)
  {
    server.stop(); // Webサーバーを停止
  }
  delete[] wifiRcvd;  // 動的に割り当てられたメモリを解放
  wifiRcvd = nullptr; // ヌルポインタに設定
}

void cbxWiFi::startWifi()
{
  char ipString[16];
  cbx3_log(LOG_INF, "Start WiFi");

  rcvdFlg = false; // 受信フラグのクリア

  strcpy(wifiRcvd, "NODATA");
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password, channel);
  WiFi.softAPConfig(ip, ip, subnet);
  IPAddress IP = WiFi.softAPIP();
  sprintf(ipString, "%d.%d.%d.%d", IP[0], IP[1], IP[2], IP[3]);
  cbx3_log(LOG_INF, "AP IP address: %s", ipString);
  startMDNS();
  serverActive = false;
  startServer();
}

void cbxWiFi::stopWifi()
{
  stopServer();
  WiFi.softAPdisconnect(true);
  cbx3_log(LOG_INF, "stop WiFi server");
  stopMDNS();
}

void cbxWiFi::startServer()
{
  if (!serverActive)
  {
    server.on("/", HTTP_GET, [this]()
              { handleStaticFile("/index.html", "text/html"); });
    server.on("/style.css", HTTP_GET, [this]()
              { handleStaticFile("/style.css", "text/css"); });
    server.on("/control", HTTP_GET, [this]()
              { handleStaticFile("/control.html", "text/html"); });
    server.on("/form", HTTP_GET, [this]()
              { handleStaticFile("/form.html", "text/html"); });

    // 変数値をJSONで返すエンドポイント
    server.on("/status.json", [this]()
              {
                // コールバックを呼び出してステータスを更新
                if (this->onStatusUpdate) // コールバック関数が設定されていれば実行
                {
                  this->onStatusUpdate(); // 設定されたコールバック関数を実行
                } // ステータスを更新
                String json = this->convertToJSON(this->status); // JSONデータに変換
                server.send(200, "application/json", json);      // JSONを返す
              });

    server.on("/submit", HTTP_POST, [this]()
              { cbxWiFi::handleSubmit(); });

    // 施錠EndPoint
    server.on("/lock", HTTP_POST, [this]()
              {
      JsonDocument jsonDoc;
      if (onLock) {
        onLock();  // LOCK操作のコールバックを呼び出す
        sendJsonResponse(200, "success", "LOCK action executed");
      } else {
        sendJsonResponse(500, "error", "LOCK callback not set");
      } });

    // 解錠EndPoint
    server.on("/unlock", HTTP_POST, [this]()
              {
      JsonDocument jsonDoc;
      if (onUnlock) {
        onUnlock();  // UNLOCK操作のコールバックを呼び出す
        sendJsonResponse(200, "success", "UNLOCK action executed");
      } else {
        sendJsonResponse(500, "error", "UNLOCK callback not set");
      } });

    server.begin();
    serverActive = true;
    cbx3_log(LOG_INF, "HTTP server started");
  }
}

void cbxWiFi::stopServer()
{
  if (serverActive)
  {
    server.stop(); // Webサーバーを停止
    serverActive = false;
    cbx3_log(LOG_INF, "HTTP server stopped");
  }
}

void cbxWiFi::startMDNS()
{
  if (!MDNS.begin(hostname))
  {
    cbx3_log(LOG_ERR, "Error setting up MDNS responder!");
    while (1)
    {
      cbx_wait(1000);
    }
  }
  cbx3_log(LOG_INF, "mDNS responder started");
}

void cbxWiFi::stopMDNS()
{
  MDNS.end(); // mDNSを停止する
  cbx3_log(LOG_INF, "mDNS responder stopped");
}

void cbxWiFi::initSPIFS()
{
  if (!SPIFFS.begin(true))
  {
    cbx3_log(LOG_ERR, "An error occurred while mounting SPIFFS");
    return;
  }
}

bool cbxWiFi::isConnected()
{
  char ipString[16];
  IPAddress apIP = WiFi.softAPIP();
  sprintf(ipString, "%d.%d.%d.%d", apIP[0], apIP[1], apIP[2], apIP[3]);
  cbx3_log(LOG_INF, "CHECK AP IP address: %s", ipString);
  return apIP != IPAddress(0, 0, 0, 0);
}

void cbxWiFi::handleSubmit()
{
  if (server.hasArg("inputString"))
  {
    String message = server.arg("inputString");
    cbx3_log(LOG_INF, "Received message: %s", message.c_str());
    server.sendHeader("Content-Language", "ja");
    server.send(200, "text/html",
                "Message received: " + message +
                    "<br><br><a href=\"/form\">Back to Form</a>");

    memset(wifiRcvd, 0, MAX_WIFI_RCVD_LENGTH);
    strncpy(wifiRcvd, message.c_str(), MAX_WIFI_RCVD_LENGTH - 1);
    wifiRcvd[MAX_WIFI_RCVD_LENGTH - 1] = '\0';

    rcvdFlg = true;
  }
  else
  {
    server.send(400, "text/plain", "Bad Request");
  }
}

bool cbxWiFi::isMessage() { return rcvdFlg; }

char *cbxWiFi::getMessage()
{
  rcvdFlg = false;
  return wifiRcvd;
}

void cbxWiFi::handleClient() { server.handleClient(); }

// 構造体をJSON文字列に変換する関数
String cbxWiFi::convertToJSON(const StatusData &status)
{
  JsonDocument jsonDoc;

  // 構造体データをJSONにマッピング
  jsonDoc["FWVersion"] = status.FWVersion;
  jsonDoc["timecode"] = status.timecode;
  jsonDoc["isKSUexist"] = status.isKSUexist;
  jsonDoc["isKeyLocked"] = status.isKeyLocked;
  jsonDoc["isDoorClosed"] = status.isDoorClosed;
  jsonDoc["isACPower"] = status.isACPower;
  jsonDoc["temperature"] = status.temperature;
  jsonDoc["humidity"] = status.humidity;
  jsonDoc["BatteryVolt"] = status.BatteryVolt;

  // JSON文字列を生成
  String output;
  serializeJson(jsonDoc, output);

  return output; // JSON文字列を返す
}

// ステータス更新コールバックを登録
void cbxWiFi::setStatusUpdateCallback(StatusUpdateCallback callback)
{
  onStatusUpdate = callback;
}

void cbxWiFi::setLockCallback(LockCallback lockCallback)
{
  onLock = lockCallback;
}

void cbxWiFi::setUnlockCallback(LockCallback unlockCallback)
{
  onUnlock = unlockCallback;
}

bool cbxWiFi::sendSPIFFSFile(const char *path, const char *contentType)
{
  File file = SPIFFS.open(path, "r"); // SPIFFSからファイルを開く
  if (!file)
  {
    cbx3_log(LOG_ERR, "File not found: %s", path);    // エラーログ出力
    server.send(404, "text/plain", "File not found"); // 404エラーを送信
    return false;                                     // ファイルが見つからなかった場合はfalseを返す
  }
  server.streamFile(file, contentType); // ファイルの内容をストリーム送信
  file.close();                         // ファイルを閉じる
  return true;                          // ファイル送信に成功した場合はtrueを返す
}

void cbxWiFi::sendJsonResponse(int code, const char *status, const char *message)
{
  JsonDocument jsonDoc;
  jsonDoc["status"] = status;
  jsonDoc["message"] = message;
  String json;
  serializeJson(jsonDoc, json);
  server.send(code, "application/json", json);
}

void cbxWiFi::handleStaticFile(const char *path, const char *contentType)
{
  if (!sendSPIFFSFile(path, contentType))
  {
    cbx3_log(LOG_WAR, "Failed to send static file: %s", path); // 警告ログに変更
  }
}
