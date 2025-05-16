#pragma once

/* Templated Hcms290X
**  use  DualDisplay as true to accept up to 8 characters on display() method. 4 characters otherwise
**  Call constructor with appropriate font(s) accounting for flipped up/down projection through the WwvbClock PCB/lens
*/

struct  Raster5x7Font {
    enum {RASTER_BYTES_PER_CHARACTER = 5};
    const int gIndexOfFirstRasterableAscii;
    const int gNumberOfCharacters;
    const uint8_t * const gCharToRasters;
};

enum LED_Hardware_e {SINGLE_ROW_OF_FOUR, DOUBLE_ROWS_OF_FOURS, DOUBLE_ROWS_OF_FOURS_FLIPPED_UPDOWN};

// dual display supports a second cascaded IC above/below the primary
template <LED_Hardware_e DualDisplay=SINGLE_ROW_OF_FOUR>
class Hcms290X 
{
    public:
    /* Caller must ensure that flipped fonts are named here if either the single or
    ** double LED displays are projected and flipped */

    Hcms290X(int nEnablePin, int regSelPin, int blankingPin, int resetPin,
          unsigned numFonts,
          const Raster5x7Font **fonts);

    enum {DISPLAY_WIDTH = 4 + (DualDisplay==SINGLE_ROW_OF_FOUR ? 0 : 4)};

    void setup(bool);
    void loop(); // enable display to do time-based updated

    void setCurrentFontIdx(unsigned f) { if (f < m_numFonts) m_currentFontIdx = f;}
    void setRotate180(bool);
    void setLedCurrent(uint8_t);
    void setledPWM(uint8_t);
    void display();
    void noDisplay();
  
    /* displayString() replaces the entire contents of the display with the string at *p
    ** Up to 8 characters are displayed in DualDisplay, otherwise 4.
    ** Fewer than the maximum appear in the upper row, left-most character positions, unless rightJustify is true
    ** This class supports the WWVBclock PCB orientation of the LED ICs.
    ** On that PCB, with the HCMS209x documentation's definition of up,down,left,right,
    ** the optional second IC is mounted below the primary IC, making a second row of 4 characters. 
    ** If p points to an empty string, or is a null pointer, the display is blanked.
    ** If p points to a string longer than the display width, only the first DISPLAY_WIDTH characters 
    ** in p are displayed.
    */
    void displayString(const char *p, bool rightJustify=false); 

    protected:
        enum class LED_Current_t {LEDB1 = 2 << 4, LEDB2 = 1 << 4, LEDB3 = 0, LEDB4 = 3 << 4};

        void setupDevice(bool sleep, LED_Current_t led, uint8_t pwm);
        bool m_enabled;
        unsigned m_currentFontIdx;
        LED_Current_t m_ledCurrent;
        uint8_t m_pwm;
        bool m_sleep;
        bool m_rotate180;
        const int nEnablePin;
        const int regSelPin;
        const int blankingPin;
        const int resetPin;
        const unsigned m_numFonts;
        const Raster5x7Font ** const gRasters;
};

// These fonts are C++ files generated from other code in this repo. Notice
// the first command overwrites LED_rasters.h, and the others below all append
//      perl     ./LED_rasters.pl --normal < LED_rasters.txt   > LED_rasters.h
extern Raster5x7Font gOem5x7Font; 

//      perl     ./LED_rasters.pl --flip   < LED_rasters.txt   >> LED_rasters.h
extern Raster5x7Font gOem5x7FontFlipped;

//      5x7Rasters SmallDigits5x7Font.bmp         >> ../WWVBclock/LED_rasters.h
extern Raster5x7Font  gSmallDigits5x7Font;

///      5x7Rasters SmallDigits5x7Font.bmp -flip  >> ../WWVBclock/LED_rasters.h
extern Raster5x7Font  gSmallDigits5x7FontFlipped;

//      5x7Rasters Digits7Seg5x7Font.bmp  -nooverlays   >> ../WWVBclock/LED_rasters.h
extern Raster5x7Font  gDigits7Seg5x7Font;

///      5x7Rasters Digits7Seg5x7Font.bmp -flip  -nooverlays   >> ../WWVBclock/LED_rasters.h
extern Raster5x7Font  gDigits7Seg5x7FontFlipped;


