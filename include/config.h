#pragma once
#include <cstdint>

// ── Display ──────────────────────────────────────────────────────────────────
#define PANEL_W       128
#define PANEL_H       64
#define CHAIN_LEN     2
#define DISPLAY_W     (PANEL_W * CHAIN_LEN)   // 256
#define DISPLAY_H     PANEL_H                  // 64

// ── MatrixPortal S3 HUB75 pins ───────────────────────────────────────────────
// Verify against your board schematic if display shows garbage
#define HUB75_R1  42
#define HUB75_G1  41
#define HUB75_B1  40
#define HUB75_R2  38
#define HUB75_G2  39
#define HUB75_B2  37
#define HUB75_A   45
#define HUB75_B   36
#define HUB75_C   48
#define HUB75_D   35
#define HUB75_E   21
#define HUB75_LAT 47
#define HUB75_OE  14
#define HUB75_CLK  2

// ── Timings (ms) ─────────────────────────────────────────────────────────────
#define SCENE_SCORE_MS      8000
#define SCENE_FIXTURE_MS    8000
#define SCENE_STANDING_MS   20000
#define POLL_LIVE_MS        30000
#define POLL_NORMAL_MS      600000    // 10 min
#define POLL_LNR_MS         1800000   // 30 min
#define NTP_INTERVAL_MS     3600000   // 1 hr
#define LIVE_GRACE_MS       300000    // 5 min
#define WDT_TIMEOUT_S       30

// ── Logos ─────────────────────────────────────────────────────────────────────
#define LOGO_LG_W   64
#define LOGO_LG_H   64
#define LOGO_SM_W   16
#define LOGO_SM_H   16

// ── Scene layout ──────────────────────────────────────────────────────────────
#define CENTER_X    LOGO_LG_W                  // 64
#define CENTER_W    (DISPLAY_W - 2*LOGO_LG_W)  // 128
#define CENTER_MID  (CENTER_X + CENTER_W/2)    // 128

// ── Colors (RGB565) ───────────────────────────────────────────────────────────
#define C_BLACK   0x0000u
#define C_WHITE   0xFFFFu
#define C_GOLD    0xFD40u   // (255,168,0)  approx — corrected from 0xFD00
#define C_RED     0xD8C0u   // (220,24,0)   approx
#define C_GREEN   0x068Au   // (0,210,80)   approx
#define C_BLUE    0x023Fu   // (0,68,255)   approx — corrected from 0x021F
#define C_ORANGE  0xFC40u   // (255,136,0)  approx
#define C_PURPLE  0x780Fu   // (120,0,120)  approx
#define C_GREY    0x7BEFu

// Helper — pack RGB888 to RGB565 at compile time
#define RGB565(r,g,b) ((uint16_t)((((r)&0xF8)<<8)|(((g)&0xFC)<<3)|((b)>>3)))
