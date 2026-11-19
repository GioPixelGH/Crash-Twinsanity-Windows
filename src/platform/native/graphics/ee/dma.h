#pragma once

// The DMA controller's source chain mode, as the renderer's channels use it: tags (CNT, NEXT, REF, REFS, REFE, CALL, RET, END)
// that send quadwords after them or at their address and say where the next tag is, with a two level call stack. VIF1's
// channel sends each tag's second half to the VIF first (TTE), the GIF's channel only the data.
//
// The chains' addresses are the renderer's 32 bit DMA addresses (address.h), turned back into host memory here.

#include "common.h"

#include <functional>

namespace Ee
{
// A chain started at its first tag: each piece (the tag's VIF words when tagWords, then its data) goes to the sink
void RunDmaChain(const u8* firstTag, bool tagWords, const std::function<void(const u32* words, u32 count)>& sink);
// The same, with each tag shown to onTag before its pieces go to the sink (THE LIST 74's split by kind: where a bucket starts)
void RunDmaChain(const u8* firstTag, bool tagWords, const std::function<void(const u32* words, u32 count)>& sink,
                 const std::function<void(const u8* tag)>& onTag);
}
