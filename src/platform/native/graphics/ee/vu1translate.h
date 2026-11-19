#pragma once

// VU1's programs run as C++ translated from the game's microcode at build time (native/tools/vu1_translate.py; native/GRAPHICS.md,
// "VU1 translation"), the interpreter (vu.cpp) running whatever has no translation. The results are the interpreter's, bit for
// bit: TWIN_VU1 chooses
//
//     (unset)      translated
//     interpret    the interpreter alone
//     verify       each program run twice, by the interpreter on a copy and translated, and compared: every XGKICK's packet
//                  (VU1's data memory) and registers, and at the program's end the registers, flags and memory
//
// A program's translation is used where micro memory holds its code (checked whenever the code changes), wherever it was
// uploaded to.

#include "common.h"

namespace Ee
{
class Vu;

// Makes VU1 run its programs translated (or as TWIN_VU1 says)
void AttachVu1Translation(Vu& vu1);
// Makes VU0 run its microprograms (the decal program) translated, the same way (TWIN_VU0: the same choices)
void AttachVu0Translation(Vu& vu0);

// Statistics: the instructions run translated, and (verify) the runs and kicks compared and the differences found
struct Vu1TranslationStats
{
    u64 translatedInstructions = 0;
    u64 interpretedInstructions = 0;
    u64 entries = 0;
    u64 verifiedRuns = 0;
    u64 verifiedKicks = 0;
    u64 differences = 0;
};
const Vu1TranslationStats& GetVu1TranslationStats();

// The translated VU1 program micro memory holds at an instruction's address, as the dispatcher last told it (the one running,
// while it runs), or -1 (none told: TWIN_VU1=interpret, or code no translation has); and a program's address in the retail
// executable's .vutext (0 for none). VU1's thread's (the VU stage's with three threads)
s32 Vu1ProgramAt(u32 pc);
// The translated program where VU1's run now (its MSCAL or MSCNT) started, or -1
s32 Vu1RunEntryProgram();
u32 Vu1ProgramAddress(s32 program);
}
