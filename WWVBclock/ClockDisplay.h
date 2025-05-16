#pragma once
#include <LiquidCrystal.h>
#include "WWVBclock.h"

/* This class monitors the time-of-day on its loop() call and
** updates both the LED and LCD displays accordingly.
** It also has methods to receive weather notifications, which also
** update the display accordingly.
**
*/
class ClockDisplay : public ClockNotification
{
    public:
        ClockDisplay(LiquidCrystal &lcd, Hcms290xType_t &led);
        void setup();
        void loop(bool ledEnabled, bool lcdEnabled);
        void setRadioSilence(bool); // The WWVB receiver might need us to shut down oscillators.
        void printClock();

        enum class TimeDisplaySyle {OEM_FONT, SEG7_FONT, USE_DECIMAL, SMALL_COLON, DISPLAY_STYLE_MAX};

        //various clock options
        void setDisplayStyle(TimeDisplaySyle);
        void setUtcMinutesOffset(int minutes);
        void setDST(bool);
        void set12Hour(bool);
        void unitsInMetric(bool);
        bool setRainGaugeCorrection(uint16_t perThousand);
        void scheduleDSTchangeAt(bool begins, time_t utcMidnight, uint8_t localHour);
        void useFlippedFonts(bool);
        void updateDisplay();

        // weather notifications
        void notifyIndoorTemp(float) override;
        void notifyOutdoorTemp(float) override;
        void notifyRainmm(float) override;
       
    protected:
        void ledDisplayAddColon(char *) const;
        void displayRain(float mm);
        LiquidCrystal &lcd;
        Hcms290xType_t &led;
        int lastDisplayedHour;
        int lastDisplayedMinute;
        time_t lastTimet;
        bool radioSilence;
        int utcSecondsOffset;
        bool DST;
        bool m_DstScheduledBegin;
        time_t m_DstChangesWhen;
        bool m_unitsInMetric;
        bool m_12Hour;
        TimeDisplaySyle m_displayStyle;
        enum Blink_t {BLINK_1, BLINK_2, BLINK_3, BLINK_4} m_Blink;
        bool m_flippedFonts;
        float m_outdoortempC;
        unsigned long m_outdoortempTime;
        float m_rainToday;
        float m_rainYesterday;
        bool m_clearedRainToday;
        uint16_t m_rainGaugeCorrectionPerThousand;
};
