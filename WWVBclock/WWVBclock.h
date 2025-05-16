#pragma once
#include "HCMS290X.h"
#include "WwvbClockDefinitions.h"

enum Led_Font_enum {HCMS_OEM_FONT_IDX,     HCMS_SMALLDIG_FONT_IDX,        HCMS_7SEG_FONT_IDX, 
                    FLIPPED_FONT_INCREMENT,
                    HCMS_FLIPPED_OEM_FONT_IDX = FLIPPED_FONT_INCREMENT, HCMS_FLIPPED_SMALLDIG_FONT_IDX, HCMS_FLIPPED_7SEG_FONT_IDX, 
                NUM_LED_FONTS};

#if (DUAL_ROW_LED_DISPLAY == 0)
typedef Hcms290X<LED_Hardware_e::SINGLE_ROW_OF_FOUR> Hcms290xType_t;
#elif (FLIPPED_LED_UPDOWN == 0)
typedef Hcms290X<LED_Hardware_e::DOUBLE_ROWS_OF_FOURS> Hcms290xType_t;
#else
typedef Hcms290X<LED_Hardware_e::DOUBLE_ROWS_OF_FOURS_FLIPPED_UPDOWN> Hcms290xType_t;
#endif

class ClockNotification {
public:
    virtual void notifyIndoorTemp(float tempX10Cent)=0;
    virtual void notifyOutdoorTemp(float)=0;
    virtual void notifyRainmm(float)=0;
};

 enum class ClockCommands_t {   // ORDER MUST MATCH CLOCKCOMMANDS ENTRIES
    ListCommands,
    TimeZoneOffset,
    ObserveDST,
    LedCurrent,
    LedPWM,
    DstIsInEffect,
    Time,
    RotateLed180,
    IndoorThermometerMask,
    OutdoorThermometerMask,
    RaingaugeMask,
    MetricUnits,
    WwvbSynced,
    C12HourDisplay,
    Es100Enable,
    TimeDisplayFont,
    UseFlippedFonts,
    Hcms290xEnable,
    TryRadioSilence,
    StartupDelaySeconds,
    RainGaugeCorrect,
    MonitorRSSI,
    BeginRadioSilence,
    EndRadioSilence,
    TransmitMessage,
    PrintClock,
    PrintRadio,
    PrintParameters,
 };

extern const char * const CLOCKCOMMANDS[];

extern void routeCommand(const char *cmd, uint8_t len, uint8_t senderid = -1, bool toMe = true);
extern void restoreAllSettings();
extern int32_t aDecimalToInt(const char*& p);
extern uint32_t aHexToInt(const char*&p);



