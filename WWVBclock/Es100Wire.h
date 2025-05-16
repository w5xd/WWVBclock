#pragma once
#include <TimeLib.h>
class Es100Wire
{
/* The ES100 is a WWVB receiver (60KHz) on a single die. Use i2c to talk to it.
*/
 public:
    Es100Wire(int irqPin, int enablePin, TwoWire &);
    void setup(bool enable);
    bool loop(bool isSynced); // returns true when it has a receipt. 
    time_t getUTCandClear();
    int8_t isDstNow(); // WWVB reports every minute whether DST is now in effect
    bool ScheduledDst(bool &onOff, time_t &when, uint8_t &localHour); // returns UTC midnight of date of next change
    static void printClock();
    
 protected:
   void shutdown();
   void listen();
   bool writeRegister(uint8_t reg, uint8_t val);
   static void debugPrint(const TimeElements &);
   void debugRegisterPrint();
   static uint8_t fromBCD(int16_t);
   int16_t readRegister(uint8_t reg);
   static void isr();
   enum class ReceptionState { SHUTDOWN, ACTIVE, IDLE};
   const int irqPin;
   const int enablePin;
   TwoWire &Wire;
   ReceptionState m_state;
   time_t m_time;
   int16_t m_status0;
   uint8_t m_yearOfDst;
   int16_t m_nextDstMonthStatus;
   int16_t m_nextDstDayStatus;
   int16_t m_nextDstHourStatus;
   uint8_t m_errorReported;
   static volatile bool isrTriggered;
};
