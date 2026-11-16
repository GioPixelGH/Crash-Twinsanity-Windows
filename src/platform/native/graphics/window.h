#pragma once

// The window the game's picture is shown in: the native platform makes it (SDL3) and hands it over at start-up; the graphics show
// each frame there through the window's backend (display.cpp, THE LIST 72): OpenGL 3.3 core on a context of the window's (made
// with SDL_WINDOW_OPENGL), or Vulkan through SDL's GPU API (made with SDL_WINDOW_VULKAN, when NativeGraphicsVulkanWanted says so).
// False when the window can't be drawn in that way (the platform makes an OpenGL one instead). Without a window the frames are kept
// to be read back (NativeGraphicsLastPicture, display.h)

struct SDL_Window;

bool NativeGraphicsAttachWindow(SDL_Window* window);
// Whether the window is to be Vulkan's ($TWIN_GPU_API=vulkan; THE LIST 73 makes it a setting), for a build that has it
bool NativeGraphicsVulkanWanted();
