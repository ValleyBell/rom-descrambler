#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"

// -------- Hex Output --------
void ResizeHexDisplay(const HEXVIEW_INFO* hvi, HEXVIEW_WORK* hvw)
{
	size_t remFSize = hvi->endOfs ? (hvi->endOfs - 1) : 0;
	hvw->ofsDigits = 0;
	do
	{
		hvw->ofsDigits ++;	// count number of digits required to show file offsets
		remFSize >>= 4;
	} while(remFSize > 0);
	
	hvw->lbLen = hvw->ofsDigits;	// digits for file offset
	
	hvw->lbLen += hvi->sepSize;	// space between offset and hex display
	hvw->lbDigPos = hvw->lbLen;
	hvw->lbLen += hvi->lineChrs * 2;	// hex digits per line
	hvw->lbLen += (hvi->lineChrs - 1) / hvi->wordSize;	// spaces between words
	
	hvw->lbLen += hvi->sepSize;	// spaces between hex and character display
	hvw->lbChrPos = hvw->lbLen;
	hvw->lbLen += hvi->lineChrs;
	
	hvw->lineBuf = (char*)realloc(hvw->lineBuf, hvw->lbLen + 1);	// +1 for '\0' terminator
	return;
}

size_t GetMaxBytesPerLine(const HEXVIEW_INFO* hvi, const HEXVIEW_WORK* hvw, size_t screenWidth)
{
	size_t lenStatic = hvw->ofsDigits + 2 * hvi->sepSize;
	if (screenWidth <= lenStatic)
		return 0;
	screenWidth -= lenStatic;
	
	// width =
	//	(lineChrs*2) [hex digits] +
	//	(lineChrs-1)*wordSize [spaces between groups] +
	//	(lineChrs*1) [ASCII characters]
	// -> w = (c*3) + (c-1)/g
	// -> c = (w*g+1) / (3*g+1)
	// -> c = (w*g+g) / (3*g+1) [minor adjustment to make up for round-to-zero during integer divisions]
	return ((screenWidth + 1) * hvi->wordSize) / (3 * hvi->wordSize + 1);
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

void ShowHexDump(APP_DATA* ad, WINDOW* win, size_t startOfs, size_t lines, UINT8 descrmbMode)
{
	size_t ofs = startOfs;
	size_t line;
	int startY;
	int startX;
	
	getyx(win, startY, startX);
	ad->work.lineBuf[ad->work.lbLen] = '\0';
	for (line = 0; line < lines; line ++)
	{
		size_t pos;
		size_t wBytes = ad->info.wordSize;	// remaining bytes for a word
		size_t bpDig = ad->work.lbDigPos;	// buffer position: digits
		size_t bpChr = ad->work.lbChrPos;	// buffer position: characters
		if (ofs >= ad->fileData.size)
			break;
		
		memset(ad->work.lineBuf, ' ', ad->work.lbLen);
		PrintHexOfs(ad->work.lineBuf, ofs, ad->work.ofsDigits);
		for (pos = 0; pos < ad->info.lineChrs; ofs ++, pos ++)
		{
			size_t dOfs = (descrmbMode & DSM_ADDR) ? DSI_Encode(&ad->dsiAddr, ofs) : ofs;
			if (dOfs < ad->fileData.size)
			{
				UINT8 value = (descrmbMode & DSM_DATA) ? (UINT8)DSI_Encode(&ad->dsiData, ad->fileData.data[dOfs]) : ad->fileData.data[dOfs];
				ad->work.lineBuf[bpDig] = PrintHexDigit((value >> 4) & 0x0F);	bpDig ++;
				ad->work.lineBuf[bpDig] = PrintHexDigit((value >> 0) & 0x0F);	bpDig ++;
				ad->work.lineBuf[bpChr] = PrintRawByte(value);	bpChr ++;
			}
			else
			{
				bpDig += 2;
				bpChr ++;
			}
			wBytes --;
			if (wBytes == 0)
			{
				wBytes = ad->info.wordSize;
				bpDig ++;	// space between words
			}
		}
		mvwaddstr(win, startY, startX, ad->work.lineBuf);
		startY ++;
	}
	
	return;
}
