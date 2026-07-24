#pragma once

#define GPRS_APN "soracom.io"
#define GPRS_USER "sora"
#define GPRS_PASS "sora"
#define AUTO_SELECT_CARRIER 0 // 1：キャリア自動選択　この場合下記のPREFERRED_CARRIERは無視される
// 以下、優先キャリア設定だが、基本自動選択を推奨
// #define PREFERRED_CARRIER "44010" // 優先キャリア : Docomo
#define PREFERRED_CARRIER "44020" // 優先キャリア : Softba

#define LTE_BAND_AUTO_MB_PRI "1,3,8,19,28"
#define LTE_BAND_AUTO_PB_PRI "19,8,28,1,3"

#define LTE_BAND_DOCOMO_MB_PRI "1,3,19,28"
#define LTE_BAND_DOCOMO_PB_PRI "19,28,1,3"

#define LTE_BAND_SB_MB_PRI "1,3,8,28"
#define LTE_BAND_SB_PB_PRI "8,28,1,3"
#define MQTT_BROKER "beam.soracom.io"
