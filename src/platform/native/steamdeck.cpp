// Whether the program runs on a Steam Deck (native.h's SteamDeckModel): the firmware's names Linux shows in /sys, Valve's board
// and the model's name, "Jupiter" (the LCD Deck) or "Galileo" (the OLED one). $TWIN_STEAM_DECK says so on any computer: 1 a Deck,
// 0 not one (the checks; a Deck that should start as a PC)
#include "native.h"

#include <cstdlib>
#include <fstream>
#include <string>

namespace
{
#if defined(__linux__)
std::string FirstLine(const char* path)
{
    std::ifstream file(path);
    std::string line;
    std::getline(file, line);
    return line;
}

// The model's description, empty when it isn't a Deck
std::string FirmwareModel()
{
    if (FirstLine("/sys/devices/virtual/dmi/id/board_vendor") != "Valve")
    {
        return "";
    }

    const std::string product = FirstLine("/sys/devices/virtual/dmi/id/product_name");
    return product == "Jupiter"   ? "Jupiter, the LCD model"
           : product == "Galileo" ? "Galileo, the OLED model"
                                  : "";
}
#endif
}

const char* Native::SteamDeckModel()
{
    const char* forced = std::getenv("TWIN_STEAM_DECK");
    if (forced != nullptr && forced[0] != '\0')
    {
        return forced[0] == '1' ? "$TWIN_STEAM_DECK" : nullptr;
    }

#if defined(__linux__)
    static const std::string model = FirmwareModel();
    return model.empty() ? nullptr : model.c_str();
#else
    return nullptr;
#endif
}
