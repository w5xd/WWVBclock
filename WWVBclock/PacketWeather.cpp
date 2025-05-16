#include <Arduino.h>
#include <RFM69registers.h>
#include "PacketWeather.h"
#include "WWVBclock.h"
#include "WwvbClockDefinitions.h"

#define MONITOR_RSSI

namespace { 
    const uint8_t GATEWAY_NODEID = 1;
    char reportbuf[sizeof(RFM69::DATA) + 1];
    const float NO_DATA = -999.9;

    float parseForColon(char flag, const char* p, uint8_t len)
    {   // help parse the Wireless Thermometer packet
        uint8_t c = len;
        for (;;)
        {
            if (!*p)
                return NO_DATA;
            if (c == 0)
                return NO_DATA;
            if (p[0] == flag && p[1] == ':')
            {
                p += 2;  c -= 2;
                return atof(p);
            } else
            {
                p += 1;
                c -= 1;
            }
        }
        return NO_DATA;
    }
}
 
PacketWeather::PacketWeather(int nSS_pin, int int_Pin) 
    : radio(nSS_pin, int_Pin)
    , radioSetupOK(false)
    , m_prevRgF(0x7FFF) // far away from expected responses
    , indoorTemperatureSensorMask(0)
    , outdoorTemperatureSensorMask(0)
    , raingaugeSensorMask(0)
    , m_monitorRSSI(false)
    , m_sleepBegan(millis())
    , m_clock(0)
{}

void ::PacketWeather::setNotify(ClockNotification*p)
{
    m_clock = p;
}

void PacketWeather::setup()
{
     if (radioConfiguration.NodeId() != 0xff &&
        radioConfiguration.NetworkId() != 0xff)
    {
        bool ok = radio.initialize(radioConfiguration.FrequencyBandId(),
            radioConfiguration.NodeId(), radioConfiguration.NetworkId());
        if (ok)
        {
            uint32_t freq;
            if (radioConfiguration.FrequencyKHz(freq))
                radio.setFrequency(freq*1000);
            radio.spyMode(true);
            radioSetupOK = radio.getFrequency() != 0;
        }
#if USE_SERIAL
#endif
    }
    else
    {
    }

    if (radioSetupOK)
    {
        radio.setHighPower(); // Always use this for RFM69HCW
        // Turn on encryption if so configured:
        const char* key = radioConfiguration.EncryptionKey();
        if (radioConfiguration.encrypted())
            radio.encrypt(key);
    }
}

void PacketWeather::radioPrintInfo()
{
#if USE_SERIAL
    Serial.print(F("Node "));
    Serial.print(radioConfiguration.NodeId(), DEC);
    Serial.print(F(" on network "));
    Serial.print(radioConfiguration.NetworkId(), DEC);
    Serial.print(F(" band "));
    Serial.print(radioConfiguration.FrequencyBandId(), DEC);
    Serial.print(F(" key "));
    radioConfiguration.printEncryptionKey(Serial);
    Serial.println();
    if (radioSetupOK)
    {
        Serial.print(F("FreqKHz="));
        Serial.println(radio.getFrequency()/1000);
        Serial.print(F("Bitrate="));
        Serial.println(radio.getBitRate());
    }
    else
        Serial.println("Radio init FAILED");

#endif
}

void PacketWeather::radioPrintRegs()
{
#if USE_SERIAL
    if (radioSetupOK)
         radio.readAllRegs();
    else
        Serial.println("Radio not setup");
#endif
}

bool PacketWeather::ProcessCommand(const char* cmd, uint8_t len, uint8_t senderid, bool toMe)
{
    if (toMe && radioConfiguration.ApplyCommand(cmd))
         return true;

    if (strncmp(cmd, "SENDGATEWAY ", 12) == 0)
    {
        const char *q = cmd + 12;
        if (strncmp(q, "R ", 2) == 0)
        {
            q += 2;
            DEBUG_STATEMENT( bool status = )
                 radio.sendWithRetry(GATEWAY_NODEID, q, strlen(q)+1);
            DEBUG_OUTPUT1("SendGateway status ");
            DEBUG_OUTPUT1(status ? "ACK" : "NAK");
            DEBUG_OUTPUT1('\n');
        }
        else
            radio.send(GATEWAY_NODEID, q, strlen(q)+1);
        return true;
    }
 
    static const int MAX_SENDER_ID = 32;
    if (senderid <= MAX_SENDER_ID && senderid > 0)
    {   // is this a weather sensor we have been asked to monitor?
        uint32_t mask = 1L << (senderid-1);
        if (0 != (mask & (indoorTemperatureSensorMask | outdoorTemperatureSensorMask)))
        {
             // Example thermometers:
            //      C:49433, B:244, T:+20.37
            //      C:1769, B:198, T:+20.58 R:45.46
            auto tCx10 = parseForColon('T', cmd, len);
            if (tCx10 == NO_DATA)
                return false;
            if (m_clock)
            {
                if (0 != (mask & indoorTemperatureSensorMask))
                    m_clock->notifyIndoorTemp(tCx10);
                else if (0 != (mask & outdoorTemperatureSensorMask))
                    m_clock->notifyOutdoorTemp(tCx10);
            }
            DEBUG_OUTPUT1(F("received temperature. id="));
            DEBUG_OUTPUT1(static_cast<int>(senderid));
            DEBUG_OUTPUT1(F(" tCx10="));
            DEBUG_OUTPUT1(tCx10);
            DEBUG_OUTPUT1('\n');
            //int16_t rhx10 = parseForColon('R', cmd, len);
        }
        else if (mask & raingaugeSensorMask)
        {
            DEBUG_OUTPUT1(F("received rainguage. id="));
            DEBUG_OUTPUT1(static_cast<int>(senderid));
            DEBUG_OUTPUT1(F(" string='"));
            DEBUG_OUTPUT1(cmd);
            DEBUG_OUTPUT1('\'');
            const char *isF = strstr(cmd, " F: ");
            const char *isRG = strstr(cmd, " RG: ");
            if (isF != 0 && isRG != 0)
            {
                auto rg = atoi(isRG + 5);
                if (rg != 0)
                {
                    auto f = atoi(isF + 4);
                    int16_t diffF = m_prevRgF - f;
                    DEBUG_OUTPUT1(" m_prevRgF=");
                    DEBUG_OUTPUT1(m_prevRgF);
                    DEBUG_OUTPUT1(" diffF=");
                    DEBUG_OUTPUT1(diffF);
                    if (diffF < 0)
                        diffF = -diffF;
                    if (diffF >= 1000)
                    {   // 1000 is magic number. that is what the Silicon Labs magnetometer reads
                        m_prevRgF = f;
                        if (m_clock)
                            m_clock->notifyRainmm(rg);
                    }
                }
                else
                    return false;
            }
            else
                return false;
            DEBUG_OUTPUT1('\n');      
        }
        else
            return false;
        return true;
    }
    return false;
}

#if defined(MONITOR_RSSI)
namespace MonitorRSSI {
    const int NUM_RSSI_PWR2 = 5;
    const int NUM_RSSI_RECORDS = 1 << NUM_RSSI_PWR2;
    const int MONITOR_RSSI_MSEC = 100;
    const int PRINT_RSSI_MSEC = 2000;
    int whichRssiRecord = 0;
    int16_t rssiRecord[NUM_RSSI_RECORDS];
    auto prevRssiRecord = millis();
    auto prevRssiPrint = millis();
}
#endif

void PacketWeather::loop()
{
   if (!radioSetupOK)
        return;
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wunused-but-set-variable"
    static auto stamp = millis();
    auto now = millis();
    #pragma GCC diagnostic pop
    static bool printMillis = false;
    if (printMillis)
    {
#if USE_SERIAL > 0
        Serial.print(F("PacketWeather:loop delay msec="));
        Serial.println(millis() - stamp);
#endif
        printMillis = false;
    }
     // check radio
    if (radio.receiveDone()) // Got a packet over the radio
    {   // RFM69 ensures no trailing zero byte when buffer is full
        memset(reportbuf, 0, sizeof(reportbuf));
        memcpy(reportbuf, &radio.DATA[0], sizeof(radio.DATA));
        auto targetId = radio.TARGETID;
        unsigned senderId = radio.SENDERID;
        DEBUG_STATEMENT(auto rssi = radio.RSSI);
        DEBUG_STATEMENT(int16_t rssi1 = radio.readRSSI());
        bool toMe = radioConfiguration.NodeId() == targetId;
        auto ackR = radio.ACKRequested();

    #if defined(DEBUG_TO_SERIAL)
        Serial.print('"');
        Serial.print(reportbuf);
        Serial.print("\" ");
        Serial.print("Received. RSSI=");
        Serial.print(rssi);
        Serial.print(" now is: ");
        Serial.print(rssi1);
        Serial.print(" node:");
        Serial.println(senderId);
    #endif

        if (toMe && ackR)
        {
            stamp = millis();
            printMillis = true;
            radio.sendACK();
#if USE_SERIAL > 0
            Serial.print("delay in SendACK ");
            Serial.println(millis() - stamp);
#endif
        }

        routeCommand(reportbuf, sizeof(radio.DATA), static_cast<uint8_t>(senderId), toMe);
     }
#if defined(MONITOR_RSSI)
     if (now - MonitorRSSI::prevRssiRecord >= MonitorRSSI::MONITOR_RSSI_MSEC)
     {
        MonitorRSSI::rssiRecord[MonitorRSSI::whichRssiRecord++] = radio.readRSSI();
        if (MonitorRSSI::whichRssiRecord >= MonitorRSSI::NUM_RSSI_RECORDS)
            MonitorRSSI::whichRssiRecord = 0;
        MonitorRSSI::prevRssiRecord = now;
     }
#if USE_SERIAL > 0
     if (m_monitorRSSI && (now - MonitorRSSI::prevRssiPrint >= MonitorRSSI::PRINT_RSSI_MSEC))
     {
        MonitorRSSI::prevRssiPrint = now;
        int32_t total = 0;
        for (int i = 0; i < MonitorRSSI::NUM_RSSI_RECORDS; i += 1)
            total += MonitorRSSI::rssiRecord[i];
        total >>= MonitorRSSI::NUM_RSSI_PWR2;
        uint16_t var = 0;
        for (int i = 0; i < MonitorRSSI::NUM_RSSI_RECORDS; i += 1)
        {
            int16_t t = MonitorRSSI::rssiRecord[i] - total;
            var += t * t;
        }
        Serial.print("RSSI AVG: ");
        Serial.print(total);
        Serial.print(" VAR: ");
        Serial.println(var);
     }
#endif
#endif
}

void PacketWeather::MonitorRSSI(bool b)
{
    m_monitorRSSI = b;
}

void PacketWeather::SetThermometerIdMasks(uint32_t indoor, uint32_t outdoor)
{
    indoorTemperatureSensorMask = indoor;
    outdoorTemperatureSensorMask = outdoor;
}

void PacketWeather::SetRaingaugeIdMask(uint32_t m)
{
    raingaugeSensorMask = m;
}

void PacketWeather::SendRadioMessage(int node, const char *c)
{
#if USE_SERIAL > 0
    Serial.print("Send radio ");
    Serial.print(node);
    Serial.print(' ');
    Serial.println(c);
#endif
    radio.sendWithRetry(node, c, strlen(c));
}
