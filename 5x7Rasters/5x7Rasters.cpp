/* 5x7Rasters for the Broadcom HCMS-290x LED 5x7 raster display
** reference: https://docs.broadcom.com/doc/5988-7539EN
** This program writes C language declarations to stdout.
**
** Normal usage would be:
**  5x7Rasters SmallDigits5x7Font.bmp >> ../WWVBclock/LED_rasters.h
**  5x7Rasters SmallDigits5x7Font.bmp -flip >> ../WWVBclock/LED_rasters.h
**  5x7Rasters Digits7Seg5x7Font.bmp -nooverlays >> ../WWVBclock/LED_rasters.h
**  5x7Rasters Digits7Seg5x7Font.bmp -nooverlays -flip >> ../WWVBclock/LED_rasters.h
** Notice all the above commands append to the header file.
** LED_rasters.pl also appends. 
*/

#include <iostream>
#include <fstream>
#include <string>
#include <array>
#include <vector>
#include <iterator>
#include <iomanip>
#include <functional>
#include <cctype>

/* This is the data structure this program generates C code
* to initialize:
        struct  Raster5x7Font {
            enum {RASTER_BYTES_PER_CHARACTER = 5};
            const int gIndexOfFirstRasterableAscii;
            const int gNumberOfCharacters;
            const uint8_t * const gCharToRasters;
        };
        extern Raster5x7Font gFont;

The initialization looks like this:

        namespace Raster5x7Font_gFont{
            const uint8_t rasters[] = {
         0x00, 0x00, 0x00, 0x00, 0x00, // " " aka: 0x20
         ... continued
         };}
        Raster5x7Font gOem5x7Font({
            0x20, // index of first font character
            95, // number of characters in this font
            Raster5x7Font_gFont::rasters});

This program generates rasters for digits only
fonts with a colon like this: (defocus your eyes to see the digits)

            0:    1:    2:    3:    4:    5:    6:    7:    8:    9:    
       ^    ----X ----X ----X ----X ----X ----X ----X ----X ----X ----X 
       ^    -XX-- XX--- XXX-- XXX-- --XX- XXXX- XXX-- XXXX- -XX-- XXXX- 
       ^    X--X- -X--- ---XX ---X- -X-X- X---- X---- ---X- X--X- X--X- 
       ^    X--X- -X--- --X-- XXX-- XXXXX XXX-- XXXX- --X-- -XX-- XXXX-
       +    X--X- -X--- X---- ---X- ---X- ---X- X--X- -X--- X--X- ---X- 
       +    -XX-- -X--- XXXX- XXX-- ---X- XXX-- XXXX- X---- -XX-- -XXX- 
       +    ----X ----X ----X ----X ----X ----X ----X ----X ----X ----X 
            ..... ..... ..... ..... ..... ..... ..... ..... ..... .....

Initialize the rasters for each character, left to right, reading the bits
bottom to top with +++ as MSB and ^^^^ as least
*/

namespace {
    typedef std::function<unsigned char(unsigned char, int)> modificationFcn_t; // translate 8 bits of raster for column n

    const unsigned char COLON_COL4_RASTER = 0x14;
    const int NUM_FONT_DIGITS = 10; // This code assumes the BMP file has a character for each of '0' through '9'
    const int NUM_HCMS290X_RASTER_COLUMNS = 5;

    enum class FontOverlay_t { AS_IS, // centered up/down
                COLON,  // centered up/down with a colon at the right
                // remainder are shifted up one pixel to make room for a "decimal"
                NO_DECIMAL, RIGHT_DECIMAL, LEFT_DECIMAL };

    // BMP reading code cribbed from: https://stackoverflow.com/questions/9296059/read-pixel-value-in-bmp-file
    std::vector<std::vector<char>> readBMP(const std::string& file)
    {
        static constexpr size_t HEADER_SIZE = 54;

        std::ifstream bmp(file, std::ios::binary);

        std::array<char, HEADER_SIZE> header;
        bmp.read(header.data(), header.size());

        auto fileSize = *reinterpret_cast<uint32_t*>(&header[2]);
        auto dataOffset = *reinterpret_cast<uint32_t*>(&header[10]);
        auto width = *reinterpret_cast<uint32_t*>(&header[18]);
        auto height = *reinterpret_cast<uint32_t*>(&header[22]);
        auto depth = *reinterpret_cast<uint16_t*>(&header[28]);

        if (0)
        {

            std::cout << "fileSize: " << fileSize << std::endl;
            std::cout << "dataOffset: " << dataOffset << std::endl;
            std::cout << "width: " << width << std::endl;
            std::cout << "height: " << height << std::endl;
            std::cout << "depth: " << depth << "-bit" << std::endl;
        }

        if (width != 70)
            throw std::runtime_error("Width of bitmap must be 70 pixels");
        if (height != 8)
            throw std::runtime_error("Height of bitmap must be 8 pixels");
        if (depth != 8)
            throw std::runtime_error("BMP format must be 8 bits per pixel (256 color)");

        // position file pointer to data
        bmp.seekg(dataOffset - HEADER_SIZE, bmp.cur);

        int rowBufLen = (width + 3) & ~3; // spec is multiple of 4

        std::vector<std::vector<char>> ret;

        std::vector<char> imgRow(rowBufLen);
        for (int i = 0; i < static_cast<int>(height); i++)
        {
            bmp.read(imgRow.data(), imgRow.size());
            ret.push_back(imgRow);
            ret.back().resize(width);
        }

        return ret;
    }

    unsigned char bitreverse(unsigned char v)
    {
        // not EXACTLY a bit reverse
        unsigned char ret = 0;
        for (int i = 0; i < 7; i++)
        {
            if (0 != (v & (1 << i)))
                ret |= 1 << (6 - i);
        }
        return ret;
    }

    void writeDeclaration(const std::vector<std::vector<char>>& b, const modificationFcn_t& f)
    {
        for (int cIdx = 0; cIdx < NUM_FONT_DIGITS; cIdx++)
        {   // There must be 10 digits in the font
            std::cout << "    ";
            for (int col = 0; col < NUM_HCMS290X_RASTER_COLUMNS; col++)
            {   // each digit has 5 columns (see Broadcom docs)
                unsigned char v = 0;
                for (int pixel8 = 1; pixel8 < 8; pixel8++)
                {   // pixel8 starts at 1 cuz 5x7 display has no bottom row
                    auto pixel = static_cast<unsigned char>(b[pixel8][col + (7 * cIdx)]);
                    if (pixel <= (1 << 7))
                        v |= 1 << (7 - pixel8);
                }
                if (f)  // translate character 
                    v = f(v, col);
                std::cout << "0x" << std::setw(2) << std::setfill('0') <<
                    std::hex << static_cast<unsigned>(v) << ", ";
            }
            std::cout << std::endl;
        }
    }

    unsigned char rasterForCharacterShiftedUpAndWithDecimal(unsigned char v, int col, FontOverlay_t decimal, const modificationFcn_t &f)
    {   
        static const unsigned char bottomPixel = 0x40;
        v >>= 1; // moves the raster UP and loses topmost pixel
        if (decimal == FontOverlay_t::LEFT_DECIMAL && col <= 1)
            v |= bottomPixel;
        else if (decimal == FontOverlay_t::RIGHT_DECIMAL && col >= 3)
            v |= bottomPixel;
        return f(v, col);
    }

    void writeDeclarations(const std::vector<std::vector<char>>& b,
        const std::string& declarationKey, char initialFontCharacter,
        const std::vector<FontOverlay_t> &overlays,
        const modificationFcn_t& f)
    {
        std::cout << "namespace Raster5x7Font_" << declarationKey << '{' << std::endl;
        std::cout << "    const uint8_t rasters[] = {" << std::endl;
        int numEntriesInFont = 0;
        for (auto overlay : overlays)
        {
            switch (overlay)
            {
            case FontOverlay_t::AS_IS:
                std::cout << "// digits 0 through 9" << std::endl;
                writeDeclaration(b, f); // wrote NUM_FONT_DIGITS to declaration
                numEntriesInFont += NUM_FONT_DIGITS;
                break;
            case FontOverlay_t::COLON:
                std::cout << "// digits 0 through 9 with trailing colon" << std::endl;
                std::cout << "// starts with character '0' + 0x" << std::hex<< static_cast<unsigned>(numEntriesInFont) << std::endl;
                writeDeclaration(b,
                    [f](unsigned char v, int col)
                    {
                        if (col == 4)
                            v |= COLON_COL4_RASTER;
                        return f(v, col);
                    });
                numEntriesInFont += NUM_FONT_DIGITS; // wrote 10 more digits
                break;
            case FontOverlay_t::NO_DECIMAL:
                std::cout << "// digits 0 through 9 shifted up a pixel" << std::endl;
                std::cout << "// starts with character '0' + 0x" << std::hex << static_cast<unsigned>(numEntriesInFont) << std::endl;
                writeDeclaration(b,
                    std::bind(&rasterForCharacterShiftedUpAndWithDecimal, std::placeholders::_1, std::placeholders::_2, FontOverlay_t::NO_DECIMAL, f));
                numEntriesInFont += NUM_FONT_DIGITS; // wrote 10 more digits
                break;
            case FontOverlay_t::RIGHT_DECIMAL:
                std::cout << "// digits 0 through 9 with right decimal" << std::endl;
                std::cout << "// starts with character '0' + 0x" << std::hex << static_cast<unsigned>(numEntriesInFont) << std::endl;
                writeDeclaration(b,
                    std::bind(&rasterForCharacterShiftedUpAndWithDecimal, std::placeholders::_1, std::placeholders::_2, FontOverlay_t::RIGHT_DECIMAL, f));
                numEntriesInFont += NUM_FONT_DIGITS; // wrote 10 more digits
                break;
            case FontOverlay_t::LEFT_DECIMAL:
                std::cout << "// digits 0 through 9 with left decimal" << std::endl;
                std::cout << "// starts with character '0' + 0x" << std::hex << static_cast<unsigned>(numEntriesInFont) << std::endl;
                writeDeclaration(b,
                    std::bind(&rasterForCharacterShiftedUpAndWithDecimal, std::placeholders::_1, std::placeholders::_2, FontOverlay_t::LEFT_DECIMAL, f));
                numEntriesInFont += NUM_FONT_DIGITS; // wrote 10 more digits
                break;
            default:
                break;
            }
        }

        std::cout << "};}" << std::endl;
        std::cout << "Raster5x7Font " << declarationKey << "({" << std::endl;
        std::cout << "    0x" << std::hex << static_cast<unsigned>(initialFontCharacter) << ", // index of initial font character" << std::endl;
        std::cout << "    " << std::dec << numEntriesInFont << ", // number of characters in this font" << std::endl;
        std::cout << "    Raster5x7Font_" << declarationKey << "::rasters});" << std::endl;
    }
}

int main(int argc, char **argv)
{
    std::string fbmp;
    bool flip = false;
    char initialFontCharacter = '0';
    std::vector<FontOverlay_t> overlays = { FontOverlay_t::AS_IS, FontOverlay_t::COLON,  FontOverlay_t::NO_DECIMAL, FontOverlay_t::RIGHT_DECIMAL, FontOverlay_t::LEFT_DECIMAL };
    for (int i = 1; i < argc; i++)
    {
        std::string arg(argv[i]);
        if (i == 1)
        {
            fbmp = arg;
            continue;
        }
        if (arg == "-flip")
            flip = true;
        else if (arg == "-nooverlays")
            overlays.resize(1);
        else
        {
            std::cerr << "unknown command argument: " << argv[i] << std::endl;
            return 0;
        }
    }

    if (fbmp.empty())
    {
        std::cerr << "usage: 5x7Rasters <bmp file name> [-flip] [-colon=0x14] [-iniChar=0]" << std::endl;
        return 0;
    }

    auto basename = fbmp;

    auto lastdot = fbmp.rfind('.');
    if (lastdot != fbmp.npos)
    {
        std::string ext;
        for (auto c : fbmp.substr(lastdot))
            ext += tolower(c);
        if (ext == ".bmp")
            basename = fbmp.substr(0, lastdot);
    }

    auto b = readBMP(fbmp);

    modificationFcn_t rev = flip ?
        [](unsigned char v, int) { return bitreverse(v); } :
        [](unsigned char v, int) { return v; };

    std::string CinstanceName = "g" + basename;
    if (flip)
        CinstanceName += "Flipped";
    std::cout << "// This is a GENERATED file. Do NOT Edit! See 5x7Rasters.cpp" << std::endl;
    // Write c++ header file with inline initialization
    writeDeclarations(b, CinstanceName, initialFontCharacter, overlays, rev );
    std::cout << std::endl;
}

