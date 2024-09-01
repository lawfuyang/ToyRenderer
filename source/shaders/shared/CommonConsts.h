#ifndef _COMMON_CONSTS_H_
#define _COMMON_CONSTS_H_

// NOTE: this will be shared by both C++ & HLSL files. So this file must be compilable by both

static const int MaterialFlag_UseDiffuseTexture           = (1 << 0);
static const int MaterialFlag_UseNormalTexture            = (1 << 1);
static const int MaterialFlag_UseMetallicRoughnessTexture = (1 << 2);

static const int SamplerIdx_AnisotropicClamp  = 0;
static const int SamplerIdx_AnisotropicWrap   = 1;
static const int SamplerIdx_AnisotropicBorder = 2;
static const int SamplerIdx_AnisotropicMirror = 3;
static const int SamplerIdx_Count             = 4;

#endif // #define _COMMON_CONSTS_H_
