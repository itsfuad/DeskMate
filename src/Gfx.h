// Gfx.h — shared ST7789 device, drawing primitives, and boot/status screens.
//
// This is the core display layer. Desk-dashboard feature modes
// render on top of it via gfxDev() and the exposed text helpers; each feature owns
// its own feature-specific rendering. Nothing feature-specific lives here.
#pragma once
#include <Arduino.h>
#include "Settings.h"

class Arduino_GFX;   // fwd-decl: only the drawing .cpp files pull in the full lib

// ---- Shared colors (RGB565) ----------------------------------------------
#define C_BLACK  0x0000
#define C_WHITE  0xFFFF
#define C_GREEN  0x07E0
#define C_RED    0xF800
#define C_GRAY   0x8410
#define C_DGRAY  0x4208
#define C_YELLOW 0xFFE0
#define C_BLUE   0x041F

// DeskMate UI palette (RGB565). Feature screens may define additional shades,
// while boot/status screens use these shared accents for a coherent system look.
#define C_UI_BG      0x0862
#define C_UI_PANEL   0x10C4
#define C_UI_PANEL2  0x10E5
#define C_UI_LINE    0x2188
#define C_UI_TEXT    0xEF9F
#define C_UI_MUTED   0x8D16
#define C_UI_CYAN    0x66B9
#define C_UI_BLUE    0x755F
#define C_UI_AMBER   0xF62A
#define C_UI_VIOLET  0x9C7F
#define C_UI_ROSE    0xFBD0
#define C_UI_GREEN   0x5630

// ---- Device lifecycle -----------------------------------------------------
void         gfxBegin(const Settings& s);
void         gfxSetBrightness(uint8_t pct, bool inverted);
void         gfxSetRotation(uint8_t r);
Arduino_GFX* gfxDev();                 // shared draw target for feature renderers

// ---- Text helpers (built-in 6x8 font, integer scaled) ---------------------
int     gfxTextW(const char* s, uint8_t size);
void    gfxDrawCentered(const char* s, int y, uint8_t size, uint16_t color);
uint8_t gfxFitSize(const char* s, int maxW, uint8_t maxSize);

// ---- Shared boot / status / diagnostic screens ----------------------------
void gfxBoot(const char* line1, const char* line2);
void gfxApInfo(const char* ssid, const char* pass, const char* ip);
void gfxMessage(const char* title, const char* msg, uint16_t titleColor);
void gfxCrash(const char* epc, const char* addr, const char* ip);  // safe-mode diag
