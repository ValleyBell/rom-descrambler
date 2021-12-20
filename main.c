#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <curses.h>

#include "common.h"


static UINT8 WriteFileData(const char* fileName, size_t dataLen, const void* data);


// CM-32P ROM descrambling
//static DESCRMB_INFO dsiAddr = {17, {0,5,4,6, 1,2,3,8, 10,13,9,7, 11,12,16,14, 15}, (1<<17)-1};
// U-110 card descrambling
static DESCRMB_INFO dsiAddr = {17, {0,5,4,6, 1,2,3,15, 13,10,14,7, 12,11,16,9, 8}, (1<<17)-1};
static DESCRMB_INFO dsiData = {8, {2,7,6,4, 1,3,0,5}, 0xFF};

static HEXVIEW_INFO hvInfo = {16, 2, 3, 0};
static HEXVIEW_WORK hvWork = {0, 0, 0, 0, NULL};

static const char* fileName;
static FILE_DATA fData;

int main(int argc, char* argv[])
{
	setlocale(LC_ALL, "");	// enable UTF-8 support on Linux
	
	if (argc < 2)
	{
		printf("Usage: %s file.bin\n", argv[0]);
		return 0;
	}
	
	fileName = argv[1];
	{
		FILE* hFile = fopen(fileName, "rb");
		if (hFile == NULL)
		{
			printf("Unable to read %s\n", fileName);
			return 1;
		}
		fseek(hFile, 0, SEEK_END);
		fData.size = ftell(hFile);
		rewind(hFile);
		fData.data = (UINT8*)malloc(fData.size);
		fData.size = fread(fData.data, 1, fData.size, hFile);
		fclose(hFile);
	}
	
	hvInfo.endOfs = fData.size;
	tui_main();
	return 0;
}


const char* GetLoadedFileName(void)
{
	return fileName;
}

UINT8 SaveDescrambledFile(const char* fileName)
{
	UINT8* descData;
	UINT8 retVal;
	size_t curPos;
	
	descData = (UINT8*)malloc(fData.size);
	for (curPos = 0x00; curPos < fData.size; curPos ++)
	{
		size_t dOfs = DSI_Encode(&dsiAddr, curPos);
		if (dOfs < fData.size)
			descData[curPos] = (UINT8)DSI_Encode(&dsiData, fData.data[dOfs]);
		else
			descData[curPos] = 0x00;
	}
	
	retVal = WriteFileData(fileName, fData.size, descData);
	free(descData);
	return retVal;
}

static UINT8 WriteFileData(const char* fileName, size_t dataLen, const void* data)
{
	FILE* hFile;
	size_t writtenBytes;
	
	hFile = fopen(fileName, "wb");
	if (hFile == NULL)
		return 0xFF;
	
	writtenBytes = fwrite(data, 1, dataLen, hFile);
	fclose(hFile);
	
	if (writtenBytes < dataLen)
		return 0x01;
	else
		return 0x00;
}

// -------- Hex Output --------
HEXVIEW_INFO* GetHexViewInfo(void)
{
	return &hvInfo;
}

DESCRMB_INFO* GetDescambleInfo(UINT8 type)
{
	if (type == DST_ADDR)
		return &dsiAddr;
	else if (type == DST_DATA)
		return &dsiData;
	else
		return NULL;
}

void ResizeHexDisplay(void)
{
	size_t remFSize = hvInfo.endOfs ? (hvInfo.endOfs - 1) : 0;
	hvWork.ofsDigits = 0;
	do
	{
		hvWork.ofsDigits ++;	// count number of digits required to show file offsets
		remFSize >>= 4;
	} while(remFSize > 0);
	
	hvWork.lbLen = hvWork.ofsDigits;	// digits for file offset
	
	hvWork.lbLen += hvInfo.sepSize;	// space between offset and hex display
	hvWork.lbDigPos = hvWork.lbLen;
	hvWork.lbLen += hvInfo.lineChrs * 2;	// hex digits per line
	hvWork.lbLen += (hvInfo.lineChrs - 1) / hvInfo.wordSize;	// spaces between words
	hvWork.lbLen += hvInfo.sepSize;	// spaces between hex and character display
	hvWork.lbChrPos = hvWork.lbLen;
	
	hvWork.lbLen += hvInfo.lineChrs;
	
	hvWork.lineBuf = (char*)realloc(hvWork.lineBuf, hvWork.lbLen + 1);	// +1 for '\0' terminator
	return;
}

static char PrintHexDigit(UINT8 digit)
{
	if (digit < 0x0A)
		return '0' + digit;
	else
		return 'A' + (digit - 0x0A);
}

static char PrintRawByte(UINT8 byte)
{
	return (byte >= 0x20 && byte <= 0x7E) ? (char)byte : '.';
}

static void PrintHexByte(char* buffer, UINT8 value)
{
	buffer[0] = PrintHexDigit((value >> 4) & 0x0F);
	buffer[1] = PrintHexDigit((value >> 0) & 0x0F);
	return;
}

static void PrintHexOfs(char* buffer, size_t offset, size_t digits)
{
	size_t remDigs = digits;
	while(remDigs > 0)
	{
		remDigs --;
		buffer[remDigs] = PrintHexDigit(offset & 0x0F);
		offset >>= 4;
	}
	return;
}

void ShowHexDump(WINDOW* win, size_t startOfs, size_t lines, UINT8 descrmbMode)
{
	size_t ofs = startOfs;
	size_t line;
	int startY;
	int startX;
	
	getyx(win, startY, startX);
	hvWork.lineBuf[hvWork.lbLen] = '\0';
	for (line = 0; line < lines; line ++)
	{
		size_t pos;
		size_t wBytes = hvInfo.wordSize;	// remaining bytes for a word
		size_t bpDig = hvWork.lbDigPos;	// buffer position: digits
		size_t bpChr = hvWork.lbChrPos;	// buffer position: characters
		if (ofs >= fData.size)
			break;
		
		memset(hvWork.lineBuf, ' ', hvWork.lbLen);
		PrintHexOfs(hvWork.lineBuf, ofs, hvWork.ofsDigits);
		for (pos = 0; pos < hvInfo.lineChrs; ofs ++, pos ++)
		{
			size_t dOfs = (descrmbMode & DSM_ADDR) ? DSI_Encode(&dsiAddr, ofs) : ofs;
			if (dOfs < fData.size)
			{
				UINT8 value = (descrmbMode & DSM_DATA) ? (UINT8)DSI_Encode(&dsiData, fData.data[dOfs]) : fData.data[dOfs];
				hvWork.lineBuf[bpDig] = PrintHexDigit((value >> 4) & 0x0F);	bpDig ++;
				hvWork.lineBuf[bpDig] = PrintHexDigit((value >> 0) & 0x0F);	bpDig ++;
				hvWork.lineBuf[bpChr] = PrintRawByte(value);	bpChr ++;
			}
			else
			{
				bpDig += 2;
				bpChr ++;
			}
			wBytes --;
			if (wBytes == 0)
			{
				wBytes = hvInfo.wordSize;
				bpDig ++;	// space between words
			}
		}
		mvwaddstr(win, startY, startX, hvWork.lineBuf);
		startY ++;
	}
	
	return;
}

// -------- Descrambling --------
static size_t CalcBitmask(UINT8 bits)
{
	if (bits >= (sizeof(size_t) * 8))
		return ~(size_t)0;
	else
		return ((size_t)1 << bits) - 1;
}

void DSI_Init(DESCRMB_INFO* dsi, UINT8 bits)
{
	UINT8 bit;
	dsi->bitCnt = bits;
	for (bit = 0; bit < dsi->bitCnt; bit ++)
		dsi->bitMap[bit] = bit;
	dsi->bitMask = CalcBitmask(dsi->bitCnt);
	return;
}

void DSI_Invert(DESCRMB_INFO* dsi)
{
	DESCRMB_INFO dsiOrg;
	UINT8 bit;
	
	memcpy(dsiOrg.bitMap, dsi->bitMap, dsi->bitCnt);
	for (bit = 0; bit < dsi->bitCnt; bit ++)
		dsi->bitMap[dsiOrg.bitMap[bit]] = bit;
	return;
}

size_t DSI_Decode(const DESCRMB_INFO* dsi, size_t value)	// external -> internal
{
	size_t result = value & ~dsi->bitMask;
	UINT8 bit;
	for (bit = 0; bit < dsi->bitCnt; bit ++)
		result |= ((value >> dsi->bitMap[bit]) & 0x01) << bit;
	return result;
}

size_t DSI_Encode(const DESCRMB_INFO* dsi, size_t value)	// internal -> external
{
	size_t result = value & ~dsi->bitMask;
	UINT8 bit;
	for (bit = 0; bit < dsi->bitCnt; bit ++)
		result |= ((value >> bit) & 0x01) << dsi->bitMap[bit];
	return result;
}
