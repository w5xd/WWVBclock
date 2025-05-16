#pragma once
#include <RadioConfiguration.h>
#include <RFM69.h>
#include "WWVBclock.h"

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
        RFM69 radio;
        RadioConfiguration radioConfiguration;
        bool radioSetupOK ;
        int16_t m_prevRgF;
        uint32_t indoorTemperatureSensorMask;
        uint32_t outdoorTemperatureSensorMask;
        uint32_t raingaugeSensorMask;
        bool m_monitorRSSI;
        unsigned long m_sleepBegan;
        ClockNotification *m_clock;
};