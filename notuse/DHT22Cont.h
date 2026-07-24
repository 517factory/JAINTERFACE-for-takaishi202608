#pragma once

/**
 *  DHT22 Control
 *
 *  517Factory
 * */

#include <DHT.h>
#include "header.h"
#include "Debug.h"

#define DHTTYPE DHT22

class DHT22Cont
{
private:
  volatile bool DHTState; //  1 or 0
  volatile int PinNo;
  char StrMsg[32];

public:
  volatile float HUM;
  volatile float TMP;
  DHT22Cont(int pin);
  void DHT22begin();
  void ReadData();
  float isTMP();
  float isHUM();
  bool isState();
  String getStrMessage();
};
