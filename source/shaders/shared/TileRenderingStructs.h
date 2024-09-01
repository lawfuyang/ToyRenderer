#ifndef _TILE_RENDERING_STRUCTS_
#define _TILE_RENDERING_STRUCTS_

#include "StructsCommon.h"

struct TileRenderingConsts
{
	Vector2U m_OutputDimensions;
	uint32_t m_TileSize;
	uint32_t m_NbTiles;
	uint32_t m_TileID;
};

#endif // #ifndef _TILE_RENDERING_STRUCTS_
