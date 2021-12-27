#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <locale.h>
#include <curses.h>

#ifdef _MSC_VER
#define	stricmp		_stricmp
#define	strnicmp	_strnicmp
#define strdup		_strdup
#else
#define	stricmp		strcasecmp
#define	strnicmp	strncasecmp
#endif

#include "common.h"


static UINT8 ReadFileData(const char* fileName, FILE_DATA* fileData);
static UINT8 WriteFileData(const char* fileName, size_t dataLen, const void* data);


int main(int argc, char* argv[])
{
	const HEXVIEW_INFO hviDefault = {16, 2, 2, 0};
	UINT8 retVal;
	APP_DATA hexView;
	const char* fileName;
	FILE_DATA fData;
	
	setlocale(LC_ALL, "");	// enable UTF-8 support on Linux
	
	if (argc < 2)
	{
		printf("Usage: %s file.bin\n", argv[0]);
		return 0;
	}
	
	fileName = argv[1];
	retVal = ReadFileData(fileName, &fData);
	if (retVal == 0xFF)
	{
		printf("Unable to read %s\n", fileName);
		return 1;
	}
	else if (retVal == 0x80)
	{
		printf("File too large!\n");
		return 2;
	}
	else if (retVal)
	{
		printf("Load Error\n");
		return 3;
	}
	hexView.filePath = strdup(fileName);
	hexView.fileData = fData;
	hexView.cfgPath = (char*)malloc(strlen(hexView.filePath) + 4);
	sprintf(hexView.cfgPath, "%s.cfg", hexView.filePath);
	
	hexView.info = hviDefault;
	hexView.info.endOfs = fData.size;
	hexView.work.lineBuf = NULL;
	ResizeHexDisplay(&hexView.info, &hexView.work);
	DSI_Init(&hexView.dsiAddr, DSI_Size2AddrBitCount(hexView.fileData.size));
	DSI_Init(&hexView.dsiData, 8);
	hexView.cfgAutoSave = 0;
	
	retVal = LoadConfiguration(hexView.cfgPath, &hexView);
	if (! retVal)
		printf("Configuration loaded.\n");
	
	tui_main(&hexView);
	
	if (hexView.cfgAutoSave)
	{
		retVal = SaveConfiguration(hexView.cfgPath, &hexView);
		if (retVal)
			printf("Error saving configuratoin!\n");
		else
			printf("Configuration saved.\n");
	}
	
	free(hexView.fileData.data);
	free(hexView.work.lineBuf);
	free(hexView.cfgPath);
	return 0;
}


static UINT8 ReadFileData(const char* fileName, FILE_DATA* fileData)
{
	FILE* hFile = fopen(fileName, "rb");
	if (hFile == NULL)
		return 0xFF;
	
	fseek(hFile, 0, SEEK_END);
	fileData->size = ftell(hFile);
	rewind(hFile);
	
	fileData->data = (UINT8*)malloc(fileData->size);
	if (fileData->data != NULL)
		fileData->size = fread(fileData->data, 1, fileData->size, hFile);
	fclose(hFile);
	
	if (fileData->data == NULL)
		return 0x80;
	return 0x00;
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

UINT8 SaveDescrambledFile(const char* fileName, APP_DATA* ad)
{
	UINT8* descData;
	UINT8 retVal;
	size_t curPos;
	
	descData = (UINT8*)malloc(ad->fileData.size);
	for (curPos = 0x00; curPos < ad->fileData.size; curPos ++)
	{
		size_t dOfs = DSI_Encode(&ad->dsiAddr, curPos);
		if (dOfs < ad->fileData.size)
			descData[curPos] = (UINT8)DSI_Encode(&ad->dsiData, ad->fileData.data[dOfs]);
		else
			descData[curPos] = 0x00;
	}
	
	retVal = WriteFileData(fileName, ad->fileData.size, descData);
	free(descData);
	return retVal;
}

static char* str_lstrip(char* str)
{
	// trim off leading spaces
	while(*str != '\0' && isspace((unsigned char)*str))
		str ++;
	return str;	 // return new start
}

static void str_rstrip(char* str)
{
	char* end;
	for (end = str; *end != '\0'; end ++)	// seek to end of string
		;
	
	// trim off trailing spaces
	while(end > str && isspace((unsigned char)end[-1]))
		end --;
	*end = '\0';
	return;
}

static char* str_strip(char* str)
{
	str = str_lstrip(str);
	str_rstrip(str);
	return str;
}

static int str_startswith_i(const char* str, const char* prefix)
{
	return strnicmp(str, prefix, strlen(prefix));
}

static bool ParseVal_Bool(const char* str)
{
	if (! stricmp(str, "True"))
		return 1;
	else if (! stricmp(str, "False"))
		return 0;
	else
		return strtol(str, NULL, 0) ? 1 : 0;
}

static UINT8 ParseVal_U8(const char* str)
{
	char* end;
	unsigned long val = strtoul(str, &end, 0);
	if (end == str)
		return 0xFF;
	return (val < 0x100) ? (UINT8)val : 0xFF;
}

UINT8 LoadConfiguration(const char* fileName, APP_DATA* ad)
{
	FILE* hFile;
#define LINE_SIZE	64
	char line[LINE_SIZE];
	
	hFile = fopen(fileName, "rt");
	if (hFile == NULL)
		return 0xFF;
	while(1)
	{
		char* sKey;
		char* sValue;
		char* splitPtr;
		char* end;
		
		splitPtr = fgets(line, LINE_SIZE, hFile);
		if (splitPtr == NULL)
			break;
		
		sKey = str_strip(line);
		if (*sKey == '\0' || *sKey == ';' || *sKey == '#')
			continue;	// empty line or comment - skip
		
		splitPtr = strchr(sKey, '=');
		if (splitPtr == NULL)
			continue;	// not "key = value" - skip
		splitPtr[0] = '\0';
		str_rstrip(sKey);
		sValue = str_lstrip(&splitPtr[1]);
		
		if (!stricmp(sKey, "AutoSaveCfg"))
		{
			ad->cfgAutoSave = ParseVal_Bool(sValue);
		}
		else if (!stricmp(sKey, "BytesPerLine"))
		{
			unsigned long val = strtoul(sValue, &end, 0);
			if (end != sValue)
				ad->info.lineChrs = (size_t)val;
		}
		else if (!stricmp(sKey, "BytesPerGroup"))
		{
			unsigned long val = strtoul(sValue, &end, 0);
			if (end != sValue)
				ad->info.wordSize = (size_t)val;
		}
		else if (!str_startswith_i(sKey, "Mapping_"))
		{
			DESCRMB_INFO* dsi = NULL;
			UINT8 bit;
			
			sKey = strchr(sKey, '_') + 1;
			*sKey = (char)toupper((unsigned char)*sKey);
			if (*sKey == 'A')
				dsi = &ad->dsiAddr;
			else if (*sKey == 'D')
				dsi = &ad->dsiData;
			if (dsi == NULL)
				continue;
			sKey ++;
			if (!stricmp(sKey, "Count"))
			{
				UINT8 bitCnt = ParseVal_U8(sValue);
				if (bitCnt > 0 && bitCnt <= DESCRMB_MAXBITS)
					DSI_Resize(dsi, bitCnt);
			}
			else
			{
				UINT8 srcBit = ParseVal_U8(sKey);
				UINT8 dstBit = ParseVal_U8(sValue);
				if (srcBit < DESCRMB_MAXBITS && dstBit < DESCRMB_MAXBITS)
					dsi->bitMap[srcBit] = dstBit;
			}
		}
	}
	fclose(hFile);
	return 0x00;
}

UINT8 SaveConfiguration(const char* fileName, const APP_DATA* ad)
{
	FILE* hFile;
	UINT8 mode;
	
	hFile = fopen(fileName, "wt");
	if (hFile == NULL)
		return 0xFF;
	fprintf(hFile, "%s = %s\n", "AutoSaveCfg", ad->cfgAutoSave ? "True" : "False");
	fprintf(hFile, "%s = %u\n", "BytesPerLine", (unsigned int)ad->info.lineChrs);
	fprintf(hFile, "%s = %u\n", "BytesPerGroup", (unsigned int)ad->info.wordSize);
	for (mode = 0; mode < 2; mode ++)
	{
		const DESCRMB_INFO* dsi = NULL;
		char modeLetter = '\0';
		UINT8 bit;
		
		if (mode == 0)
		{
			modeLetter = 'A';
			dsi = &ad->dsiAddr;
		}
		else if (mode == 1)
		{
			modeLetter = 'D';
			dsi = &ad->dsiData;
		}
		if (dsi == NULL)
			continue;
		fprintf(hFile, "Mapping_%cCount = %u\n", modeLetter, dsi->bitCnt);
		for (bit = 0; bit < dsi->bitCnt; bit ++)
			fprintf(hFile, "Mapping_%c%u = %u\n", modeLetter, bit, dsi->bitMap[bit]);
	}
	fclose(hFile);
	return 0x00;
}
