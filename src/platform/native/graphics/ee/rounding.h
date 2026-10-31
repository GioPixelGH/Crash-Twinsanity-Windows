#pragma once

// The VU's arithmetic rounds toward zero: the mode set for a run (a VU program, a macro instruction) and the one before put back
// after it. On x86-64 only SSE's rounding bits (MXCSR's) are set, where the floats are computed: the C library's
// fegetround/fesetround also read and set the x87's control word, through MinGW's __mingw_setfp, which cost VU1's thread 4% of
// its time at each program's start and end
#include <cfenv>
#if defined(__x86_64__) || defined(_M_X64)
#include <xmmintrin.h>
#endif

namespace Ee
{
class RoundTowardZero
{
public:
#if defined(__x86_64__) || defined(_M_X64)
    RoundTowardZero() : saved_(_mm_getcsr()) { _mm_setcsr(saved_ | 0x6000u); }
    ~RoundTowardZero() { _mm_setcsr(saved_); }

private:
    unsigned int saved_;
#else
    RoundTowardZero() : saved_(std::fegetround()) { std::fesetround(FE_TOWARDZERO); }
    ~RoundTowardZero() { std::fesetround(saved_); }

private:
    int saved_;
#endif

public:
    RoundTowardZero(const RoundTowardZero&) = delete;
    RoundTowardZero& operator=(const RoundTowardZero&) = delete;
};

// The GS's work rounds to nearest (a kick made inside a VU1 program's run): the same, the other way
class RoundToNearest
{
public:
#if defined(__x86_64__) || defined(_M_X64)
    RoundToNearest() : saved_(_mm_getcsr()) { _mm_setcsr(saved_ & ~0x6000u); }
    ~RoundToNearest() { _mm_setcsr(saved_); }

private:
    unsigned int saved_;
#else
    RoundToNearest() : saved_(std::fegetround()) { std::fesetround(FE_TONEAREST); }
    ~RoundToNearest() { std::fesetround(saved_); }

private:
    int saved_;
#endif

public:
    RoundToNearest(const RoundToNearest&) = delete;
    RoundToNearest& operator=(const RoundToNearest&) = delete;
};
}
