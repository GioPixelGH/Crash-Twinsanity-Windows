#include "hardware.h"

#include "capture.h"
#include "renderthread.h"
#include "ee/dma.h"
#include "ee/gif.h"
#include "ee/rounding.h"
#include "ee/vif.h"
#include "ee/vu.h"
#include "ee/vu1translate.h"
#include "gs/gs.h"
#include "math/vu0.h"

#include <cfenv>

namespace NativeGraphics
{
Hardware& GetHardware()
{
    static Hardware hardware = [] {
        Hardware made;
        made.gs = new Gs::Gs();
        made.gif = new Ee::Gif(*made.gs);
        made.vu0 = new Ee::Vu(0);
        made.vu1 = new Ee::Vu(1);
        made.vif0 = new Ee::Vif(0, *made.vu0, nullptr);
        made.vif1 = new Ee::Vif(1, *made.vu1, made.gif);
        Ee::Vu* vu1 = made.vu1;
        Ee::Gif* gif = made.gif;
        // The GS's work rounds to nearest, as on its other paths and its worker threads (VU1's programs round toward zero); a
        // kick only copied for the render thread does no arithmetic
        made.vu1->xgkick = [vu1, gif](u32 address) {
            if (gif->forwarding() && !OnRenderThread())
            {
                gif->Kick(vu1->data(), address, vu1->kickPc * 8);
                return;
            }

            Ee::RoundToNearest rounding;
            gif->Kick(vu1->data(), address, vu1->kickPc * 8);
        };
        // VU1's programs run translated (ee/vu1translate.h)
        Ee::AttachVu1Translation(*made.vu1);
        // VU0's microprogram (the decals') too
        Ee::AttachVu0Translation(*made.vu0);
        // The maths' VU0 reads the data the graphics put in VU0's memory (the particles' views)
        NativeMath::SetVu0DataMemory(made.vu0->data());
        return made;
    }();
    return hardware;
}

void SendVif0Chain(const void* chain)
{
    Hardware& hardware = GetHardware();
    Ee::RunDmaChain(static_cast<const u8*>(chain), true,
                    [&](const u32* words, u32 count) { hardware.vif0->Process(words, count); });
}

void SendVif1Chain(const void* chain)
{
    Hardware& hardware = GetHardware();
    const bool queued = RenderThreadRunsVu1();
    Ee::RunDmaChain(static_cast<const u8*>(chain), true, [&](const u32* words, u32 count) {
        CaptureVif1(words, count);
        if (queued)
        {
            RenderQueueVif1(words, count);
        }
        else
        {
            hardware.vif1->Process(words, count);
        }
    });
    // What VU1 and VIF1 sent the GIF, to the render thread now
    RenderFlush();
}

void SendGifChain(const void* chain)
{
    Hardware& hardware = GetHardware();
    Ee::RunDmaChain(static_cast<const u8*>(chain), false, [&](const u32* words, u32 count) {
        CaptureGif(words, count);
        hardware.gif->Transfer(2, reinterpret_cast<const u8*>(words), count / 4);
    });
    RenderFlush();
}
}
