#ifndef _BLOOM_STRUCTS_
#define _BLOOM_STRUCTS_

#include "StructsCommon.h"

struct BloomDownsampleConsts
{
	Vector2 m_InvSourceResolution;
	uint32_t m_bIsFirstDownsample;
};

struct BloomUpsampleConsts
{
	float m_FilterRadius;
};

#endif // #ifndef _BLOOM_STRUCTS_
