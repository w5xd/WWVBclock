
#include <Arduino.h>
#include <TimeLib.h>
#include "ClockDisplay.h"
#include "WwvbClockDefinitions.h"

namespace {
    const float ABSENT_TEMP = -999.9;
}

ClockDisplay::ClockDisplay(LiquidCrystal &lcd, Hcms290xType_t &led) 
    : lcd(lcd)
    , led(led)
    , lastDisplayedHour(-1)
    , lastDisplayedMinute(-1)
    , lastTimet(0)
    , radioSilence(false)
    , utcSecondsOffset(0)
    , DST(false)
    , m_DstScheduledBegin(false)
    , m_DstChangesWhen(0)
    , m_unitsInMetric(false)
    , m_12Hour(false)
    , m_displayStyle(TimeDisplaySyle::OEM_FONT)
    , m_Blink(BLINK_1)
    , m_flippedFonts(false)
    , m_outdoortempC(ABSENT_TEMP)
    , m_outdoortempTime(0)
    , m_rainToday(0)
    , m_rainYesterday(0)
    , m_clearedRainToday(false)
    , m_rainGaugeCorrectionPerThousand(1000)
{
}

static unsigned char OneHalfPixels[8] =
{
    0x10,
    0x12,
    0x14,
    0x0B,
    0x09,
    0x17,
    0x04,
    0x07,
};

void ClockDisplay::setup()
{
    lcd.begin(8,2);
    lcd.clear();
    lcd.noCursor();
    lcd.noBlink();
    lcd.noAutoscroll();
    lcd.createChar(0, OneHalfPixels);

    led.displayString("");
    led.display();
}

void ClockDisplay::updateDisplay()
{
    lastDisplayedMinute = -1; // force immediate re-display
    lastTimet = 0;
}

void ClockDisplay::scheduleDSTchangeAt(bool begins, time_t utcMidnight, uint8_t localHour)
{
    m_DstScheduledBegin = begins;
    auto t = utcMidnight + utcSecondsOffset + 3600 * localHour;
    m_DstChangesWhen = t;
}

void ClockDisplay::setUtcMinutesOffset(int minutes)
{
    utcSecondsOffset = minutes * 60;
    updateDisplay();
}

void ClockDisplay::useFlippedFonts(bool v)
{
    m_flippedFonts = v;
    updateDisplay();
}

void ClockDisplay::setDisplayStyle(TimeDisplaySyle s)
{
    if (static_cast<unsigned>(s) >= static_cast<unsigned>(TimeDisplaySyle::DISPLAY_STYLE_MAX))
        return;
    m_displayStyle = s;
    updateDisplay();
}

void ClockDisplay::setDST(bool v) { 
    DST = v; 
    updateDisplay();
};

void ClockDisplay::set12Hour(bool v) { 
    m_12Hour = v;
    updateDisplay();
};

void ClockDisplay::setRadioSilence(bool v)
{
    if (v != radioSilence)
    {
        DEBUG_OUTPUT1(F("ClockDisplay::setRadioSilence: "));
        DEBUG_OUTPUT1(static_cast<int>(v));
        DEBUG_OUTPUT1('\n');
        if (v)
        {
            lcd.noDisplay();
            led.noDisplay();
        }
        else
        {
            lcd.display();
            led.display();
        }
    }
    radioSilence = v;
}

static void printdig(Print &s, uint8_t d)
{
    if (d < 10)
        s.print('0');
    s.print(static_cast<int>(d));
}

void ClockDisplay::printClock()
{
#if USE_SERIAL
    auto t = now();
    auto min = minute(t);
    auto hr = hour(t);
    auto yr = year(t);
    auto mo = month(t);
    auto dy = day(t);
    Serial.print("Now is ");
    Serial.print(static_cast<int>(yr));
    Serial.print('/');
    Serial.print(static_cast<int>(mo));
    Serial.print('/');
    Serial.print(static_cast<int>(dy));
    Serial.print(' ');
    if (hr < 10)
        Serial.print('0');
    Serial.print(static_cast<int>(hr));
    Serial.print(':');
    if (min < 10)
        Serial.print('0');
    Serial.println(static_cast<int>(min));   
#endif 
}

void ClockDisplay::displayRain(float rmm)
{
    char buf[8];
    memset(buf,0,sizeof(buf));
    float corrected = rmm * m_rainGaugeCorrectionPerThousand / 1000.f;
    uint16_t mm = static_cast<unsigned>(corrected);
    lcd.setCursor(4,1);
    if (m_unitsInMetric)
    {
        if (mm < 999)
        {
            dtostrf(mm, 3, 0, buf);
            auto len = strlen(buf);
            buf[len++] = 'm';
            buf[len] = 0;
            while (len++ < 4)
                    lcd.write(' ');
            lcd.print(buf);
        }
        else
            lcd.print('?');
    }
    else
    {
        float inch = mm / 25.4f;
        if (inch < 10.f)
        {
            char *p = buf;
            dtostrf(inch, 4, inch < 1.0f ? 2 : 1, buf);
            if (*p == '0');
                p += 1;
            auto len = strlen(p);
            if (len < 4)
            {
                p[len++] = '"';
                p[len] = 0;
            }
            while (len++ < 4)
                lcd.write(' ');
            lcd.print(p);
        }
        else
            lcd.print('?');                
    }
}

void ClockDisplay::loop(bool ledEnabled, bool lcdEnabled)
{
    if (radioSilence)
        return;

    if (timeStatus() != timeSet)
    {
        lcd.clear();
        lcd.print("Not Set");
        static const char SET[Hcms290xType_t::DISPLAY_WIDTH] = {'S', 'e', 't', '!' };
        led.setCurrentFontIdx(HCMS_OEM_FONT_IDX);
        led.displayString(SET);       
        return;
    }

    auto t = now();
    if (t == lastTimet)
        return;
    lastTimet = t;
    m_Blink = static_cast<Blink_t>(static_cast<unsigned>(m_Blink)+1);
    if (static_cast<unsigned>(m_Blink) > static_cast<unsigned>(BLINK_4))
        m_Blink = BLINK_1;
    if (m_DstChangesWhen != 0)
    {
        if (t >= m_DstChangesWhen)
        {
            DST = m_DstScheduledBegin;
            m_DstChangesWhen = 0;
        }
    }
    t += utcSecondsOffset;
    if (DST)
        t += 3600;
    auto sec = second(t);
    auto min = minute(t);
    auto hr = hour(t);
    if ((hr == 0) && (min == 0) && !m_clearedRainToday)
    {
        m_clearedRainToday = true;
        m_rainYesterday = m_rainToday;
        m_rainToday = 0;
        DEBUG_OUTPUT1("clock clearing rain today\n");
    }
    if (hr == 12)
        m_rainYesterday = 0;
    if (m_clearedRainToday && hr != 0)
    {
        m_clearedRainToday = false;
        DEBUG_OUTPUT1("clock ready to clear tomorrow\n");
    }
    if (m_12Hour)
        hr = hourFormat12(t);
    bool hrOrMinChanged = (min != lastDisplayedMinute) ||   (hr != lastDisplayedHour);
    if (lcdEnabled)
    {
        lcd.clear();
        if (m_12Hour)
        {
            if (hr < 10)
                lcd.print(' ');
            lcd.print(static_cast<int>(hr));
        }
        else
            printdig(lcd, hr);
        lcd.print(':');
        printdig(lcd, min);
        lcd.print(':');
        printdig(lcd, sec);
        if (m_outdoortempC != ABSENT_TEMP)
        {
            static const unsigned long t10_MINUTES_MSEC = 1000ul * 60u * 10u;
            if (millis() - m_outdoortempTime < t10_MINUTES_MSEC)
            {
                char buf[8];
                memset(buf,0,sizeof(buf));
                lcd.setCursor(0,1);
                auto t = m_outdoortempC;
                if ((t < 99 && t > -40))
                {
                    if (!m_unitsInMetric)
                    {
                        t *= 9;
                        t /= 5;
                        t += 32;
                        dtostrf(t, 3, 0, buf);
                        buf[3] = 0xdf;
                        lcd.print(buf);
                    }
                    else
                    {
                        dtostrf(t, 3, 0, buf);
                        lcd.print(buf);
                        float intpart;

                        if (fabs(modf(t, &intpart)) >= 0.5)
                            lcd.write(byte(0));
                        lcd.write(byte(0xdf));
                    }
                }
                else
                    lcd.write('?');
             }
            else m_outdoortempC = ABSENT_TEMP;
        }
        if (m_rainYesterday > 0 && ((m_Blink == BLINK_1) || (m_Blink==BLINK_2)))
        {
            lcd.setCursor(0,1);
            lcd.print("Yst:");
            displayRain(m_rainYesterday);
        }
        else if (m_rainToday > 0)
            displayRain(m_rainToday);
    }
    if (hrOrMinChanged || m_displayStyle == TimeDisplaySyle::SMALL_COLON)
    {   // led doesn't show seconds
        if (ledEnabled)
        {
            char buffer[Hcms290xType_t::DISPLAY_WIDTH];
            memset(buffer, 0, sizeof(buffer));
            int h1 = hr / 10;
            int h2 = hr % 10;
            int m1 = min / 10;
            int m2 = min % 10;
            buffer[0] = '0' + h1;
            if (m_12Hour && hr < 10)
                buffer[0] = ' ';
            buffer[1] = '0' + h2;
            buffer[2] = '0' + m1;
            buffer[3] = '0' + m2;
            ledDisplayAddColon(buffer);
        }
    }
    lastDisplayedMinute = min;
    lastDisplayedHour = hr;
}

void ClockDisplay::ledDisplayAddColon(char *p) const
{
    uint8_t fontIdx=0;
    switch (m_displayStyle)
    {
        case TimeDisplaySyle::OEM_FONT:
            fontIdx = HCMS_OEM_FONT_IDX;
            break;
        case TimeDisplaySyle::SEG7_FONT:
            fontIdx = HCMS_7SEG_FONT_IDX;
            break;
        case    TimeDisplaySyle::USE_DECIMAL:
            fontIdx = HCMS_SMALLDIG_FONT_IDX;
            if (p[0] != ' ')
                p[0] += 20; // align character top
            p[1] += 30; // aling top, with right dec
            p[2] += 40; // align top with left dec
            p[3] += 20; // align top 
            break;
        case    TimeDisplaySyle::SMALL_COLON:
            fontIdx = HCMS_SMALLDIG_FONT_IDX;
            if ((m_Blink==BLINK_1) || (m_Blink==BLINK_3))
                    p[1] += 10; // second character gets a colon
            break;
        default:
            break;
    }
    if (m_flippedFonts)
        fontIdx += FLIPPED_FONT_INCREMENT;
    led.setCurrentFontIdx(fontIdx);
    led.displayString(p);
}
 
void ClockDisplay::notifyIndoorTemp(float)
{}

void ClockDisplay::notifyOutdoorTemp(float tempX10Cent)
{
    m_outdoortempC = tempX10Cent;
    m_outdoortempTime = millis();
}

void ClockDisplay::notifyRainmm(float v)
{    m_rainToday += v;}

void ClockDisplay::unitsInMetric(bool v)
{    m_unitsInMetric = v;}

bool ClockDisplay::setRainGaugeCorrection(uint16_t t)
{
    bool ret = false;
    if (t >= 500 && t <= 2000)
    {   // range 0.5 to 2.0 allowed for correction from 1 mm per mm
        m_rainGaugeCorrectionPerThousand = t;
        ret = true;
    }
    DEBUG_OUTPUT1("Raingauge correct=");
    DEBUG_OUTPUT1(m_rainGaugeCorrectionPerThousand);
    DEBUG_OUTPUT1('\n');
    return ret;
}