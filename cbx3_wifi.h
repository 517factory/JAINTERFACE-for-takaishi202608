#pragma once
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "header.h"
#include "Debug.h"

/**
 *  ESP32-S3 Wifi-Server(SoftAP) for cocobox3
 *  LittleFS版
 *
 *  517Factory
 * */

#define MAX_WIFI_RCVD_LENGTH 255

// コールバック関数型の定義
typedef std::function<void()> StatusUpdateCallback;
typedef std::function<void()> LockCallback;

class cbxWiFi
{
private:
    const char *ssid = "CocoboxAccessPoint";              // SSID
    const char *password = "1234567890";                  // PASSWORD
    const IPAddress ip = IPAddress(192, 168, 123, 45);    // IP-Address
    const IPAddress subnet = IPAddress(255, 255, 255, 0); // Subnet-mask
    const char *hostname = "cocobox";                     // host name
    const int channel = 10;                               // WiFiChannell
    WebServer server;
    fs::FS &filesystem; // 参照としてLittleFSインスタンスを保持
    void startMDNS();   // startup mDNS
    void stopMDNS();    // stop mDNS
    char *wifiRcvd;
    bool rcvdFlg;
    bool serverActive;
    StatusUpdateCallback onStatusUpdate; // コールバック関数ポインタ
    LockCallback onLock;                 // LOCK操作用コールバック
    LockCallback onUnlock;               // UNLOCK操作用コールバック
    bool sendLittleFSFile(const char *path, const char *contentType);
    void sendJsonResponse(int code, const char *status, const char *message);
    void handleStaticFile(const char *path, const char *contentType);
    String convertToJSON(const StatusData &status);

public:
    explicit cbxWiFi(fs::FS &fs_instance);
    ~cbxWiFi();
    void startWifi();
    void stopWifi();
    void startServer();
    void stopServer();
    bool isConnected();
    void handleClient();
    void handleSubmit();
    char *getMessage();
    bool isMessage();
    StatusData status;
    void setStatusUpdateCallback(StatusUpdateCallback callback); // StatusUpdateコールバック設定
    void setLockCallback(LockCallback lockCallback);             // 施錠用コールバック設定
    void setUnlockCallback(LockCallback unlockCallback);         // 解錠用コールバック設定
};
