#ifndef TM_FONTS_H
#define TM_FONTS_H

#include <stdint.h>
#include <string.h>

typedef struct {
    uint16_t FontWidth;
    uint16_t FontHeight;
    const uint16_t *data;
} TM_FontDef_t;

typedef struct {
    uint16_t Length;
    uint16_t Height;
} TM_FONTS_SIZE_t;

extern TM_FontDef_t TM_Font_7x10;
extern TM_FontDef_t TM_Font_11x18;
extern TM_FontDef_t TM_Font_16x26;

char* TM_FONTS_GetStringSize(char* str, TM_FONTS_SIZE_t* SizeStruct, TM_FontDef_t* Font);

#endif