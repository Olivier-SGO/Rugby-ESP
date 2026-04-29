#pragma once
#include <cstdint>
#include "config.h"

// Actual pixel width for each logo (derived from file size at load time)
extern uint8_t  gCompLogoLgW[3];
extern uint8_t  gCompLogoSmW[3];

// Pixel buffers — allocated for the maximum possible width
extern uint16_t* gCompLogoLg[3];
extern uint16_t* gCompLogoSm[3];

void loadCompLogos();
int  compIndex(const char* comp);  // 0=TOP14 1=PRO D2 2=CC
