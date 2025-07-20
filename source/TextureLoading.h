#pragma once

void ReadDDSTextureFileHeader(FILE* file, class Texture& texture);
void ReadDDSMipInfos(class Texture& texture);
void ReadDDSMipData(class Texture& texture, uint32_t mip);
