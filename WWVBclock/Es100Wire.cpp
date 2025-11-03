#include <Wire.h>
#include "WwvbClockDefinitions.h"
#include "Es100Wire.h"

static const uint8_t ES100_SLAVE_ADDR (0x32);

// ES100 API register addresses
enum {
    ES100_CONTROL0_REG       =0x00,
    ES100_CONTROL1_REG       =0x01,
    ES100_IRQ_STATUS_REG     =0x02,
    ES100_STATUS0_REG        =0x03,
    ES100_YEAR_REG           =0x04,
    ES100_MONTH_REG          =0x05,
    ES100_DAY_REG            =0x06,
    ES100_HOUR_REG           =0x07,
    ES100_MINUTE_REG         =0x08,
    ES100_SECOND_REG         =0x09,
    ES100_NEXT_DST_MONTH_REG =0x0A,
    ES100_NEXT_DST_DAY_REG   =0x0B,
    ES100_NEXT_DST_HOUR_REG  =0x0C,
    ES100_DEVICE_ID_REG      =0x0D,
};

static const uint8_t CONTROL0_START = 1;
static const uint8_t CONTROL0_ANT1_OFF = 1 << 1;
static const uint8_t CONTROL0_ANT2_OFF = 1 << 2;
static const uint8_t CONTROL0_START_ANT = 1 << 3;

static const uint8_t STATUS_0_RXOK = 1;
static const uint8_t STATUS_0_DST0 = 1 << 5;
static const uint8_t STATUS_0_DST1 = 1 << 6;

static const uint8_t IRQSTATUS_RX_COMPLETE = 1;

static const uint8_t DST_HOUR_SPECIAL3 = 1 << 7;
static const uint8_t DST_HOUR_SPECIAL2 = 1 << 6;
static const uint8_t DST_HOUR_SPECIAL1 = 1 << 5;
static const uint8_t DST_HOUR_SPECIAL0 = 1 << 4;
static const uint8_t DST_HOUR_SPECIAL = DST_HOUR_SPECIAL3 | DST_HOUR_SPECIAL2 | DST_HOUR_SPECIAL1 | DST_HOUR_SPECIAL0;

Es100Wire::Es100Wire(int irqPin, int enablePin, TwoWire &wire) 
    :irqPin(irqPin)
    ,enablePin(enablePin)
    ,Wire(wire)
    ,m_state(ReceptionState::SHUTDOWN)
    ,m_time(0)
    ,m_status0(0)
    ,m_yearLstRec(0)
    ,m_monthLstRec(0)
    ,m_dayLstRec(0)
    ,m_hourLstRec(0)
    ,m_nextDstMonthStatus(-1)
    ,m_nextDstDayStatus(-1)
    ,m_nextDstHourStatus(-1)
    ,m_errorReported(0xff)
{}

void Es100Wire::setup(bool enable)
{
    pinMode(enablePin, OUTPUT);
    digitalWrite(enablePin, LOW);
    if (!enable)
        return;

    Wire.begin();
    pinMode(irqPin, INPUT);
    digitalWrite(enablePin, HIGH);
    attachInterrupt(irqPin, &Es100Wire::isr, FALLING);
    delay(20);
    int16_t devId = readRegister(ES100_DEVICE_ID_REG);
#if USE_SERIAL
    Serial.print("ES100 device ID ");
    Serial.println(devId);
#endif
    digitalWrite(enablePin, LOW);
}

uint8_t Es100Wire::fromBCD(int16_t v)
{
    return static_cast<uint8_t>(((v & 0xF0u) >> 4) * 10u + (v & 0xFu));
}

bool Es100Wire::loop(bool isSynced)
{
    if (isSynced)
    {
        if (m_state != ReceptionState::SHUTDOWN)
            shutdown();
        return false;
    }
    if (m_state != ReceptionState::ACTIVE)
    {
        listen();
        return false;
    }
    noInterrupts();
    bool triggered = isrTriggered;
    isrTriggered = false;
    interrupts();
    if (triggered)
    {
        auto irqStatus = readRegister(ES100_IRQ_STATUS_REG);
        DEBUG_OUTPUT1(F("Es100 interrupt: 0x"));
        DEBUG_OUTPUT2(static_cast<unsigned>(irqStatus), HEX);
        DEBUG_OUTPUT1('\n');
        if (irqStatus < 0)
            return false;
        if (irqStatus & IRQSTATUS_RX_COMPLETE)
        {
            // got something!
            TimeElements toRead = {};
            auto yr = readRegister(ES100_YEAR_REG);
            if (yr < 0)
                return false;
            toRead.Year = (2000 - 1970) + fromBCD(yr);
            m_yearLstRec = toRead.Year;

            auto mo = readRegister(ES100_MONTH_REG);
            if (mo < 0)
                return false;
            toRead.Month = fromBCD(mo);
            m_monthLstRec = toRead.Month;

            auto dy = readRegister(ES100_DAY_REG);
            if (dy < 0)
                return false;
            toRead.Day = fromBCD(dy);
            m_dayLstRec = toRead.Day;

            auto hr = readRegister(ES100_HOUR_REG);
            if (hr < 0)
                return false;
            toRead.Hour = fromBCD(hr);
            m_hourLstRec = toRead.Hour;

            auto mn = readRegister(ES100_MINUTE_REG);
            if (mn < 0)
                return false;
            toRead.Minute = fromBCD(mn);

            auto sc = readRegister(ES100_SECOND_REG);
            if (sc < 0)
                return false;
            toRead.Second = fromBCD(sc);
            DEBUG_OUTPUT1(F("WWVB time.\n"));
            debugPrint(toRead);
            m_time = makeTime(toRead);

            m_nextDstMonthStatus = readRegister(ES100_NEXT_DST_MONTH_REG);
            m_nextDstDayStatus = readRegister(ES100_NEXT_DST_DAY_REG);
            m_nextDstHourStatus = readRegister(ES100_NEXT_DST_HOUR_REG);
            m_status0 = readRegister(ES100_STATUS0_REG);
            m_state = ReceptionState::IDLE;
            debugRegisterPrint();
            return true;
        }
    }
    return false;
}

void Es100Wire::printClock()
{
#ifdef DEBUG_TO_SERIAL
    TimeElements te;
    breakTime(now(), te);
    debugPrint(te);
#endif
}

time_t Es100Wire::getUTCandClear()
{
    auto ret = m_time;
    m_time = 0;
    return ret;
}

void Es100Wire::shutdown()
{
#if USE_SERIAL
    Serial.print(F("Es100Wire::shutdown\n"));
#endif
    digitalWrite(enablePin, LOW);
    m_state = ReceptionState::SHUTDOWN;
}

void Es100Wire::listen()
{
    digitalWrite(enablePin, HIGH);
    if (writeRegister(ES100_CONTROL0_REG, CONTROL0_START | CONTROL0_ANT2_OFF))
    {
#if USE_SERIAL
        Serial.print(F("Es100Wire::listen\n"));
#endif
        m_state = ReceptionState::ACTIVE;
    }
}

bool Es100Wire::writeRegister(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg,val};
    Wire.beginTransmission(ES100_SLAVE_ADDR);
    Wire.write(buf, sizeof(buf));
    auto r = Wire.endTransmission();
    if (r != 0)
    {
        if (r != m_errorReported)
        {
            DEBUG_OUTPUT1(F("Es100Wire::writeRegister failed: "));
            DEBUG_OUTPUT1(r);
            DEBUG_OUTPUT1('\n');
        }
        m_errorReported = r;
    }
    else if (m_errorReported != 0)
    {
        DEBUG_OUTPUT1(F("Es100Wire::writeRegister success.\n"));
        m_errorReported = 0;
    }
    return r == 0;
}

int16_t Es100Wire::readRegister(uint8_t reg)
{   
    Wire.beginTransmission(ES100_SLAVE_ADDR);
    Wire.write(reg);
    if (0 != Wire.endTransmission())
        return -1;
    if (1 != Wire.requestFrom(ES100_SLAVE_ADDR, static_cast<uint8_t>(1), true))
    {
        DEBUG_OUTPUT1(F("Es100Wire::readRegister failed\n"));
        return -1;
    }
    return static_cast<uint8_t>(Wire.read());
}

int8_t Es100Wire::isDstNow()
{
    if (m_status0 < 0)
        return -1; // use valid m_nextDstDayStatus as proxy for whether m_status0 is OK;
    uint8_t stat = (m_status0 & (STATUS_0_DST0 | STATUS_0_DST1 | STATUS_0_RXOK)) ;
    if (stat == STATUS_0_RXOK)
        return 0;
    if (stat == (STATUS_0_DST0 | STATUS_0_DST1 | STATUS_0_RXOK))
        return 1;
    if ((m_nextDstHourStatus >= 0) && (m_nextDstHourStatus & DST_HOUR_SPECIAL3)!=0)
    {
        auto masked = m_nextDstHourStatus & DST_HOUR_SPECIAL;
        if (masked == (DST_HOUR_SPECIAL0|DST_HOUR_SPECIAL3))
            return 0;
         if (masked == (DST_HOUR_SPECIAL1|DST_HOUR_SPECIAL3))
            return 1;
    }
       
    return -1; // don't really know 
}

bool Es100Wire::ScheduledDst(bool &begins, time_t &when, uint8_t &localHour, bool print) // returns UTC midnight of date of change
{
    TimeElements t = {};
    if ((m_status0 >= 0) && (m_nextDstHourStatus >= 0) && 0 == ((m_nextDstHourStatus & DST_HOUR_SPECIAL3)))
    {
        t.Year = m_yearLstRec;
        localHour =  m_nextDstHourStatus & 0xF;
        // there are two possible ways to report a scheduled DST change
         if (((m_status0 & STATUS_0_RXOK) != 0) && 
                // STATUS_0_DST0 does not match STATUS_0_DST1
                (0 != (STATUS_0_DST0 & (m_status0 ^ (m_status0 >> 1)))))
        {   // schedule DST on the day it changes
            begins = (m_status0 & STATUS_0_DST1) != 0;
            t.Month = m_monthLstRec;
            t.Day = m_dayLstRec;
            if (m_hourLstRec >= 11)  // late in the day today?
                localHour = 2;  // then change happened in the past. 
            if (print)
            {
#if USE_SERIAL
                Serial.print(F("Es100 DST "));
                Serial.print(begins ? "beginning" : "ending");
                Serial.print(" year: ");
                Serial.print(static_cast<int>(1970+t.Year));
                Serial.print('-');
                Serial.print(static_cast<int>(t.Month));
                Serial.print('-');
                Serial.print(static_cast<int>(t.Day));
                Serial.print(F(", today at "));
                if (localHour < 10)
                    Serial.print('0');
                Serial.print(static_cast<int>(localHour));
                Serial.println("00");
 #endif
            }
        }
        else if ((m_nextDstHourStatus >= 0) && (m_nextDstDayStatus >= 0) && (m_nextDstMonthStatus >= 0))
        {   // WWVB has broadcast the scheduled change way in advance
            begins = (m_status0 & STATUS_0_DST0) == 0;
            t.Month = fromBCD(m_nextDstMonthStatus);
            t.Day = fromBCD(m_nextDstDayStatus);
            if (t.Month < m_monthLstRec)
                t.Year += 1;
            if (print)
            {
 #if USE_SERIAL
                Serial.print(F("Es100 DST scheduled "));
                Serial.print(begins ? "beginning" : "ending");
                Serial.print(F(" at Y-M-D hr: "));
                Serial.print(static_cast<int>(1970+t.Year));
                Serial.print('-');
                Serial.print(static_cast<int>(t.Month));
                Serial.print('-');
                Serial.print(static_cast<int>(t.Day));
                Serial.print(' ');
                if (localHour < 10)
                    Serial.print('0');
                Serial.print(static_cast<int>(localHour));
                Serial.println("00");
 #endif
            }
       }
        else 
            return false;
    }
    else 
        return false;

    when = makeTime(t);
    return true;
}

void Es100Wire::debugPrint(const TimeElements &t)
{
#ifdef DEBUG_TO_SERIAL
    DEBUG_OUTPUT1(F(" Year:"));
    DEBUG_OUTPUT1(static_cast<unsigned>(1970 + t.Year));
    DEBUG_OUTPUT1(F("\n Month:"));
    DEBUG_OUTPUT1(static_cast<unsigned>(t.Month));
    DEBUG_OUTPUT1(F("\n Day:"));
    DEBUG_OUTPUT1(static_cast<unsigned>(t.Day));
    DEBUG_OUTPUT1(F("\n Hour:"));
    DEBUG_OUTPUT1(static_cast<unsigned>(t.Hour));
    DEBUG_OUTPUT1(F("\n Minute:"));
    DEBUG_OUTPUT1(static_cast<unsigned>(t.Minute));
    DEBUG_OUTPUT1(F("\n Second:"));
    DEBUG_OUTPUT1(static_cast<unsigned>(t.Second));
    DEBUG_OUTPUT1('\n');
#endif
}

void Es100Wire::debugRegisterPrint()
{
    return;
#ifdef DEBUG_TO_SERIAL
    DEBUG_OUTPUT1(F("ES100:****\n"));
    for (uint8_t i = 0; i <= 0xd; i++)
    {
        auto r = readRegister(i);
        DEBUG_OUTPUT1(F("Es100 register 0x"));
        DEBUG_OUTPUT2(i, HEX);
        DEBUG_OUTPUT1(F(" =0x"));
        DEBUG_OUTPUT2(r, HEX);
        DEBUG_OUTPUT1('\n');
    }
#endif
}

volatile bool Es100Wire::isrTriggered;
void Es100Wire::isr()
{
    isrTriggered = true;
}

