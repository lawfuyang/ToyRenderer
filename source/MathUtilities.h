#pragma once

#include "extern/simplemath/SimpleMath.h"

using Vector2    = DirectX::SimpleMath::Vector2;
using Vector2I   = DirectX::XMINT2;
using Vector2U   = DirectX::XMUINT2;
using Vector3    = DirectX::SimpleMath::Vector3;
using Vector3I   = DirectX::XMINT3;
using Vector3U   = DirectX::XMUINT3;
using Vector4    = DirectX::SimpleMath::Vector4;
using Vector4I   = DirectX::XMINT4;
using Vector4U   = DirectX::XMUINT4;
using Matrix     = DirectX::SimpleMath::Matrix;

using Plane       = DirectX::SimpleMath::Plane;
using Quaternion  = DirectX::SimpleMath::Quaternion;
using Color       = DirectX::SimpleMath::Color;
using Ray         = DirectX::SimpleMath::Ray;
using Viewport    = DirectX::SimpleMath::Viewport;
using Sphere      = DirectX::BoundingSphere;
using AABB        = DirectX::BoundingBox;
using OBB         = DirectX::BoundingOrientedBox;
using Frustum     = DirectX::BoundingFrustum;

#define KINDA_SMALL_NUMBER                   1e-4f
#define KINDA_BIG_NUMBER                     1e10f
#define NullWithEpsilon(a)                   (std::fabsf(a) <= KINDA_SMALL_NUMBER)
#define EqualWithEpsilon(a, b)               (std::fabsf(a - b) <= KINDA_SMALL_NUMBER)
#define GreaterWithEpsilon(a, b)             ((a) - (b) > KINDA_SMALL_NUMBER)
#define GreaterOrEqualWithEpsilon(a, b)      ((b) - (a) < KINDA_SMALL_NUMBER)
#define LesserWithEpsilon(a, b)              ((b) - (a) > KINDA_SMALL_NUMBER)
#define LesserOrEqualWithEpsilon(a, b)       ((a) - (b) < KINDA_SMALL_NUMBER)

inline void ScalarSinCos(float& sinResult, float& cosResult, float value) { return DirectX::XMScalarSinCos(&sinResult, &cosResult, value); }
inline bool NearEqual(const Vector4& v1, const Vector4& v2, const Vector4& epsilon = Vector4{ KINDA_SMALL_NUMBER }) { return DirectX::XMVector4NearEqual(v1, v2, epsilon); };
inline bool NearEqual(const Vector3& v1, const Vector3& v2, const Vector3& epsilon = Vector3{ KINDA_SMALL_NUMBER }) { return DirectX::XMVector3NearEqual(v1, v2, epsilon); };
inline bool NearZero(const Vector4& v) { return NearEqual(v, Vector4::Zero); }
inline bool NearZero(const Vector3& v) { return NearEqual(v, Vector3::Zero); }

#define CREATE_SIMD_FUNCTIONS( TYPE )                                                                                   \
    static TYPE Sqrt( TYPE s )                                   { return TYPE(DirectX::XMVectorSqrt(s)); }             \
    static TYPE Reciprocal( TYPE s )                             { return TYPE(DirectX::XMVectorReciprocal(s)); }       \
    static TYPE ReciprocalSqrt( TYPE s )                         { return TYPE(DirectX::XMVectorReciprocalSqrt(s)); }   \
    static TYPE Floor( TYPE s )                                  { return TYPE(DirectX::XMVectorFloor(s)); }            \
    static TYPE Ceiling( TYPE s )                                { return TYPE(DirectX::XMVectorCeiling(s)); }          \
    static TYPE Round( TYPE s )                                  { return TYPE(DirectX::XMVectorRound(s)); }            \
    static TYPE Exp( TYPE s )                                    { return TYPE(DirectX::XMVectorExp(s)); }              \
    static TYPE Pow( TYPE b, TYPE e )                            { return TYPE(DirectX::XMVectorPow(b, e)); }           \
    static TYPE Log( TYPE s )                                    { return TYPE(DirectX::XMVectorLog(s)); }              \
    static TYPE Sin( TYPE s )                                    { return TYPE(DirectX::XMVectorSin(s)); }              \
    static TYPE Cos( TYPE s )                                    { return TYPE(DirectX::XMVectorCos(s)); }              \
    static TYPE Tan( TYPE s )                                    { return TYPE(DirectX::XMVectorTan(s)); }              \
    static TYPE ASin( TYPE s )                                   { return TYPE(DirectX::XMVectorASin(s)); }             \
    static TYPE ACos( TYPE s )                                   { return TYPE(DirectX::XMVectorACos(s)); }             \
    static TYPE ATan( TYPE s )                                   { return TYPE(DirectX::XMVectorATan(s)); }             \
    static TYPE ATan2( TYPE y, TYPE x )                          { return TYPE(DirectX::XMVectorATan2(y, x)); }         \
    static TYPE Lerp( TYPE a, TYPE b, TYPE t )                   { return TYPE(DirectX::XMVectorLerpV(a, b, t)); }      \
    static TYPE MultiplyAdd( TYPE v1, TYPE v2, TYPE v3 )         { return DirectX::XMVectorMultiplyAdd(v1, v2, v3); }   \
    static TYPE VectorLess( TYPE lhs, TYPE rhs )                 { return DirectX::XMVectorLess(lhs, rhs); }            \
    static TYPE VectorLessEqual( TYPE lhs, TYPE rhs )            { return DirectX::XMVectorLessOrEqual(lhs, rhs); }     \
    static TYPE VectorGreater( TYPE lhs, TYPE rhs )              { return DirectX::XMVectorGreater(lhs, rhs); }         \
    static TYPE VectorGreaterOrEqual( TYPE lhs, TYPE rhs )       { return DirectX::XMVectorGreaterOrEqual(lhs, rhs); }  \
    static TYPE VectorEqual( TYPE lhs, TYPE rhs )                { return DirectX::XMVectorEqual(lhs, rhs); }           \
    static TYPE VectorSelect( TYPE lhs, TYPE rhs, TYPE control ) { return DirectX::XMVectorSelect(lhs, rhs, control); } \

CREATE_SIMD_FUNCTIONS(Vector3)
CREATE_SIMD_FUNCTIONS(Vector4)

inline Vector4 Normalize3(const Vector4& v4)
{
    Vector3 v3{ v4.x, v4.y, v4.z };
    v3.Normalize();
    return Vector4{ v3.x, v3.y, v3.z, 1.0f };
}

template <typename T>
inline constexpr T Saturate(T v)
{
    return std::clamp(v, static_cast<T>(0), static_cast<T>(1));
}

//damps a value using a deltatime and a speed
template <typename T> inline T Damp(T val, T target, T speed, float dt)
{
    T maxDelta = speed * dt;
    return val + std::clamp(target - val, -maxDelta, maxDelta);
}

template <typename T> T SmoothStep(T min, T max, T f)
{
    f = std::clamp((f - min) / (max - min), T(0.f), T(1.f));
    return f * f * (T(3.f) - T(2.f) * f);
}

template <typename T> T SmootherStep(T min, T max, T f)
{
    f = std::clamp((f - min) / (max - min), T(0.f), T(1.f));
    return f * f * f * (f * (f * T(6.f) - T(15.f)) + T(10.f));
}

template <typename T> constexpr bool IsAligned(T value, size_t alignment)
{
    return ((size_t)value & (alignment - 1)) == 0;
}

template <typename T> constexpr T AlignUpWithMask(T value, size_t mask)
{
    return (T)(((size_t)value + mask) & ~mask);
}

template <typename T> constexpr T AlignDownWithMask(T value, size_t mask)
{
    return (T)((size_t)value & ~mask);
}

template <typename T> constexpr T AlignUp(T value, size_t alignment)
{
    return AlignUpWithMask(value, alignment - 1);
}

template <typename T> constexpr T AlignDown(T value, size_t alignment)
{
    return AlignDownWithMask(value, alignment - 1);
}

template <typename T> constexpr T DivideByMultiple(T value, size_t alignment)
{
    return (T)((value + alignment - 1) / alignment);
}

template <typename T> constexpr bool IsPowerOfTwo(T value)
{
    return 0 == (value & (value - 1));
}

template <typename T> constexpr bool IsDivisible(T value, T divisor)
{
    return (value / divisor) * divisor == value;
}

template <typename T> constexpr T AlignPowerOfTwo(T value)
{
    return value == 0 ? 0 : 1 << Log2(value);
}

// round given value to next multiple, not limited to pow2 multiples...
constexpr uint32_t RoundToNextMultiple(uint32_t val, uint32_t multiple)
{
    uint32_t t = val + multiple - 1;
    return t - (t % multiple);
}

constexpr uint32_t GetNextPow2(uint32_t n)
{
    --n;
    n = n | (n >> 1);
    n = n | (n >> 2);
    n = n | (n >> 4);
    n = n | (n >> 8);
    n = n | (n >> 16);
    ++n;

    return n;
}

constexpr uint32_t GetLog2(uint32_t val)
{
    uint32_t pow2 = 0;
    for (pow2 = 0; pow2 < 31; ++pow2)
        if (val <= (1u << pow2))
            return pow2;

    return val;
}

/** Divides two integers and rounds up */
constexpr uint32_t DivideAndRoundUp(uint32_t Dividend, uint32_t Divisor)
{
    return (Dividend + Divisor - 1) / Divisor;
}

void ModifyPerspectiveMatrix(Matrix& mat, float nearPlane, float farPlane, bool bReverseZ, bool bInfiniteZ);
void GetFrustumCornersWorldSpace(const Matrix& projview, Vector3(&frustumCorners)[8]);
AABB MakeLocalToWorldAABB(const AABB& aabb, const Matrix& worldMatrix);
Sphere MakeLocalToWorldSphere(const Sphere& sphere, const Matrix& worldMatrix);
Vector2 ProjectWorldPositionToViewport(const Vector3& worldPos, const Matrix& viewProjMatrix, const Vector2U& viewportDim);

constexpr float PI    = 3.1415926535897932384626433832795f;
constexpr float PIBy2 = 1.5707963267948966192313216916398f;

constexpr float ConvertToRadians(float fDegrees) { return fDegrees * (PI / 180.0f); }
constexpr float ConvertToDegrees(float fRadians) { return fRadians * (180.0f / PI); }

constexpr float Cos0     = 1.0f;
constexpr float Cos1     = 0.99984769515639123915701155881391f;
constexpr float Cos2     = 0.99939082701909573000624344004393f;
constexpr float Cos3     = 0.99862953475457387378449205843944f;
constexpr float Cos4     = 0.99756405025982424761316268064426f;
constexpr float Cos5     = 0.99619469809174553229501040247389f;
constexpr float Cos6     = 0.99452189536827333692269194498057f;
constexpr float Cos10    = 0.98480775301220805936674302458952f;
constexpr float Cos15    = 0.9659258262890682867497431997289f;
constexpr float Cos20    = 0.93969262078590838405410927732473f;
constexpr float Cos22_5  = 0.92387953251128675612818318939679f;
constexpr float Cos25    = 0.90630778703664996324255265675432f;
constexpr float Cos30    = 0.86602540378443864676372317075294f;
constexpr float Cos35    = 0.81915204428899178968448838591684f;
constexpr float Cos40    = 0.76604444311897803520239265055542f;
constexpr float Cos45    = 0.70710678118654752440084436210485f;
constexpr float Cos46    = 0.69465837045899728665640629942269f;
constexpr float Cos50    = 0.64278760968653932632264340990726f;
constexpr float Cos55    = 0.57357643635104609610803191282616f;
constexpr float Cos60    = 0.5f;
constexpr float Cos65    = 0.42261826174069943618697848964773f;
constexpr float Cos67_5  = 0.3826834323650897717284599840304f;
constexpr float Cos70    = 0.34202014332566873304409961468226f;
constexpr float Cos75    = 0.25881904510252076234889883762405f;
constexpr float Cos80    = 0.17364817766693034885171662676931f;
constexpr float Cos85    = 0.08715574274765817355806427083747f;
constexpr float Cos87    = 0.05233595624294383272211862960908f;
constexpr float Cos90    = 0.0f;
constexpr float Cos95    = -0.08715574274765817355806427083747f;
constexpr float Cos100   = -0.17364817766693034885171662676931f;
constexpr float Cos105   = -0.25881904510252076234889883762405f;
constexpr float Cos110   = -0.3420201433256687330440996146822f;
constexpr float Cos112_5 = -0.3826834323650897717284599840304f;
constexpr float Cos115   = -0.4226182617406994361869784896477f;
constexpr float Cos120   = -0.5f;
constexpr float Cos125   = -0.57357643635104609610803191282616f;
constexpr float Cos130   = -0.64278760968653932632264340990726f;
constexpr float Cos135   = -0.7071067811865475244008443621048f;
constexpr float Cos140   = -0.76604444311897803520239265055542f;
constexpr float Cos145   = -0.81915204428899178968448838591684f;
constexpr float Cos150   = -0.8660254037844386467637231707529f;
constexpr float Cos155   = -0.90630778703664996324255265675432f;
constexpr float Cos157_5 = -0.92387953251128675612818318939679f;
constexpr float Cos160   = -0.93969262078590838405410927732473f;
constexpr float Cos165   = -0.9659258262890682867497431997289f;
constexpr float Cos170   = -0.98480775301220805936674302458952f;
constexpr float Cos175   = -0.99619469809174553229501040247389f;
constexpr float Cos180   = -1.0f;

constexpr float Sin10  = Cos80;
constexpr float Sin15  = Cos75;
constexpr float Sin20  = Cos70;
constexpr float Sin30  = Cos60;
constexpr float Sin40  = Cos50;
constexpr float Sin45  = Cos45;
constexpr float Sin50  = Cos40;
constexpr float Sin60  = Cos30;
constexpr float Sin70  = Cos20;
constexpr float Sin75  = Cos15;
constexpr float Sin80  = Cos10;
constexpr float Sin150 = Sin30;
constexpr float Sin165 = Sin15;

constexpr float Tan30 = 0.57735026918962576450914878050196f;
constexpr float Tan55 = 1.4281480067421145021606184849985f;

constexpr float Rad0     = 0.0f;
constexpr float Rad1     = 0.01745329251994329576923690768488f;
constexpr float Rad2_5   = 0.04363323129985823942309226921221f;
constexpr float Rad5     = 0.08726646259971647884618453842443f;
constexpr float Rad9     = 0.15707963267948966192313216916398f;
constexpr float Rad10    = 0.17453292519943295769236907684886f;
constexpr float Rad12_5  = 0.21816615649929119711546134606108f;
constexpr float Rad15    = 0.26179938779914943653855361527329f;
constexpr float Rad17_5  = 0.30543261909900767596164588448551f;
constexpr float Rad20    = 0.34906585039886591538473815369772f;
constexpr float Rad22_5  = 0.39269908169872415480783042290994f;
constexpr float Rad25    = 0.43633231299858239423092269212215f;
constexpr float Rad30    = 0.52359877559829887307710723054658f;
constexpr float Rad35    = 0.61086523819801535192329176897101f;
constexpr float Rad40    = 0.69813170079773183076947630739545f;
constexpr float Rad44    = 0.76794487087750501384642393813499f;
constexpr float Rad45    = 0.78539816339744830961566084581988f;
constexpr float Rad50    = 0.87266462599716478846184538424431f;
constexpr float Rad55    = 0.95993108859688126730802992266874f;
constexpr float Rad60    = 1.0471975511965977461542144610932f;
constexpr float Rad65    = 1.1344640137963142250003989995176f;
constexpr float Rad67_5  = 1.1780972450961724644234912687298f;
constexpr float Rad70    = 1.221730476396030703846583537942f;
constexpr float Rad75    = 1.3089969389957471826927680763665f;
constexpr float Rad80    = 1.3962634015954636615389526147909f;
constexpr float Rad90    = 1.5707963267948966192313216916398f;
constexpr float Rad100   = 1.7453292519943295769236907684886f;
constexpr float Rad105   = 1.832595714594046055769875306913f;
constexpr float Rad112_5 = 1.9634954084936207740391521145497f;
constexpr float Rad115   = 2.0071286397934790134622443837619f;
constexpr float Rad120   = 2.0943951023931954923084289221863f;
constexpr float Rad125   = 2.1816615649929119711546134606108f;
constexpr float Rad130   = 2.2689280275926284500007979990352f;
constexpr float Rad135   = 2.3561944901923449288469825374596f;
constexpr float Rad140   = 2.4434609527920614076931670758841f;
constexpr float Rad145   = 2.5307274153917778865393516143085f;
constexpr float Rad150   = 2.6179938779914943653855361527329f;
constexpr float Rad157_5 = 2.7488935718910690836548129603696f;
constexpr float Rad160   = 2.7925268031909273230779052295818f;
constexpr float Rad165   = 2.8797932657906438019240897680062f;
constexpr float Rad170   = 2.9670597283903602807702743064306f;
constexpr float Rad180   = PI;
constexpr float Rad360   = PI * 2.0f;

constexpr float Ln2    = 0.6931471805599453094172321214581f;
constexpr float Sqrt2  = 1.4142135623730950488016887242097f;
constexpr float Sqrt3  = 1.7320508075688772935274463415059f;
constexpr float Sqrt5  = 2.2360679774997896964091736687313f;
constexpr float Sqrt15 = 3.8729833462074168851792653997824f;
constexpr float SqrtPi = 1.772453850905516027298167483341f;

namespace Bezier
{
    // Performs a cubic bezier interpolation between four control points,
    // returning the value at the specified time (t ranges 0 to 1)
    Vector3 CubicInterpolate(const Vector3& p1, const Vector3& p2, const Vector3& p3, const Vector3& p4, float t);

    // Computes the tangent of a cubic bezier curve at the specified time.
    Vector3 CubicTangent(const Vector3& p1, const Vector3& p2, const Vector3& p3, const Vector3& p4, float t);

    using PatchVertexOutputFn = std::function<void(const Vector3&, const Vector3&, const Vector2&)>;
    using PatchIndexOutputFn  = std::function<void(uint32_t)>;

    // Creates vertices for a patch that is tessellated at the specified level.
    // Calls the specified outputVertex function for each generated vertex,
    // passing the position, normal, and texture coordinate as parameters.
    void CreatePatchVertices(const std::array<Vector3, 16>& patch, uint32_t tessellation, bool isMirrored, PatchVertexOutputFn outputVertexFunc);

    // Creates indices for a patch that is tessellated at the specified level.
    // Calls the specified outputIndex function for each generated index value.
    void CreatePatchIndices(uint32_t tessellation, bool isMirrored, PatchIndexOutputFn outputIndexFunc);
}
