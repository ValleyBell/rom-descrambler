#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <curses.h>

#include "common.h"


static UINT8 ReadFileData(const char* fileName, FILE_DATA* fileData);
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

static APP_DATA hexView;

int main(int argc, char* argv[])
{
	UINT8 retVal;
	
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
	hvInfo.endOfs = fData.size;
	
	hexView.info = hvInfo;
	hexView.work = hvWork;
	hexView.fileName = fileName;
	hexView.fileData = fData;
	hexView.dsiAddr = dsiAddr;
	hexView.dsiData = dsiData;
	
	tui_main(&hexView);
	
	free(hexView.fileData.data);
	free(hexView.work.lineBuf);
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
