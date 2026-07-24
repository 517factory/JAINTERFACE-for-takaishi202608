#include "Arduino.h"
#include "DHT22Cont.h"
using namespace std;

/*
DHT22温湿度センサー制御

517Factory
*/

DHT dht(DHTPIN, DHTTYPE);

DHT22Cont::DHT22Cont(int pin)
{
    DHTState = 0;
    HUM = 0;
    TMP = 0;
}

void DHT22Cont::ReadData()
{
    HUM = dht.readHumidity();
    TMP = dht.readTemperature();
    if (isnan(HUM) || isnan(TMP))
    {
        cbx3_log(LOG_WAR, ("Failed to read from DHT sensor!"));
        TMP = (float)99.9;
        HUM = (float)99.9;
        DHTState = 1;
    }
    else
    {
        DHTState = 0;
    }
}

void DHT22Cont::DHT22begin()
{
    dht.begin();
}

float DHT22Cont::isHUM()
{
    return HUM;
}

float DHT22Cont::isTMP()
{
    return TMP;
}

bool DHT22Cont::isState()
{
    return DHTState;
}

String DHT22Cont::getStrMessage()
{
    ReadData();
    char TStrD[5];
    char HStrD[5];

    dtostrf(TMP, 2, 1, TStrD);
    dtostrf(HUM, 2, 1, HStrD);
    String StrMsg = String(SEND_COMMAND_TMP) + String(TStrD) + "," + String(SEND_COMMAND_HUM) + String(HStrD);
    return StrMsg;
}