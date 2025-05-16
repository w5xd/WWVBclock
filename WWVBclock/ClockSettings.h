#pragma once
#include <LiquidCrystal.h>
#include <TimeLib.h>

// class to manipulate clock settings based on user pressing sw1 and sw2

class ClockSettings {
    public:
        ClockSettings(LiquidCrystal &lcd);
        void setup();
        bool loop(bool sw1, bool sw2);
        void es100UpdatedAt(time_t);
        static const char *optionName(uint8_t param, uint8_t opt);
        static time_t g_es100UpdatedAt;
        static void applySw1(uint8_t param, uint8_t opt);

    protected:
        void displayCurrentSw1();
        void processSw1Buttons(unsigned long mil, bool sw1, bool sw2);
        void processSw2Buttons(unsigned long mil, bool sw1, bool sw2);
        enum State_t {IDLE, HOLD_SW1_FOR_START, SW1_IN_PROGRESS, HOLD_SW2_FOR_START, SW2_IN_PROGRESS, WAIT_FOR_SW_RELEASE} m_state;
        uint8_t m_curParam;
        uint8_t m_curOption;
        unsigned long m_lastButtonMsec;
        bool m_prevSw1;
        bool m_prevSw2;
        bool m_haveSetTime;
           LiquidCrystal &lcd;
};
