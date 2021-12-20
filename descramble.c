#include <stddef.h>
#include <string.h>	// for memcpy()

#include "common.h"

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
