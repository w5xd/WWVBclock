#pragma once
#include <RadioConfiguration.h>
#include <RFM69.h>
#include "WWVBclock.h"

class RFM69delayCanSend : public RFM69
{
    public:
        RFM69delayCanSend(int nss, int irq) : RFM69(nss, irq, true)
        {}
        
        bool canSend() override 
        {       
            /* this delay(1) is inspired by the  #ifdef ESP8266 in the library code
                https://github.com/LowPowerLab/RFM69
            */
            auto ret = RFM69::canSend();
            if (!ret)
                delay(1);
            return ret;
        }
};

class PacketWeather {
    public: 
        PacketWeather(int nSS_pin, int int_Pin);
        void setup();
        void loop();
        void radioPrintInfo();
        void radioPrintRegs();
        void setNotify(ClockNotification*);
        void SetThermometerIdMasks(uint32_t indoor, uint32_t outdoor);
        void SetRaingaugeIdMask(uint32_t);
        void SendRadioMessage(int node, const char *m);
        void MonitorRSSI(bool);
        bool ProcessCommand(const char* cmd, uint8_t len, uint8_t senderid, bool toMe);
    protected:
        RFM69delayCanSend radio;
        RadioConfiguration radioConfiguration;
        bool radioSetupOK ;
        uint32_t indoorTemperatureSensorMask;
        uint32_t outdoorTemperatureSensorMask;
        uint32_t raingaugeSensorMask;
        bool m_monitorRSSI;
        uint16_t prevRGcount;
        unsigned long m_sleepBegan;
        ClockNotification *m_clock;
};