/* Arduino sketch for WWVB clock
** (c) 2025 by Wayne E. Wright
**
**  This is the sketch is part of a clock that does the following:
**  projects the time of day on a darkened ceiling
**  separately displays on an 8x2 character LCD
**  keeps battery backed up time using an RTC with lithium coin cell  
**  sets the battery backed up clock from a single chip 60KHz WWVB receiver
**  receives and displays weather sensor updates from a packet radio 
**
** The specific components this sketch supports as installed on its printed circuit board:
**      arduino Teensy 4.0 including its RTC clock with CR2032 cell
**      Broadcom HCMS-2905 1x4 alpha-numeric LED pixel array. 5x7 pixels per cell
**      Option 2nd row with a second HCMS-2905
**      Canduino ES100-MOD BPSK WWVB receiver
**      RFM69 packet radio transceiver to monitor packet thermometer(s)
**      parallel access LCD character display. NHD-0208AZ-RN-YBW-33V
**
** Clock
** Using the Arduino Time Library, this sketch gets its time 
** from the Time library, which in turn maintains its time from the
** main CPU clock running the Arduino. The Time Library is
** set to update itself ("syncrhonize with") the built-in coil cell backed up 
** Real Time Clock (RTC) on the Teensy 4. (Which is named Teensy3Clock in the code.)
** Finally, a WWVB receiver on this PCB is monitored and, if a valid
** signal is detected from WWVB, is used to update the Teensy clock.
** WWVB broadcasts schedule daylight savings time shifts in advance, and
** this clock saves such an announcement in order to invoke it at the 
** announced time.
**
** Weather
** The author also has built and installed instances of a packet thermometer, and
** a packet rain gauge. This sketch has code that monitors those weather sensors
** and displays their results on either its LCD or LED or both.
*/


#include <SPI.h>
#include <Wire.h>
#include <EEPROM.h>
#include <LiquidCrystal.h>
#include <TimeLib.h> // RTC support

#include "Es100Wire.h"
#include "HCMS290X.h"
#include "ClockDisplay.h"
#include "PacketWeather.h"
#include "WWVBclock.h"
#include "ClockSettings.h"

#define DIM(x) sizeof(x)/sizeof(x[0])

#define WWVBCLOCK_VERSION "1.1"

// Teensy 4.0 pin assignments
namespace {  
    // Newhaven LCD
    const int LCD_DB4_PIN = 2; 
    const int LCD_DB5_PIN = 3; 
    const int LCD_DB6_PIN = 4; 
    const int LCD_DB7_PIN = 5; 
    const int LCD_OENABLE_PIN = 0; 
    const int LCD_RS_PIN = 1;
    // LCD_RW_PIN unused -- set to always W (ground)
 
    // Broadcom LED
    const int P_LED_D_IN_PIN = MOSI;
    // optional 2nd device has its Din wired to primary device's Dout
    // all other pins are wired in parallel between the two devices.
    const int P_LED_CLOCK_PIN = SCK;
    const int P_LED_RS_PIN = 21; 
    const int P_LED_BLANK_PIN = 7;
    const int P_LED_NENABLE_PIN = 6;
    const int P_LED_RESET_PIN = 10; 

    // ES100 WWVB PSK receiver uses 2Wire on Wire1 on Teensy
    const int SCL1_PIN = 16;
    const int SDA1_PIN = 17;
    const int ES100_NIRQ_PIN = 15;
    const int ES100_EN_PIN = 18;
 
    const int RFM69_NSS_PIN = 8;
    const int RFM69_INT_PIN = 9;
    const int RFM69_MISO_PIN = MISO;
    const int RFM69_RESET_PIN = 14; // not implemented

    const int SW1_INPUT_PIN = 20; 
    const int SW2_INPUT_PIN = 19;
   
    // The asserts below are sanity checks for the PCB layout and don't
    // have much to do with whether the sketch works.
    // They might have a lot to do with whether it works on
    // a particular PCB

    static_assert(MOSI==11, "CPU not on Teensy MOSI");
    static_assert(MISO==12, "CPU not on Teensy MISO");
    static_assert(SCK==13, "CPU not on Teensy SCK");
 }

namespace Settings {
    /* EEPROM offsets to hold:
    **     offset from UTC (in minutes, range: +/- 720)
    **     whether to observe DST
    */
    // stored in EEPROM
    uint32_t PacketIndoorTempIdMask;
    uint32_t PacketOutdoorTempIdMask;
    uint32_t PacketRaingaugeIdMask;
    int16_t TimeZoneOffset; 
    uint8_t observeDST;
    uint8_t LedCurrent;
    uint8_t LedPwm;
    uint8_t dstInEffect;
    uint8_t rotateLed180;
    uint8_t unitsInMetric;
    uint8_t TwelveHourDisplay;
    uint8_t Es100Enable;
    uint8_t TimeDisplayFont;
    uint8_t UseFlippedFonts;
    uint8_t Hcms290xEnable;
    uint8_t TryRadioSilence;
    uint8_t StartupDelaySeconds;
    const uint8_t STARTUP_DELAY_MAX_SECONDS = 50;
    uint16_t RainGaugeCorrection;

   enum class EepromAddresses {WWVBCLOCK_START = (~0x7u & (7 + RadioConfiguration::EepromAddresses::TOTAL_EEPROM_USED)),
        PACKET_INDOOR_THERMOMETER_MASK = WWVBCLOCK_START,
        PACKET_OUTDOOR_THERMOMETER_MASK = PACKET_INDOOR_THERMOMETER_MASK + sizeof(PacketIndoorTempIdMask),
        PACKET_RAINGAUGE_MASK = PACKET_OUTDOOR_THERMOMETER_MASK + sizeof(PacketOutdoorTempIdMask),
        TIME_ZONE_OFFSET = PACKET_RAINGAUGE_MASK + sizeof(PacketRaingaugeIdMask),
        OBSERVE_DST = TIME_ZONE_OFFSET + sizeof(TimeZoneOffset),
        LED_CURRENT = OBSERVE_DST + sizeof(observeDST),
        LED_PWM = LED_CURRENT + sizeof(LedCurrent),
        DST_IN_EFFECT = LED_PWM + sizeof(LedPwm),
        ROTATE_LED_180 = DST_IN_EFFECT + sizeof(dstInEffect),
        UNITS_IN_METRIC = ROTATE_LED_180 + sizeof(rotateLed180),
        TWELVE_HOUR_DISPLAY = UNITS_IN_METRIC + sizeof(unitsInMetric),
        ES100_ENABLE = TWELVE_HOUR_DISPLAY + sizeof(TwelveHourDisplay),
        CLOCK_DISPLAY_STYLE = ES100_ENABLE + sizeof(Es100Enable),
        USE_FLIPPED_FONTS = CLOCK_DISPLAY_STYLE + sizeof(TimeDisplayFont),
        HCMS290x_ENABLE = USE_FLIPPED_FONTS + sizeof(UseFlippedFonts),
        TRY_RADIO_SILENCE = HCMS290x_ENABLE + sizeof(Hcms290xEnable),
        STARTUP_DELAY_SECONDS = TRY_RADIO_SILENCE + sizeof(TryRadioSilence),
        RAINGAUGE_CORRECTION = STARTUP_DELAY_SECONDS + sizeof(StartupDelaySeconds),
        TOTAL_EEPROM_USED = RAINGAUGE_CORRECTION + sizeof(RainGaugeCorrection),
    };
}

namespace {
    const int32_t T1_minute_msec = 60u * 1000;
    const int32_t T23_HOURS_msec = 23u * 60u * T1_minute_msec;
    const int32_t T1_Hour_msec = 60u * T1_minute_msec;
    const int32_t T30_seconds_msec = T1_minute_msec/2;

    PacketWeather packetWeather(RFM69_NSS_PIN, RFM69_INT_PIN);
    // front panel LCD 8x2 character display
    LiquidCrystal lcd(LCD_RS_PIN, LCD_OENABLE_PIN, LCD_DB4_PIN, LCD_DB5_PIN, LCD_DB6_PIN, LCD_DB7_PIN);
}

namespace {
    // WWVB receiver on 2wire interface (aka i2c).
    Es100Wire es100Wire(ES100_NIRQ_PIN, ES100_EN_PIN, Wire1);
    time_t getTeensy3Time() {  return Teensy3Clock.get();    } 
    
    /* Keeping the battery backed up Teensy3Time up to date with the WWVB receiver:
    **
    ** (a) At power up, tell the TimeLib to sync with Teensy.
    ** Also at system power up, enable the WWVB receiver. 
    **
    ** (b) When it receives a WWVB update, sync the Teensy with WWVB. 
    **
    ** (c) if no WWVB is received for T23_HOURS_msec hours, turn OFF both the LED and LCD displays,
    ** hoping for better reception.
    ** In this "delayed WWVB reception" state, if wither SW1 or SW2 is pressed, turn ON
    ** the displays temporarily.
    **
    ** (d) Having sync'd once, disable the WWVB receiver for an hour, then turn it on.
    ** Go back to (b)
    */ 

    bool wwvbSynced;
    unsigned long wwvbSyncTimeMsec;
    unsigned long wwvbSearchStartedMsec;
}

namespace {
    // HCMS290x 4 character LED display. Optionally a second one below the first
    const Raster5x7Font *fonts[NUM_LED_FONTS] =
    {   // ORDER in array MUST MATCH enum Led_Font_enum
        &gOem5x7Font,
        &gSmallDigits5x7Font,
        &gDigits7Seg5x7Font,
        &gOem5x7FontFlipped,
        &gSmallDigits5x7FontFlipped,
        &gDigits7Seg5x7FontFlipped,

    };
    Hcms290xType_t hcms290X(P_LED_NENABLE_PIN, P_LED_RS_PIN, P_LED_BLANK_PIN, P_LED_RESET_PIN,
        NUM_LED_FONTS, fonts);

    ClockDisplay clockDisplay(lcd, hcms290X);
    ClockSettings clockSettings(lcd);

    bool radioSilence;
    void beginRadioSilence()
    {
#if USE_SERIAL
        if (!radioSilence)
            Serial.println("Starting radio silence");
#endif
        radioSilence = true;
        clockDisplay.setRadioSilence(radioSilence);
    }
    void endRadioSilence()
    {
        radioSilence = false;
        clockDisplay.setRadioSilence(radioSilence);
    }

    const int CMD_BUFLEN = 80;
    char cmdbuf[CMD_BUFLEN];
 }

 using namespace Settings;

static void printParameters()
{
#if USE_SERIAL
    Serial.print(F("TimeZoneOffset, in hours: "));
    Serial.println(static_cast<int>(TimeZoneOffset/60));
    Serial.print(F("12Hour display:"));
    Serial.println(static_cast<int>(TwelveHourDisplay));
    Serial.print(F("Clock display style:"));
    Serial.println(static_cast<int>(TimeDisplayFont));
    Serial.print(F("Observe DST:"));
    Serial.println(static_cast<int>(observeDST));
    Serial.print(F("DST in effect now:"));
    Serial.println(static_cast<int>(dstInEffect));
    Serial.print(F("Rotate180:"));
    Serial.println(static_cast<int>(rotateLed180));
    Serial.print(F("Use flipped fonts:"));
    Serial.println(static_cast<int>(UseFlippedFonts));
    Serial.print(F("Es100Enable:"));
    Serial.println(static_cast<int>(Es100Enable));
    Serial.print(F("Hcms290xEnable:"));
    Serial.println(static_cast<int>(Hcms290xEnable));
    Serial.print(F("TryRadioSilence:"));
    Serial.println(static_cast<int>(TryRadioSilence));
    Serial.print(F("Units in metric:"));
    Serial.println(static_cast<int>(unitsInMetric));
    Serial.print(F("Led Current:"));
    Serial.println(static_cast<int>(LedCurrent));
    Serial.print(F("Led PWM:"));
    Serial.println(static_cast<int>(LedPwm));
    Serial.print(F("Indoor thermometers: 0x"));
    Serial.println(PacketIndoorTempIdMask, HEX);
    Serial.print(F("Outdoor thermometers: 0x"));
    Serial.println(PacketOutdoorTempIdMask, HEX);
    Serial.print(F("Raingauge mask: 0x"));
    Serial.println(PacketRaingaugeIdMask, HEX);
    Serial.print(F("StartupDelaySeconds="));
    Serial.println(static_cast<int>(StartupDelaySeconds));
    Serial.print(F("RainGaugeCorrection="));
    Serial.println(RainGaugeCorrection);
#endif
}

void restoreAllSettings()
{
    EEPROM.get(static_cast<uint16_t>(EepromAddresses::TIME_ZONE_OFFSET), TimeZoneOffset);
    EEPROM.get(static_cast<uint16_t>(EepromAddresses::OBSERVE_DST), observeDST);
    EEPROM.get(static_cast<uint16_t>(EepromAddresses::LED_CURRENT), LedCurrent);
    EEPROM.get(static_cast<uint16_t>(EepromAddresses::LED_PWM), LedPwm);
    EEPROM.get(static_cast<uint16_t>(EepromAddresses::DST_IN_EFFECT), dstInEffect);
    EEPROM.get(static_cast<uint16_t>(EepromAddresses::ROTATE_LED_180), rotateLed180);
    EEPROM.get(static_cast<uint16_t>(EepromAddresses::PACKET_INDOOR_THERMOMETER_MASK), PacketIndoorTempIdMask);
    EEPROM.get(static_cast<uint16_t>(EepromAddresses::PACKET_OUTDOOR_THERMOMETER_MASK), PacketOutdoorTempIdMask);
    EEPROM.get(static_cast<uint16_t>(EepromAddresses::PACKET_RAINGAUGE_MASK), PacketRaingaugeIdMask);
    EEPROM.get(static_cast<uint16_t>(EepromAddresses::UNITS_IN_METRIC), unitsInMetric);
    EEPROM.get(static_cast<uint16_t>(EepromAddresses::TWELVE_HOUR_DISPLAY), TwelveHourDisplay);
    EEPROM.get(static_cast<uint16_t>(EepromAddresses::ES100_ENABLE), Es100Enable);
    EEPROM.get(static_cast<uint16_t>(EepromAddresses::HCMS290x_ENABLE), Hcms290xEnable);
    EEPROM.get(static_cast<uint16_t>(EepromAddresses::CLOCK_DISPLAY_STYLE), TimeDisplayFont);
    EEPROM.get(static_cast<uint16_t>(EepromAddresses::USE_FLIPPED_FONTS), UseFlippedFonts);
    EEPROM.get(static_cast<uint16_t>(EepromAddresses::TRY_RADIO_SILENCE), TryRadioSilence);
    EEPROM.get(static_cast<uint16_t>(EepromAddresses::STARTUP_DELAY_SECONDS), StartupDelaySeconds);
    if (StartupDelaySeconds > STARTUP_DELAY_MAX_SECONDS) StartupDelaySeconds = STARTUP_DELAY_MAX_SECONDS;
    EEPROM.get(static_cast<uint16_t>(EepromAddresses::RAINGAUGE_CORRECTION), RainGaugeCorrection);
 }

void setup()
{
    if (StartupDelaySeconds > 0)
        delay(1000 * StartupDelaySeconds);
    restoreAllSettings();
    // when using SPI on more than one device, it is crucial to set all their Slave Select pins HIGH before SPI.begin()
    pinMode(RFM69_NSS_PIN, OUTPUT);
    digitalWrite(RFM69_NSS_PIN, HIGH);
    pinMode(P_LED_NENABLE_PIN, OUTPUT);
    digitalWrite(P_LED_NENABLE_PIN, HIGH);
    SPI.begin(); // sets the SPI pins in their Input/output state. Leave them that way
    packetWeather.setup();
    setSyncProvider(getTeensy3Time);
#if USE_SERIAL
    Serial.begin(115200);
    delay(100);
    Serial.println("Wwvb Clock version " WWVBCLOCK_VERSION);
#endif

    // deal with possibility that the EEPROM is its unitialized all-bits-set state
    if (TimeZoneOffset == 0xFFFFu) // EEPROM unitialized?
        TimeZoneOffset = 0;
    if (observeDST == 0xFFu)
        observeDST = 0;
    if (PacketIndoorTempIdMask == 0xFFFFFFFFu)
        PacketIndoorTempIdMask = 0;
    if (PacketOutdoorTempIdMask == 0xFFFFFFFFu)
        PacketOutdoorTempIdMask = 0;
    if (PacketRaingaugeIdMask == 0xFFFFFFFFu)
        PacketRaingaugeIdMask = 0;

    printParameters();

    es100Wire.setup(Es100Enable);  
    hcms290X.setup(Hcms290xEnable);
    hcms290X.setLedCurrent(LedCurrent);
    hcms290X.setledPWM(LedPwm);
    hcms290X.setRotate180(rotateLed180 != 0);
    clockSettings.setup(); 
    clockDisplay.setup();
    clockDisplay.setUtcMinutesOffset(TimeZoneOffset);
    clockDisplay.setDisplayStyle(static_cast<ClockDisplay::TimeDisplaySyle>(TimeDisplayFont));
    clockDisplay.useFlippedFonts(UseFlippedFonts != 0); 
    clockDisplay.setRainGaugeCorrection(RainGaugeCorrection);
    packetWeather.radioPrintInfo();
    packetWeather.SetThermometerIdMasks(PacketIndoorTempIdMask, PacketOutdoorTempIdMask);
    packetWeather.SetRaingaugeIdMask(PacketRaingaugeIdMask);

    pinMode(SW1_INPUT_PIN, INPUT_PULLUP);
    pinMode(SW2_INPUT_PIN, INPUT_PULLUP);
 
    auto teensyNow = Teensy3Clock.get();
    setTime(teensyNow);
    DEBUG_OUTPUT1(F("Teensy time now:"));
    DEBUG_OUTPUT1(teensyNow);
    DEBUG_OUTPUT1('\n');
    clockDisplay.setDST(dstInEffect && observeDST);
    clockDisplay.unitsInMetric(unitsInMetric != 0);
    clockDisplay.set12Hour(TwelveHourDisplay != 0);
    packetWeather.setNotify(&clockDisplay);
    wwvbSearchStartedMsec = millis();
#if USE_SERIAL
    Serial.println(F("setup() complete"));
#endif
}

static void dstScheduleFromWwvbToClock()
{   // when WWVB schedule for DST is received, give it to the clock
    if (observeDST)
    {
        time_t dayUTCstarts;
        bool begins;
        uint8_t localHour;
        if (es100Wire.ScheduledDst(begins, dayUTCstarts, localHour))
            clockDisplay.scheduleDSTchangeAt(begins, dayUTCstarts, localHour);
    }
}

static bool compareCommand(const char *p, const char *&incoming)
{
    const char *q = incoming;
    for (;;)
    {
        auto c1 = *p++;
        if (c1 == 0)
        {
            incoming = q;
            return true;
        }
        if (islower(c1))
            c1 = toupper(c1);
        auto c2 = *q++;
        if (!c2)
            return false;
        if (islower(c2))
            c2 = toupper(c2);
        if (c1 != c2)
            return false;
    }
    return false;
}

const char * const CLOCKCOMMANDS[] =
{
    "ListCommands",
    "TimeZoneOffset=",
    "ObserveDST=",
    "LedCurrent=",
    "LedPWM=",
    "DstIsInEffect=",
    "Time=",
    "RotateLed180=",
    "IndoorThermometerMask=",
    "OutdoorThermometerMask=",
    "RaingaugeMask=",
    "MetricUnits=",
    "WwvbSynced=",
    "12HourDisplay=",
    "Es100Enable=",
    "TimeDisplayFont=",
    "UseFlippedFonts=",
    "Hcms290xEnable=",
    "TryRadioSilence=",
    "StartupDelaySeconds=",
    "RainGaugeCorrect=",
    "MonitorRSSI=",
    "BeginRadioSilence",
    "EndRadioSilence",
    "TransmitMessage",
    "PrintClock",
    "PrintRadio",
    "PrintParameters",
};

static bool ProcessCommand(const char *cmd, uint8_t len)
{
    uint8_t cmdIdx = 0;
    if (compareCommand(CLOCKCOMMANDS[cmdIdx++], cmd))
    {
#if USE_SERIAL
        Serial.println("Commands:");
        for (unsigned i = 0; i < DIM(CLOCKCOMMANDS); i++)
        {
            Serial.print("   ");
            Serial.println(CLOCKCOMMANDS[i]);
        }
#endif
        return true;
    }
    if (compareCommand(CLOCKCOMMANDS[cmdIdx++], cmd)) //     "TimeZoneOffset=",
    {
        static const char upperMask = 'a' & ~'A';
        const char upperFirst = *cmd | upperMask;
        switch (upperFirst)
        {
            case 'e':
                TimeZoneOffset = -5 * 60;
                break;
            case 'c':
                TimeZoneOffset = -6 * 60;
                break;
            case 'm':
                TimeZoneOffset = -7 * 60;
                break;
            case 'p':
                TimeZoneOffset = -8 * 60;
                break;
            default:
                TimeZoneOffset = static_cast<int16_t>(aDecimalToInt(cmd));
                break;
        }
        EEPROM.put(static_cast<uint16_t>(EepromAddresses::TIME_ZONE_OFFSET), TimeZoneOffset);
        DEBUG_OUTPUT1("TimeZoneOffset=");
        DEBUG_OUTPUT1(TimeZoneOffset);
        DEBUG_OUTPUT1('\n');
        clockDisplay.setUtcMinutesOffset(TimeZoneOffset);
        return true;
    }
    if (compareCommand(CLOCKCOMMANDS[cmdIdx++], cmd))    //    "ObserveDST=",
    {
        observeDST = static_cast<uint8_t>(aDecimalToInt(cmd));
        EEPROM.put(static_cast<uint16_t>(EepromAddresses::OBSERVE_DST), observeDST);
        clockDisplay.setDST(dstInEffect && observeDST);
        dstScheduleFromWwvbToClock();
        return true;
    }
    if (compareCommand(CLOCKCOMMANDS[cmdIdx++], cmd)) //    "LedCurrent=",
    {
        LedCurrent = 0x3u & static_cast<uint8_t>(aDecimalToInt(cmd));
        EEPROM.put(static_cast<uint16_t>(EepromAddresses::LED_CURRENT), LedCurrent);
        hcms290X.setLedCurrent(LedCurrent);
        return true;
    }    
    if (compareCommand(CLOCKCOMMANDS[cmdIdx++], cmd)) //     "LedPWM=",
    {
        LedPwm = 0xFu & static_cast<uint8_t>(aDecimalToInt(cmd));
        EEPROM.put(static_cast<uint16_t>(EepromAddresses::LED_PWM), LedPwm);
        hcms290X.setledPWM(LedPwm);
        return true;
    }    
    if (compareCommand(CLOCKCOMMANDS[cmdIdx++], cmd))  //  "DstIsInEffect=",
    {
        if (!cmd || !cmd[0])
        {
#if USE_SERIAL
            Serial.print(F("Dst in effect="));
            Serial.println(static_cast<int>(dstInEffect));
#endif
        }
        else
        {
            dstInEffect = static_cast<uint8_t>(aDecimalToInt(cmd));
            EEPROM.put(static_cast<uint16_t>(EepromAddresses::DST_IN_EFFECT), dstInEffect);
            clockDisplay.setDST(dstInEffect && observeDST);
        }
        return true;
    }  
    if (compareCommand(CLOCKCOMMANDS[cmdIdx++], cmd)) // "Time=",
    {
        auto utc = static_cast<time_t>(aDecimalToInt(cmd));
        DEBUG_OUTPUT1(F("As set:"));
        DEBUG_OUTPUT1(utc);
        DEBUG_OUTPUT1('\n');
        Teensy3Clock.set(utc);
        setTime(utc);
        return true;
    }  
    if (compareCommand(CLOCKCOMMANDS[cmdIdx++], cmd)) // "RotateLed180=",
    {
        rotateLed180 = static_cast<uint8_t>(aDecimalToInt(cmd));
        DEBUG_OUTPUT1(F("Rotate180 is now:"));
        DEBUG_OUTPUT1(static_cast<unsigned>(rotateLed180));
        DEBUG_OUTPUT1('\n');
        EEPROM.put(static_cast<uint16_t>(EepromAddresses::ROTATE_LED_180), rotateLed180);
        hcms290X.setRotate180(rotateLed180 != 0);
        clockDisplay.updateDisplay();
        return true;
    }  
    if (compareCommand(CLOCKCOMMANDS[cmdIdx++], cmd)) //IndoorThermometerMask=",
    {
        PacketIndoorTempIdMask = aHexToInt(cmd);
        EEPROM.put(static_cast<uint16_t>(EepromAddresses::PACKET_INDOOR_THERMOMETER_MASK), PacketIndoorTempIdMask);
        packetWeather.SetThermometerIdMasks(PacketIndoorTempIdMask,PacketOutdoorTempIdMask);
        return true;
    }  
    if (compareCommand(CLOCKCOMMANDS[cmdIdx++], cmd)) // "OutdoorThermometerMask=",
    {
        PacketOutdoorTempIdMask = aHexToInt(cmd);
        EEPROM.put(static_cast<uint16_t>(EepromAddresses::PACKET_OUTDOOR_THERMOMETER_MASK), PacketOutdoorTempIdMask);
        packetWeather.SetThermometerIdMasks(PacketIndoorTempIdMask,PacketOutdoorTempIdMask);
        return true;
    }  
    if (compareCommand(CLOCKCOMMANDS[cmdIdx++], cmd)) // "RaingaugeMask=",
    {
        PacketRaingaugeIdMask = aHexToInt(cmd);
        EEPROM.put(static_cast<uint16_t>(EepromAddresses::PACKET_RAINGAUGE_MASK), PacketRaingaugeIdMask);
        packetWeather.SetRaingaugeIdMask(PacketRaingaugeIdMask);
        return true;
    }  
    if (compareCommand(CLOCKCOMMANDS[cmdIdx++], cmd)) // "MetricUnits=",
    {
        auto c = cmd[0];
        bool metric = (c == 'Y' || c == 'y' || c == '1');
        if (c != 0)
        {
            unitsInMetric = metric ? 1 : 0;
            EEPROM.put(static_cast<uint16_t>(EepromAddresses::UNITS_IN_METRIC), unitsInMetric);
            clockDisplay.unitsInMetric(unitsInMetric != 0);
        }
#if USE_SERIAL
        Serial.print("unitsInMetric=");
        Serial.println(static_cast<unsigned>(unitsInMetric));
#endif
        return true;
    }  
    if (compareCommand(CLOCKCOMMANDS[cmdIdx++], cmd)) // "WwvbSynced=",
    {
        if (cmd && cmd[0])
        {
            auto c = cmd[0];
            wwvbSynced = c == 'Y' || c == 'y' || c == '1';
            if (wwvbSynced)
            {
                auto ms = millis();
                static_assert(sizeof(ms) == sizeof(wwvbSyncTimeMsec), "wrong timer datatype");
                wwvbSyncTimeMsec = ms;
            }
            else if (c == '-')
            {
                cmd += 1;
                auto nw = millis();
                wwvbSearchStartedMsec = static_cast<unsigned long>(nw - (1000 * 60l * aDecimalToInt(cmd)));
            }
        }
        {
#if USE_SERIAL
            Serial.print("Wwvb is ");
            if (!wwvbSynced) Serial.print("not");
            Serial.println(" synced");
#endif
        }
        return true;
    }
    if (compareCommand(CLOCKCOMMANDS[cmdIdx++], cmd)) // "12HourDisplay="
    {
        if (cmd && cmd[0])
        {
            auto c = cmd[0];
            TwelveHourDisplay = (c == 'Y' || c == 'y' || c == '1') ? 1 : 0;
            EEPROM.put(static_cast<uint16_t>(EepromAddresses::TWELVE_HOUR_DISPLAY), TwelveHourDisplay);
            clockDisplay.set12Hour(TwelveHourDisplay != 0);
        }
        {
#if USE_SERIAL
            Serial.print("TwelveHourDisplay is ");
            Serial.println(static_cast<int>(TwelveHourDisplay));
#endif
        }
        return true;
    }
   
    if (compareCommand(CLOCKCOMMANDS[cmdIdx++], cmd)) //     "Es100Enable=",
    {
        if (cmd && cmd[0])
        {
            auto c = cmd[0];
            Es100Enable = (c == 'Y' || c == 'y' || c == '1') ? 1 : 0;
            EEPROM.put(static_cast<uint16_t>(EepromAddresses::ES100_ENABLE), Es100Enable);
        }
        {
#if USE_SERIAL
            Serial.print("Es100Enable is ");
            Serial.println(static_cast<int>(Es100Enable));
#endif
        }
        return true;
    }

    if (compareCommand(CLOCKCOMMANDS[cmdIdx++], cmd)) //     "TimeDisplayFont=",   
    {
        if (cmd && cmd[0])
        {
            TimeDisplayFont   = aDecimalToInt(cmd);
            EEPROM.put(static_cast<uint16_t>(EepromAddresses::CLOCK_DISPLAY_STYLE), TimeDisplayFont);
        }
        {
            clockDisplay.setDisplayStyle(static_cast<ClockDisplay::TimeDisplaySyle>(TimeDisplayFont));
#if USE_SERIAL
            Serial.print("TimeDisplayFont is ");
            Serial.println(static_cast<int>(TimeDisplayFont));
#endif
        }
        return true;
    } 

    if (compareCommand(CLOCKCOMMANDS[cmdIdx++], cmd)) //    "UseFlippedFonts=",
    {
        auto c = cmd[0];
        if (cmd && c)
        {
            UseFlippedFonts = (c == 'Y' || c == 'y' || c == '1') ? 1 : 0;
            EEPROM.put(static_cast<uint16_t>(EepromAddresses::USE_FLIPPED_FONTS), UseFlippedFonts);
        }
        {
            clockDisplay.useFlippedFonts(UseFlippedFonts != 0);
#if USE_SERIAL
            Serial.print("UseFlippedFonts is ");
            Serial.println(static_cast<int>(UseFlippedFonts));
#endif
        }
        return true;
    } 

    if (compareCommand(CLOCKCOMMANDS[cmdIdx++], cmd)) //     "Hcms290xEnable=",
    {
        if (cmd && cmd[0])
        {
            auto c = cmd[0];
            Hcms290xEnable = (c == 'Y' || c == 'y' || c == '1') ? 1 : 0;
            EEPROM.put(static_cast<uint16_t>(EepromAddresses::HCMS290x_ENABLE), Hcms290xEnable);
        }
        {
#if USE_SERIAL
            Serial.print("Hcms290xEnable is ");
            Serial.println(static_cast<int>(Hcms290xEnable));
#endif
        }
        return true;
    } 

    if (compareCommand(CLOCKCOMMANDS[cmdIdx++], cmd)) //     "TryRadioSilence=",
    {
        if (cmd && cmd[0])
        {
            auto c = cmd[0];
            TryRadioSilence = (c == 'Y' || c == 'y' || c == '1') ? 1 : 0;
            EEPROM.put(static_cast<uint16_t>(EepromAddresses::TRY_RADIO_SILENCE), TryRadioSilence);
        }
#if USE_SERIAL
        Serial.print("TryRadioSilence is ");
        Serial.println(static_cast<int>(TryRadioSilence));
#endif
        return true;
    }
        
    if (compareCommand(CLOCKCOMMANDS[cmdIdx++], cmd)) //     "StartupDelaySeconds=",
    {
        if (cmd && cmd[0])
        {
            StartupDelaySeconds   = aDecimalToInt(cmd);
            EEPROM.put(static_cast<uint16_t>(EepromAddresses::STARTUP_DELAY_SECONDS), StartupDelaySeconds);
        }
#if USE_SERIAL
        Serial.print("StartupDelaySeconds is ");
        Serial.println(static_cast<int>(StartupDelaySeconds));
#endif
        return true;
    } 
    
     if (compareCommand(CLOCKCOMMANDS[cmdIdx++], cmd)) //     "RainGaugeCorrect=",
    {
        auto temp = static_cast<int16_t>(aDecimalToInt(cmd));
        if (clockDisplay.setRainGaugeCorrection(temp))
        {
            RainGaugeCorrection = temp;
            EEPROM.put(static_cast<uint16_t>(EepromAddresses::RAINGAUGE_CORRECTION), RainGaugeCorrection);
        }
        return true;
    }

    if (compareCommand(CLOCKCOMMANDS[cmdIdx++], cmd)) // "MonitorRSSI=",
    {
        auto c = cmd[0];
        packetWeather.MonitorRSSI((c == 'Y' || c == 'y' || c == '1') ? true : false);
        return true;
    }  
    
    if (compareCommand(CLOCKCOMMANDS[cmdIdx++], cmd)) // "BeginRadioSilence",
    {
        beginRadioSilence();
        return true;
    }

    if (compareCommand(CLOCKCOMMANDS[cmdIdx++], cmd)) // "EndRadioSilence",
    {
        endRadioSilence();
        return true;
    }

    if (compareCommand(CLOCKCOMMANDS[cmdIdx++], cmd)) // "TransmitMessage",
    {
        while (isspace(*cmd)) cmd += 1;
        if (!*cmd) return true;
        int node = aDecimalToInt(cmd);
        while (isspace(*cmd)) cmd += 1;
        if (!*cmd) return true;
        packetWeather.SendRadioMessage(node, cmd);
        return true;
    }

    if (compareCommand(CLOCKCOMMANDS[cmdIdx++], cmd)) // "PrintClock",
    {
        clockDisplay.printClock();
        es100Wire.printClock();
        return true;
    }

    if (compareCommand(CLOCKCOMMANDS[cmdIdx++], cmd)) // "PrintRadio",
    {
        packetWeather.radioPrintRegs();
        return true;
    }

    if (compareCommand(CLOCKCOMMANDS[cmdIdx++], cmd)) // "PrintParameters",
    {
        printParameters();
        return true;
    }

   return false;
}

void routeCommand(const char *cmd, uint8_t len, uint8_t senderid, bool toMe)
{
    bool toPrint = toMe;
    if (packetWeather.ProcessCommand(cmd, len, senderid, toMe))
    {   
        toPrint = true;
#if USE_SERIAL
        Serial.println(F("Command accepted for radio"));
#endif
    }
    else if (toMe && ProcessCommand(cmd, len))
#if USE_SERIAL
        Serial.println(F("Command accepted for clock"))
#endif
        ;
    else if (toMe)
#if USE_SERIAL
        Serial.println(F("command not processed"))
#endif
    ;
#if USE_SERIAL
    if (toPrint)
    {
        Serial.print(F("Command: ")); 
        Serial.print(cmd); 
        if (senderid != static_cast<uint8_t>(-1))
        {
            Serial.print(F(" Sender: ")); 
            Serial.println((int)senderid);
        }
        else
            Serial.println();
    }
#endif
}

void loop()
{
    auto nowMillis = millis();
    bool sw1 = digitalRead(SW1_INPUT_PIN) == LOW;
    bool sw2 = digitalRead(SW2_INPUT_PIN) == LOW;  
    
    if (wwvbSynced)
    {   // resync with WWVB after T1_Hour
        if (static_cast<int32_t>(nowMillis - wwvbSyncTimeMsec) > T1_Hour_msec)
        {
            wwvbSynced = false;
            wwvbSearchStartedMsec = nowMillis;
#if USE_SERIAL
            Serial.println(F("Beginning search for WWVB signal."));
#endif
        }
    }
    else if (static_cast<int32_t>(nowMillis - wwvbSearchStartedMsec) > T23_HOURS_msec)
    {   // 23 hour timeout leaves 60 minutes of yesterday's successful hour available now
        if (TryRadioSilence)
        {
            static unsigned long swDisplayStartTimeMsec = 0;
            static_assert(sizeof(swDisplayStartTimeMsec) == sizeof(nowMillis), "Wrong time type");
            // been searching for 24 hours
            if (sw1 || sw2)
            {
                if (static_cast<int32_t>(nowMillis - swDisplayStartTimeMsec) > 5000) // 5 second debounce
#if USE_SERIAL
                    Serial.println(F("Switch override radio silence"))
#endif
                    ;
                swDisplayStartTimeMsec = nowMillis;
                endRadioSilence();
            } else if (static_cast<int32_t>(nowMillis - swDisplayStartTimeMsec) > T30_seconds_msec)
            {
                if (swDisplayStartTimeMsec > 0)
#if USE_SERIAL
                    Serial.println(F("Resuming radio silence."))
#endif
                    ;
                swDisplayStartTimeMsec = 0;
                beginRadioSilence();
            }
        }
    }

    if (Es100Enable && es100Wire.loop(wwvbSynced))
    {   // read es100 time and setTeensy3Time to match, if needed
        auto utc = es100Wire.getUTCandClear();
        Teensy3Clock.set(utc);
        setTime(utc);
        wwvbSynced = true;
        wwvbSyncTimeMsec = millis();
        endRadioSilence();
        auto dst = es100Wire.isDstNow();
        if (dst >= 0)
        {
            if (dstInEffect != dst)
            {
                dstInEffect = static_cast<uint8_t>(dst);
                EEPROM.put(static_cast<uint16_t>(EepromAddresses::DST_IN_EFFECT), dstInEffect);
                clockDisplay.setDST((dstInEffect!=0) && observeDST);
            }
        }
        dstScheduleFromWwvbToClock();
        clockSettings.es100UpdatedAt(utc);
#if USE_SERIAL
        Serial.println(F("Successful reception of WWVB BPSK signal"));
#endif
    }

    hcms290X.loop();
 
    packetWeather.loop();

    /* this arrangement makes clockDisplay run in the loop before
    ** clockSettings, such that clockSettings can update the
    ** clockDisplay after the time is on the LCD */
    static bool lcdEnable = true;
    clockDisplay.loop(true, lcdEnable);
    lcdEnable = !clockSettings.loop(sw1, sw2);

#if USE_SERIAL
    while (Serial.available())
    {
        static  unsigned char charsInBuf;
        auto c = Serial.read();
        if (c < 0)
            break;
        auto ch = static_cast<char>(c);
        bool isRet = ch == '\n' || ch == '\r';
        if (!isRet)
            cmdbuf[charsInBuf++] = ch;
        cmdbuf[charsInBuf] = 0;
        if (isRet || charsInBuf >= CMD_BUFLEN - 1)
        {
            routeCommand(cmdbuf, charsInBuf);
            Serial.println(F("ready>"));
            charsInBuf = 0;
        }
    }
#endif
}

int32_t aDecimalToInt(const char*& p)
{   // p is set to character following terminating non-digit, unless null
    if (p)
        while (isspace(*p)) p += 1;
    uint32_t ret = 0; 
    bool neg = false;
    if (p && (*p == '-'))
    {
        neg = true;
        p += 1;
    }
    for (;;)
    {
        auto c = *p;
        if (c >= '0' && c <= '9')
        {
            ret *= 10;
            ret += c - '0';
            p+=1;
        } else 
        {
            if (c != 0)
                p+=1;
            return neg ? -ret : ret;
        }
    }
}

uint32_t aHexToInt(const char*&p)
{   // p is set to character following terminating non-digit, unless null
    uint32_t ret = 0;
    if (p && p[0] == '0' && (p[1]=='x' || p[1] == 'X'))
        p += 2;
    for (;;)
    {
        auto c = *p;
        if (isxdigit(c))
        {
            ret *= 16;
            if (isdigit(c))
                ret += c - '0';
            else
                ret += 10 + toupper(c) - 'A';
            p+=1;
        } else
        {
            if (c != 0)
                p+=1;
            return ret;
        }
    }
}