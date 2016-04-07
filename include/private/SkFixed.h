/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkFixed_DEFINED
#define SkFixed_DEFINED

#include "SkScalar.h"
#include "math.h"

#include "SkTypes.h"

/** \file SkFixed.h

    Types and macros for 16.16 fixed point
*/

/** 32 bit signed integer used to represent fractions values with 16 bits to the right of the decimal point
*/
typedef int32_t             SkFixed;
#define SK_Fixed1           (1 << 16)
#define SK_FixedHalf        (1 << 15)
#define SK_FixedMax         (0x7FFFFFFF)
#define SK_FixedMin         (-SK_FixedMax)
#define SK_FixedPI          (0x3243F)
#define SK_FixedSqrt2       (92682)
#define SK_FixedTanPIOver8  (0x6A0A)
#define SK_FixedRoot2Over2  (0xB505)

#define SkFixedToFloat(x)   ((x) * 1.52587890625e-5f)

///////////////////////////////////////////////////////////////////////////////
// ASM alternatives for our portable versions.

#if defined(SK_CPU_ARM32)
    /* This guy does not handle NaN or other obscurities, but is faster than
       than (int)(x*65536).  When built on Android with -Os, needs forcing
       to inline or we lose the speed benefit.
    */
    SK_ALWAYS_INLINE SkFixed SkFloatToFixed_arm(float x)
    {
        int32_t y, z;
        asm("movs    %1, %3, lsl #1         \n"
            "mov     %2, #0x8E              \n"
            "sub     %1, %2, %1, lsr #24    \n"
            "mov     %2, %3, lsl #8         \n"
            "orr     %2, %2, #0x80000000    \n"
            "mov     %1, %2, lsr %1         \n"
            "it cs                          \n"
            "rsbcs   %1, %1, #0             \n"
            : "=r"(x), "=&r"(y), "=&r"(z)
            : "r"(x)
            : "cc"
            );
        return y;
    }
    inline SkFixed SkFixedMul_arm(SkFixed x, SkFixed y)
    {
        int32_t t;
        asm("smull  %0, %2, %1, %3          \n"
            "mov    %0, %0, lsr #16         \n"
            "orr    %0, %0, %2, lsl #16     \n"
            : "=r"(x), "=&r"(y), "=r"(t)
            : "r"(x), "1"(y)
            :
            );
        return x;
    }

    #define SkFixedMul(x, y)           SkFixedMul_arm(x, y)
    #define SkFloatToFixed_Unsafe(x)   SkFloatToFixed_arm(x)
#else
    inline SkFixed SkFixedMul_longlong(SkFixed a, SkFixed b) {
        return (SkFixed)((int64_t)a * b >> 16);
    }

    #define SkFixedMul(x, y)           SkFixedMul_longlong(x, y)
    #define SkFloatToFixed_Unsafe(x)   ((SkFixed)((x) * SK_Fixed1))
#endif

///////////////////////////////////////////////////////////////////////////////

static inline SkFixed SkFloatToFixed(float x) {
    const SkFixed result = SkFloatToFixed_Unsafe(x);
    SkASSERT(truncf(x * SK_Fixed1) == static_cast<float>(result));
    return result;
}

// Pins over/under flows to SK_FixedMax/SK_FixedMin (slower than just a cast).
static inline SkFixed SkFloatPinToFixed(float x) {
    x *= SK_Fixed1;
    // Casting float to int outside the range of the target type (int32_t) is undefined behavior.
    if (x >= SK_FixedMax) return SK_FixedMax;
    if (x <= SK_FixedMin) return SK_FixedMin;
    const SkFixed result = static_cast<SkFixed>(x);
    SkASSERT(truncf(x) == static_cast<float>(result));
    return result;
}

#define SkFixedToDouble(x)         ((x) * 1.52587890625e-5)
#define SkDoubleToFixed_Unsafe(x)  ((SkFixed)((x) * SK_Fixed1))

static inline SkFixed SkDoubleToFixed(double x) {
    const SkFixed result = SkDoubleToFixed_Unsafe(x);
    SkASSERT(trunc(x * SK_Fixed1) == static_cast<double>(result));
    return result;
}

// Pins over/under flows to SK_FixedMax/SK_FixedMin (slower than just a cast).
static inline SkFixed SkDoublePinToFixed(double x) {
    x *= SK_Fixed1;
    // Casting double to int outside the range of the target type (int32_t) is undefined behavior.
    if (x >= SK_FixedMax) return SK_FixedMax;
    if (x <= SK_FixedMin) return SK_FixedMin;
    const SkFixed result = static_cast<SkFixed>(x);
    SkASSERT(trunc(x) == static_cast<double>(result));
    return result;
}

/** Converts an integer to a SkFixed, asserting that the result does not overflow
    a 32 bit signed integer
*/
#ifdef SK_DEBUG
    inline SkFixed SkIntToFixed(int n)
    {
        SkASSERT(n >= -32768 && n <= 32767);
        // Left shifting a negative value has undefined behavior in C, so we cast to unsigned before
        // shifting.
        return (unsigned)n << 16;
    }
#else
    // Left shifting a negative value has undefined behavior in C, so we cast to unsigned before
    // shifting. Then we force the cast to SkFixed to ensure that the answer is signed (like the
    // debug version).
    #define SkIntToFixed(n)     (SkFixed)((unsigned)(n) << 16)
#endif

#define SkFixedRoundToInt(x)    (((x) + SK_FixedHalf) >> 16)
#define SkFixedCeilToInt(x)     (((x) + SK_Fixed1 - 1) >> 16)
#define SkFixedFloorToInt(x)    ((x) >> 16)

#define SkFixedRoundToFixed(x)  (((x) + SK_FixedHalf) & 0xFFFF0000)
#define SkFixedCeilToFixed(x)   (((x) + SK_Fixed1 - 1) & 0xFFFF0000)
#define SkFixedFloorToFixed(x)  ((x) & 0xFFFF0000)

#define SkFixedAbs(x)       SkAbs32(x)
#define SkFixedAve(a, b)    (((a) + (b)) >> 1)

// Blink layout tests are baselined to Clang optimizing through undefined behavior in SkDivBits.
#if defined(SK_SUPPORT_LEGACY_DIVBITS_UB)
    #define SkFixedDiv(numer, denom) SkDivBits(numer, denom, 16)
#else
    // The divide may exceed 32 bits. Clamp to a signed 32 bit result.
    #define SkFixedDiv(numer, denom) \
        SkToS32(SkTPin<int64_t>((SkLeftShift((int64_t)numer, 16) / denom), SK_MinS32, SK_MaxS32))
#endif

///////////////////////////////////////////////////////////////////////////////

#if SK_SCALAR_IS_FLOAT

#define SkFixedToScalar(x)          SkFixedToFloat(x)
#define SkScalarToFixed(x)          SkFloatToFixed(x)
#define SkScalarPinToFixed(x)       SkFloatPinToFixed(x)

#else   // SK_SCALAR_IS_DOUBLE

#define SkFixedToScalar(x)          SkFixedToDouble(x)
#define SkScalarToFixed(x)          SkDoubleToFixed(x)
#define SkScalarPinToFixed(x)       SkDoublePinToFixed(x)

#endif

///////////////////////////////////////////////////////////////////////////////

typedef int64_t SkFixed3232;   // 32.32

#define SK_Fixed3232_1                (static_cast<SkFixed3232>(1) << 32)
#define SkIntToFixed3232(x)           (SkLeftShift((SkFixed3232)(x), 32))
#define SkFixed3232ToInt(x)           ((int)((x) >> 32))
#define SkFixedToFixed3232(x)         (SkLeftShift((SkFixed3232)(x), 16))
#define SkFixed3232ToFixed(x)         ((SkFixed)((x) >> 16))
#define SkFloatToFixed3232_Unsafe(x)  (static_cast<SkFixed3232>((x) * SK_Fixed3232_1))

static inline SkFixed3232 SkFloatToFixed3232(float x) {
    const SkFixed3232 result = SkFloatToFixed3232_Unsafe(x);
    SkASSERT(truncf(x * SK_Fixed3232_1) == static_cast<float>(result));
    return result;
}

#define SkScalarToFixed3232(x)    SkFloatToFixed3232(x)

#endif
