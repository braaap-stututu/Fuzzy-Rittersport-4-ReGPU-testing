#pragma once
// The Steam Deck profile (deck.cpp): settings.ini deck=auto|on|off, FM4_DECK=0|1.
void DeckProfileInit(); // before GpuStart: reads the settings, sets its defaults
bool DeckProfile();     // whether it is on
