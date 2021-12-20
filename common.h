#ifndef __COMMON_H__
#define __COMMON_H__

#include <stddef.h>
#include <curses.h>	// for WINDOW

typedef unsigned char	UINT8;

typedef struct file_data
{
	size_t size;
	UINT8* data;
} FILE_DATA;

typedef struct descramble_info
{
	UINT8 bitCnt;
	UINT8 bitMap[32];
	size_t bitMask;
} DESCRMB_INFO;

typedef struct hexview_info
{
	size_t lineChrs;	// characters per line
	size_t wordSize;	// word size (bytes per group)
	size_t sepSize;		// column separator size
	size_t endOfs;		// EOF offset (file size)
	size_t startOfs;	// display start offset
} HEXVIEW_INFO;
typedef struct hexview_work
{
	size_t ofsDigits;	// number of digits required for maximum offset
	// hexview line buffer
	size_t lbLen;
	size_t lbDigPos;
	size_t lbChrPos;
	char* lineBuf;	// buffer for numbers + lines
} HEXVIEW_WORK;
typedef struct applicaton_data
{
	HEXVIEW_INFO info;
	HEXVIEW_WORK work;
	const char* fileName;
	FILE_DATA fileData;
	DESCRMB_INFO dsiAddr;
	DESCRMB_INFO dsiData;
} APP_DATA;


// descramble types
#define DST_ADDR	0
#define DST_DATA	1

// descramble modes (bitmask)
#define DSM_NONE	0x00
#define DSM_ADDR	0x01
#define DSM_DATA	0x02


// TUI functions from tui.c
void tui_main(APP_DATA* ad);

// functions from main.c
UINT8 SaveDescrambledFile(const char* fileName);

// functions from hex-output.c
void ResizeHexDisplay(const HEXVIEW_INFO* hvi, HEXVIEW_WORK* hvw);
void ShowHexDump(APP_DATA* hView, WINDOW* win, size_t startOfs, size_t lines, UINT8 descrmbMode);

// functions from descramble.c
void DSI_Init(DESCRMB_INFO* dsi, UINT8 bits);
void DSI_Invert(DESCRMB_INFO* dsi);
size_t DSI_Decode(const DESCRMB_INFO* dsi, size_t value);	// external -> internal
size_t DSI_Encode(const DESCRMB_INFO* dsi, size_t value);	// internal -> external

#endif	// __COMMON_H__
