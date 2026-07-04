#pragma once

/**
 * tests/visual/SdlNativeHandle — extract the native window + display handles from an SDL_Window, cross-platform.
 *
 * WHY  : bgfx needs the platform window handle (nwh) and, on X11, the display (ndt = Display*). The old mains
 *        read `wmi.info.win.window` directly — Windows-only, which blocked the Linux build from RUNNING.
 *
 * ⚠ ISOLATION: the implementation (SdlNativeHandle.cpp) is the ONLY translation unit that includes
 *   <SDL_syswm.h>. On Linux that header drags in the X11 macros (None, Status, BlendMode→KBLedMode, …) which
 *   COLLIDE with the renderer's RHITypes.h enums. Keeping SDL_syswm.h out of this HEADER means a main can use
 *   the handles without polluting its other includes — this header pulls only <SDL.h> (X11-macro-free).
 */

#include <SDL.h>   // SDL_Window (no X11 macros — that is SDL_syswm.h, confined to the .cpp)

namespace grove {
namespace mvdemo {

// Fill *nwh (native window handle) and *ndt (native display type — X11 Display* on Linux, null on Windows) for
// `win`. Returns false if the handles are unavailable / the subsystem is unsupported (Wayland → use x11 driver).
bool getSdlNativeHandles(SDL_Window* win, void** nwh, void** ndt);

} // namespace mvdemo
} // namespace grove
