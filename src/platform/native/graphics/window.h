#pragma once

// The window the game's picture is shown in: the native platform makes it (SDL3) and hands it over at start-up; the graphics show
// each frame there through the window's backend (display.cpp, THE LIST 72): OpenGL 3.3 core on a context of the window's (made
// with SDL_WINDOW_OPENGL), or Vulkan through SDL's GPU API (made with SDL_WINDOW_VULKAN, when NativeGraphicsVulkanWanted says so).
// False when the window can't be drawn in that way (the platform makes an OpenGL one instead). Without a window the frames are kept
// to be read back (NativeGraphicsLastPicture, display.h)

struct SDL_Window;

bool NativeGraphicsAttachWindow(SDL_Window* window);
// The graphics API (THE LIST 73, graphicsapi.cpp): whether the window and the renderer are to be Vulkan's, decided once at the
// first call: the setting's (options.txt's graphics_api), $TWIN_GPU_API over it (opengl or vulkan: a test's), the other API when
// the last start with this one never showed its pictures (graphics_start.txt left in the settings folder, the setting changed);
// OpenGL in a build without Vulkan
bool NativeGraphicsVulkanWanted();
bool NativeGraphicsVulkanBuilt();
// The window about to be made with an API: graphics_start.txt names it until the start's pictures show (not with $TWIN_GPU_API)
void NativeGraphicsStartingWith(bool vulkan);
// The window couldn't show the game with the API asked for, so the other's taken: the setting changed to it (not with
// $TWIN_GPU_API)
void NativeGraphicsStartFellBack(bool vulkan);
// A picture shown in the window: enough of them and the start worked (graphics_start.txt removed)
void NativeGraphicsPictureShown();
// The session's end in order (sessionlog.cpp: the window closed, a fatal error's message, the exit): graphics_start.txt removed; a
// crash or a killed program leaves it
void NativeGraphicsStartEnded();
