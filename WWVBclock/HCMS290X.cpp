#include <Arduino.h>
#include <SPI.h>
#include "HCMS290X.h"
#include "WwvbClockDefinitions.h"

/*
** Broadcom has kindly published a set of rasters for representing the ASCII character set
** https://docs.broadcom.com/doc/5988-7539EN
**
** Each LED character is 5x7 pixels, or 35 bits. The IC loads them as
** five 8-bit bytes--40 bits with 5 of them ignored by the IC. MSB first.
**
** The file here, LED_rasters.txt, is copied&pasted from the document at the URL above.
*/

namespace {
    const uint32_t SPI_CLOCK(200000);
    const uint8_t SPI_MODE(SPI_MODE0);
    const SPISettings spiUprightSettings(SPI_CLOCK, MSBFIRST, SPI_MODE);
    const int SLEEP_MODE_BIT = 6;
    const int PEAK_CURRENT_BITS = 4;
    const int PWM_BITS = 0;

    const int CONTROL_WORD_1_BIT = 7;
    const int CONTROL_WORD_1_SIMUL_BIT = 0;
}

template <LED_Hardware_e DualDisplay>
Hcms290X<DualDisplay>::Hcms290X(int nEnablePin, int regSelPin, int blankingPin, int resetPin,
    unsigned numFonts, const Raster5x7Font **fonts)
: m_enabled(true)
, m_currentFontIdx(0)
, m_ledCurrent(LED_Current_t::LEDB3)
, m_pwm(0xF)
, m_sleep(true)
, m_rotate180(false)
, nEnablePin(nEnablePin)
, regSelPin(regSelPin)
, blankingPin(blankingPin)
, resetPin(resetPin)
, m_numFonts(numFonts)
, gRasters(fonts)
{}

template <LED_Hardware_e DualDisplay>
void Hcms290X<DualDisplay>::setup(bool enable)
{
    m_enabled = enable;
    if (!m_enabled)
        return;
    pinMode(nEnablePin, OUTPUT);
    pinMode(regSelPin, OUTPUT);
    pinMode(blankingPin, OUTPUT);
    pinMode(resetPin, OUTPUT);

    digitalWrite(nEnablePin, HIGH);
    digitalWrite(regSelPin, HIGH);
    digitalWrite(blankingPin, HIGH); // startup with display blanked

    digitalWrite(resetPin, LOW);
    delay(1);
    digitalWrite(resetPin, HIGH);

    DEBUG_OUTPUT1(F("Hcms290X::setup()\n"));

    SPI.beginTransaction(spiUprightSettings);
    digitalWrite(nEnablePin, LOW); // latches regSelPin as CONTROL register
    
    uint8_t control1 = (1 << CONTROL_WORD_1_BIT) | (1 << CONTROL_WORD_1_SIMUL_BIT);
    SPI.transfer(control1);
    if (DualDisplay > SINGLE_ROW_OF_FOUR) // if there are two ICs, set the second one to match control writes with the first
        SPI.transfer(control1);
    SPI.transfer(control1);
    delayMicroseconds(1);
    digitalWrite(nEnablePin, HIGH); // IC transfers our control word to its internal  
    SPI.endTransaction();

    // clear the DOT register
    delayMicroseconds(1);
    digitalWrite(regSelPin, LOW);
    delayMicroseconds(1);
    SPI.beginTransaction(spiUprightSettings);
    digitalWrite(nEnablePin, LOW); // latches regSelPin as DOT register
        for (uint8_t i = 0; i < DISPLAY_WIDTH; i ++)
          for (uint8_t j = 0; j < Raster5x7Font::RASTER_BYTES_PER_CHARACTER; j++)
                SPI.transfer(0);

    delayMicroseconds(1);
    digitalWrite(nEnablePin, HIGH); // IC transfers our control word to its internal  
    SPI.endTransaction();
}

template <LED_Hardware_e DualDisplay>
void Hcms290X<DualDisplay>::setupDevice(bool sleep, LED_Current_t led, uint8_t pwm)
{
    if (!m_enabled)
        return;
    DEBUG_OUTPUT1(F("Hcms290X::setupDevice: sleep: "));
    DEBUG_OUTPUT1(static_cast<int>(sleep));
    DEBUG_OUTPUT1(F(" current: 0x"));
    DEBUG_OUTPUT2(static_cast<int>(led), HEX);
    DEBUG_OUTPUT1(F(" pwm: "));
    DEBUG_OUTPUT1(static_cast<int>(pwm));
    DEBUG_OUTPUT1('\n');
    m_ledCurrent = led;
    m_pwm = pwm & 0xF;
    m_sleep = sleep;
    digitalWrite(regSelPin, HIGH);
    delayMicroseconds(1);
    SPI.beginTransaction(spiUprightSettings);
    digitalWrite(nEnablePin, LOW); // latches regSelPin as CONTROL register
    uint8_t control0 = (sleep ? 0 : (1 << SLEEP_MODE_BIT) ) |
                static_cast<uint8_t>(led) |
                (0xf & pwm);
    SPI.transfer(control0);
    delayMicroseconds(1);
    digitalWrite(nEnablePin, HIGH); // IC transfers our control word to its internal  
    SPI.endTransaction();
    digitalWrite(blankingPin, m_sleep ? HIGH : LOW);
}

template <LED_Hardware_e DualDisplay>
void Hcms290X<DualDisplay>::setLedCurrent(uint8_t c)
{
    static const LED_Current_t convert[] = 
    {
        LED_Current_t::LEDB1,
        LED_Current_t::LEDB2,
        LED_Current_t::LEDB3,
        LED_Current_t::LEDB4,
    };
    if (c > 3)
        return;
    setupDevice(m_sleep, convert[c], m_pwm);
}

template <LED_Hardware_e DualDisplay>
void Hcms290X<DualDisplay>::setledPWM(uint8_t c)
{
    setupDevice(m_sleep, m_ledCurrent, 0xF & c);
}

template <LED_Hardware_e DualDisplay>
void Hcms290X<DualDisplay>::setRotate180(bool c)
{
    m_rotate180 = c;
}

template <LED_Hardware_e DualDisplay>
void Hcms290X<DualDisplay>::displayString(const char *p, bool rightJustify)
{
    if (!m_enabled)
        return;

    const int NUM_RASTERS = DISPLAY_WIDTH * Raster5x7Font::RASTER_BYTES_PER_CHARACTER;
    // non-reentrant. the rasters are large and because this is a microcontroller,
    // we'll allocate the buffer at link time rather than on the stack at run time
    static uint8_t rasters[NUM_RASTERS];
    memset(rasters, 0, sizeof(rasters));
    int rasterToWriteFirst = 0;

    if (p)
    {
        uint8_t k = 0;
        uint8_t i = 0;
        for (; i < DISPLAY_WIDTH; i++)
        {
            if (!*p)
                break;
            int cidx = *p++;
            if (cidx == ' ') // special case space character
            {
                for (uint8_t j =0; j < Raster5x7Font::RASTER_BYTES_PER_CHARACTER; j++)
                    rasters[k++] = 0;         
            }
            else
            {           
                // confirm cidx is in the raster table
                const auto &font = *gRasters[m_currentFontIdx];
                if (cidx >= font.gIndexOfFirstRasterableAscii)
                {
                    cidx -= font.gIndexOfFirstRasterableAscii;
                    if (cidx < font.gNumberOfCharacters)
                    {
                        int offset = cidx * font.RASTER_BYTES_PER_CHARACTER;
                        for (uint8_t j =0; j < font.RASTER_BYTES_PER_CHARACTER; j++)
                            rasters[k++] = font.gCharToRasters[j + offset];
                    }
                }
            }
        }
        if (rightJustify && i < DISPLAY_WIDTH)
            rasterToWriteFirst = i * Raster5x7Font::RASTER_BYTES_PER_CHARACTER;
    }

    if (DualDisplay == LED_Hardware_e::DOUBLE_ROWS_OF_FOURS)
    {   // swap which of the devices gets written first
        rasterToWriteFirst += DISPLAY_WIDTH/2;
        if (rasterToWriteFirst >= DISPLAY_WIDTH)
            rasterToWriteFirst -= DISPLAY_WIDTH;
    }


    // send rasters to display, 
    digitalWrite(regSelPin, LOW);
    delayMicroseconds(1);
    SPISettings spiSettings(SPI_CLOCK, m_rotate180 ? LSBFIRST : MSBFIRST,SPI_MODE);
    SPI.beginTransaction(spiSettings);
    digitalWrite(nEnablePin, LOW); // latches regSelPin as DOT register

    if (!m_rotate180)
    {
        int k = rasterToWriteFirst;
        for (uint8_t i = 0; i < DISPLAY_WIDTH; i ++)
        {
            for (uint8_t j = 0; j < Raster5x7Font::RASTER_BYTES_PER_CHARACTER; j++)
            {   
                if (k >= NUM_RASTERS)
                    k -= NUM_RASTERS;
                auto rin = rasters[k++];
                SPI.transfer(rin);
            }
        }
    }
    else
    {   /* to rotate:
        **  the SPI writes LSBFIRST
        **  we write the raster bytes in reverse order
        **  shift each raster byte one bit to deal with the LED having 7 vertical pixels (not 8)
        */
        int k = rasterToWriteFirst-1; // and the first became last
        for (uint8_t i = 0; i < DISPLAY_WIDTH; i ++)
            for (uint8_t j = 0; j < Raster5x7Font::RASTER_BYTES_PER_CHARACTER; j++)
            {   
                if (k < 0)
                    k += NUM_RASTERS;
                auto r = rasters[k--];
                r <<= 1; // its really a 7 bit raster
                SPI.transfer(r);
            }
    }

    delayMicroseconds(1);
    digitalWrite(nEnablePin, HIGH); 
    SPI.endTransaction();
}

template <LED_Hardware_e DualDisplay>
void Hcms290X<DualDisplay>::loop()
{   // blink a pixel or two?
}

template <LED_Hardware_e DualDisplay>
void Hcms290X<DualDisplay>::display()
{
    setupDevice(false, m_ledCurrent, m_pwm);
}

template <LED_Hardware_e DualDisplay>
void Hcms290X<DualDisplay>::noDisplay()
{
    auto saved = m_pwm;
    setupDevice(true, m_ledCurrent, 0);
    m_pwm = saved;
}

#include "LED_rasters.h" // GENERATED file

// tell compiler to instance all valid combinations of template
// arguments. Count on the linker to use only the one needed

template class Hcms290X<SINGLE_ROW_OF_FOUR>;
template class Hcms290X<DOUBLE_ROWS_OF_FOURS>;
template class Hcms290X<DOUBLE_ROWS_OF_FOURS_FLIPPED_UPDOWN>;
