#include "fastrenderer.h"

#include "ui/nativeui.h"

#include <cstdlib>
#include <cstring>

namespace NativeGraphics
{
namespace
{
// $TWIN_FAST_RENDERER: -1 unset (the setting's), else the mode it asks for
int AskedByVariable()
{
    static const int asked = [] {
        const char* value = std::getenv("TWIN_FAST_RENDERER");
        if (value == nullptr || value[0] == '\0')
        {
            return -1;
        }

        if (std::strcmp(value, "verify") == 0)
        {
            return static_cast<int>(FastMode::Verify);
        }

        return value[0] != '0' ? static_cast<int>(FastMode::Fast) : static_cast<int>(FastMode::Accurate);
    }();
    return asked;
}
}

FastMode FastRendererMode()
{
    const int asked = AskedByVariable();
    if (asked >= 0)
    {
        return static_cast<FastMode>(asked);
    }

    return NativeUi::GetOption(NativeUi::OptionFastRenderer) != 0 ? FastMode::Fast : FastMode::Accurate;
}

bool FastRendererHasKinds()
{
    // No kind of drawing has its prepared path yet (THE LIST 75 brings the scenery's)
    return false;
}
}

bool NativeGraphicsFastRendererShown()
{
    return NativeGraphics::FastRendererHasKinds();
}
