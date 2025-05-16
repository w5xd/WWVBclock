#include <Arduino.h>
#include <TimeLib.h>
#include "ClockSettings.h"
#include "WWVBclock.h"

/* User interface:
** Press and hold SW1 (without SW2) for at least PRESS_HOLD_MSEC to
** enter setup mode.
**
** After no buttons for SETUP_TIMEOUT_MSEC, exit setup mode.
**
** While in setup mode, use a click on SW1 to cycle through the setup parameters.
** display the item on row 1 of the LCD.
** While in setup mode, use a click on SW2 to cycle through the options for the parameter.
**
*/

#define DIM(X) sizeof(X)/sizeof(X[0])

namespace {
    const int PRESS_HOLD_MSEC = 2000;
    const int PRESS_HOLD_MESSAGE_MSEC = 500;
    const int LONG_PRESS_MSEC = 999;
    const int SETUP_TIMEOUT_MSEC = 15000;
    const int DEBOUNCE_MSEC = 110;

    typedef const char * (*OptionNameFcn_t)(uint8_t, uint8_t);
    typedef void (*ApplyFcn_t)(uint8_t, uint8_t);

    struct SetupParameter {
        SetupParameter(const char *p, int CommandOption, int n,
            const char * const * OptionNames,
            ApplyFcn_t applyFcn = &ClockSettings::applySw1,
            OptionNameFcn_t optionNameFcn = &ClockSettings::optionName) 
            : ParamName(p)
            , CommandOption(CommandOption)
            , NumOptions(n)
            , optionNameFcn(optionNameFcn)
            , applyFcn(applyFcn) 
            , OptionNames(OptionNames)
            {}
        const char *ParamName; // fit in 8 character LCD row
        const int CommandOption;
        const int NumOptions;
        OptionNameFcn_t optionNameFcn;
        ApplyFcn_t applyFcn;
        const char * const * const OptionNames; // each fits in 8 character LCD row
    };
    
    namespace tz {
        void tzApply(uint8_t, uint8_t tz)
        {
            char buf[80];
            char *p = buf;
            const char *q = CLOCKCOMMANDS[static_cast<int>(ClockCommands_t::TimeZoneOffset)];
            while (*p++ = *q++);
            itoa(-60 * (4 + tz),--p, 10);
            routeCommand(buf, 1+strlen(buf));
        }
        const char * const options[] = {"Atlantic", "Eastern", "Central", "Mountain", "Pacific", "Alaska", "Hawaii"};
        SetupParameter p("TimeZone", -1, DIM(options), options, &tzApply);
    }

    namespace dst {
        const char * const options[] = {"Never", "Summer"};
        SetupParameter p("DST", static_cast<int>(ClockCommands_t::ObserveDST), DIM(options), options);
    }

    namespace dstNow {
        const char * const options[] = {"NO", "YES"};
        SetupParameter p("DST Now?", static_cast<int>(ClockCommands_t::DstIsInEffect), DIM(options), options);
    }

    namespace bright {
        const int NUM_BRITES = 6;
        void apply(uint8_t, uint8_t brite)
        {
            if (brite >= NUM_BRITES)
            {
                static const char pwmOff[] = "LedPwm=0";
                routeCommand(pwmOff, strlen(pwmOff)+1);
                return;
            }
            static const char pwmOn[] = "LedPwm=";
            char buf[80];
            char *r = buf;
            const char *q = pwmOn;
            while (*r++ = *q++);
            r -= 1;
            char current = '0';
            switch (brite)
            {   // PWM
                case 0:
                    strcpy(r, "1");
                    break;
                case 1:
                    strcpy(r, "5");
                    current = '0';
                    break;
                case 2:
                    strcpy(r, "8");
                    current = '1';
                    break;
                case 3:
                    strcpy(r, "12");
                    current = '2';
                    break;
                case 4:
                    strcpy(r, "15");
                    current = '3';
                    break;
            }
            routeCommand(buf, strlen(buf)+1);
            r = buf;
            q = CLOCKCOMMANDS[static_cast<int>(ClockCommands_t::LedCurrent)];
            while (*r++ = *q++);
            *--r = current;
            *++r = 0;
            routeCommand(buf, strlen(buf) + 1);
        }
        const char * const options[NUM_BRITES] = {"Min","Low", "Medim", "High", "Max", "Off", };
        SetupParameter p("Bright", -1, NUM_BRITES, options, &apply);
    }

    namespace r180 {
        void apply(uint8_t, uint8_t rf)
        {
            char buf[80];
            char *p = buf;
            const char *q = CLOCKCOMMANDS[static_cast<int>(ClockCommands_t::RotateLed180)];
            while (*p++ = *q++);
            *--p =  (rf & 1) == 0 ? '0' : '1';
            *++p = 0;
            routeCommand(buf, 1+strlen(buf));
            p = buf;
            q = CLOCKCOMMANDS[static_cast<int>(ClockCommands_t::UseFlippedFonts)];
            while (*p++ = *q++);
            *--p =  (rf & 2) == 0 ? '0' : '1';
            *++p = 0;
            routeCommand(buf, 1+strlen(buf));
        }
        const char * const options[] = {"No", "Yes", "No&Mirror", "Yes&Mirro"};
        SetupParameter p("Rotate", -1, DIM(options), options, &apply);
    }

    namespace h12 {
        const char * const options[] = {"24HR", "12 AM/PM", };
        SetupParameter p ("Time Hrs", static_cast<int>(ClockCommands_t::C12HourDisplay), DIM(options), options);
    }

    namespace fonts {
        const char * const options[] = {"OEM", "7SEG", "HR..SEC", "HR:SEC",};
        SetupParameter p ("Font", static_cast<int>(ClockCommands_t::TimeDisplayFont), DIM(options), options);
    }

    namespace metric {
        const char * const options[] = { "in & F", "mm & C",};
        SetupParameter p ("Units", static_cast<int>(ClockCommands_t::MetricUnits), DIM(options), options);
    }

    namespace radioSilence {
        const char * const options[] = {"Disabled", "Enabled"};
        SetupParameter p ("R. Silec", static_cast<int>(ClockCommands_t::TryRadioSilence), DIM(options), options);
    }

    namespace es100Updated {
         const char * option(uint8_t, uint8_t w)
        {
            static char buf[9];
            memset(buf,0,sizeof(buf));
            auto t = ClockSettings::g_es100UpdatedAt;
            if (t == 0)
                return buf;
            if (w == 0)
            { // DATE
                auto mo = month(t);
                auto dy = day(t);
                char *p = buf;
                if (mo < 10)
                    *p++ = '0';
                itoa(mo, p, 10);
                while (*p) p += 1;
                *p++ = '/';
                if (dy < 10)
                    *p++ = '0';
                itoa(dy, p, 10);
                return buf;
            }
            else
            {   // time
                auto min = minute(t);
                auto hr = hour(t);
                char *p = buf;
                if (hr < 10)
                    *p++ = '0';
                itoa(hr, p, 10);
                while (*p) p+=1;
                *p++ = ':';
                if (min < 10)
                    *p++ = '0';
                itoa( min, p, 10);
                return buf;
            }
            return "";
        }
        void apply(uint8_t, uint8_t)
        {}
        SetupParameter p ("WWVB up", -1, 2, 0, &apply, &option);
    }

    namespace last {
        const char * const options[] = {"NOW"};
        SetupParameter p ("EXIT", -1, DIM(options), options);
    }

    const SetupParameter *setupParameters[] =
    {
        &tz::p,
        &dst::p,
        &dstNow::p,
        &bright::p,
        &r180::p,
        &h12::p,
        &fonts::p,
        &metric::p,
        &radioSilence::p,
        &es100Updated::p,
        &last::p,
    };

    const int NUM_SETUP_PARAMETERS = DIM(setupParameters);
    const uint8_t NO_OPTION = 0xff;
}

time_t ClockSettings::g_es100UpdatedAt(0);

ClockSettings::ClockSettings(LiquidCrystal &lcd) 
    :m_state(IDLE)
    ,m_curParam(0)
    ,m_curOption(0)
    ,m_lastButtonMsec(0)
    ,m_prevSw1(false)
    ,m_prevSw2(false)
    ,m_haveSetTime(false)
    ,lcd(lcd)
{
}

void ClockSettings::setup()
{
    m_lastButtonMsec = millis();
#if USE_SERIAL
    Serial.println("ClockSettings::setup");
#endif
}

void ClockSettings::es100UpdatedAt(time_t t)
{
    g_es100UpdatedAt = t;
}

bool ClockSettings::loop(bool sw1, bool sw2)
{
    auto now = millis();
    static_assert(sizeof(now) == sizeof(m_lastButtonMsec), "Time type wrong");
    int delay = static_cast<int>(now - m_lastButtonMsec);
    bool ret = false;
    switch (m_state)
    {
        case IDLE:
            m_haveSetTime = false;
            if (sw1)
            {
                m_lastButtonMsec = now;
                m_state = HOLD_SW1_FOR_START;
                lcd.clear();
                lcd.print("Hold for"); 
                lcd.setCursor(0,1);
                lcd.print("  Menu");
                ret = true;
            }
            else if (sw2)
            {
                m_lastButtonMsec = now;
                m_state = HOLD_SW2_FOR_START;
                lcd.clear();
                lcd.print("Hold to"); 
                lcd.setCursor(0,1);
                lcd.print("set time");
                ret = true;
            }
            break;
        case WAIT_FOR_SW_RELEASE:
            if (!sw2 && !m_prevSw2 && !m_prevSw1 && !sw1 && delay >= DEBOUNCE_MSEC)
                m_state = IDLE;
            break;
        case HOLD_SW1_FOR_START:
                if (!sw1 && delay >= PRESS_HOLD_MESSAGE_MSEC)
                {
                    m_state = IDLE;
                    ret = false;
                }
                else
                {
                    ret = true;
                    if (delay >= PRESS_HOLD_MSEC)
                    {
                        m_state = SW1_IN_PROGRESS;
                        m_curOption = NO_OPTION;
                        m_curParam = 0;
                        lcd.display();
                        displayCurrentSw1();
                    }
                }
            break;
        case HOLD_SW2_FOR_START:
                if (!sw2 && delay >= PRESS_HOLD_MESSAGE_MSEC)
                {
                    m_state = IDLE;
                    ret = false;
                }
                else
                {
                    ret = true;
                    if (delay >= PRESS_HOLD_MSEC)
                    {
                        m_state = SW2_IN_PROGRESS;
                        m_haveSetTime = false;
                        m_curOption = NO_OPTION;
                        m_curParam = 0;
                        lcd.display();
                        m_lastButtonMsec = now+PRESS_HOLD_MSEC; // future to force longer hold for fast HR update
                        processSw2Buttons(now,false,false);
                        ret = false;
                    }
                }
           break;
        case SW1_IN_PROGRESS:
        case SW2_IN_PROGRESS:
            ret = true;
            if (m_state == SW1_IN_PROGRESS)
                processSw1Buttons(now, sw1, sw2);
            else
            {
                processSw2Buttons(now, sw1, sw2);
                ret = false;
            }
            if (sw1 || sw2)
            {
                if (sw1 ^ m_prevSw1 || sw2 ^ m_prevSw2)
                    m_lastButtonMsec = now; // timestamp when button is first pressed
            }
            else if (delay >= SETUP_TIMEOUT_MSEC)
            {
                if (m_haveSetTime)
                {
#if USE_SERIAL
                    Serial.println("Setting Teensy3 RTC to new time");
#endif
                    Teensy3Clock.set(::now());
                }
                m_state = IDLE;
                lcd.clear();
                ret = false;
            }
            break;
    }
    m_prevSw1 = sw1;
    m_prevSw2 = sw2;
    return ret;
}

void ClockSettings::processSw1Buttons(unsigned long now, bool sw1, bool sw2)
{
    int delay = static_cast<int>(now - m_lastButtonMsec);
    if (sw1 && !m_prevSw1 && delay > DEBOUNCE_MSEC)
    {
        if (m_curOption != NO_OPTION)
            (setupParameters[m_curParam]->applyFcn)(m_curParam, m_curOption);
        m_curParam += 1;
        if (m_curParam >= NUM_SETUP_PARAMETERS)
            m_curParam = 0;
        m_curOption = NO_OPTION;
        displayCurrentSw1();
    }
    else if (sw2 && !m_prevSw2 && delay > DEBOUNCE_MSEC)
    {
        if (m_curParam == NUM_SETUP_PARAMETERS - 1)
        {
            m_state = WAIT_FOR_SW_RELEASE;
            return;
        }
        m_curOption += 1;
        if (m_curOption >= setupParameters[m_curParam]->NumOptions)
            m_curOption = NO_OPTION;
        displayCurrentSw1();
    }
}

// for those parameters/options specified in the table
const char *ClockSettings::optionName(uint8_t param, uint8_t opt)
{
    auto options = setupParameters[param]->OptionNames;
    return options[opt];
}

void ClockSettings::displayCurrentSw1()
{
    lcd.clear();
    lcd.setCursor(0,0);
    auto pnam = setupParameters[m_curParam]->ParamName;
    auto plen = strlen(pnam);
    for (int8_t i = (8 - plen)/2; i > 0; i -= 1)
        lcd.print(' ');
    lcd.print(pnam);
    lcd.setCursor(0,1);

    if (m_curOption != NO_OPTION)
    {
        auto oname = (setupParameters[m_curParam]->optionNameFcn)(m_curParam, m_curOption);
        auto olen = strlen(oname);
        for (int8_t i = (8 - olen)/2; i > 0; i -= 1)
            lcd.print(' ');
        lcd.print(oname);
    }
 }

 void ClockSettings::applySw1(uint8_t p, uint8_t o)
 {
    auto optionCommand = setupParameters[p]->CommandOption;
    DEBUG_OUTPUT1("Clock apply. p:");
    DEBUG_OUTPUT1(static_cast<int>(p));
    DEBUG_OUTPUT1(" o: ");
    DEBUG_OUTPUT1(static_cast<int>(o));
    DEBUG_OUTPUT1(" command: ");
    DEBUG_OUTPUT1(optionCommand);
    DEBUG_OUTPUT1('\n');
    if (optionCommand > 0)
    {  
        char buf[80];
        const char *q = CLOCKCOMMANDS[optionCommand];
        char *r = buf;
        while (*r++ = *q++);
        itoa(o, --r, 10);
        routeCommand(buf, 1+strlen(buf));
    }
 }

 void ClockSettings::processSw2Buttons(unsigned long tm, bool sw1, bool sw2)
 {
    lcd.setCursor(0,1);
    lcd.print("<<    >>");
    if (sw1 && sw2)
    {
        m_state = WAIT_FOR_SW_RELEASE;
        return;
    }
    auto delay = static_cast<int>(tm - m_lastButtonMsec);
    if (sw1 || sw2)
        m_haveSetTime = true;
    if (sw1 && !m_prevSw1 && delay > DEBOUNCE_MSEC)
    {
        auto t = now();
        t += 60;
        setTime(t);
    }
    else if (sw2 && !m_prevSw2 && delay > DEBOUNCE_MSEC)
    {
        auto t = now();
        t -= 60;
        setTime(t);
    }
    else if (sw1 && delay >= LONG_PRESS_MSEC)
    {
        setTime(now() + 3600);
        m_lastButtonMsec = tm;
    }
    else if (sw2 && delay >= LONG_PRESS_MSEC)
    {
        setTime(now() - 3600);
        m_lastButtonMsec = tm;
    }
 }