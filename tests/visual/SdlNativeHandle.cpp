/**
 * SdlNativeHandle.cpp — the ONLY TU that includes <SDL_syswm.h>. See SdlNativeHandle.h for why this is isolated:
 * on Linux SDL_syswm.h pulls the X11 macros (None/Status/BlendMode…) that collide with the renderer's RHITypes.h.
 * Confining it to this .cpp lets a main call getSdlNativeHandles() without that pollution reaching its includes.
 */

#include "SdlNativeHandle.h"

#include <SDL_syswm.h>   // X11 macro pollution stops HERE
#include <cstdint>

namespace grove {
namespace mvdemo {

bool getSdlNativeHandles(SDL_Window* win, void** nwh, void** ndt) {
    if (!win || !nwh || !ndt) return false;
    SDL_SysWMinfo wmi;
    SDL_VERSION(&wmi.version);
    if (!SDL_GetWindowWMInfo(win, &wmi)) return false;

#if defined(SDL_VIDEO_DRIVER_WINDOWS)
    if (wmi.subsystem == SDL_SYSWM_WINDOWS) {
        *nwh = reinterpret_cast<void*>(wmi.info.win.window);   // HWND
        *ndt = nullptr;                                        // Windows: no display handle
        return true;
    }
#endif
#if defined(SDL_VIDEO_DRIVER_X11)
    if (wmi.subsystem == SDL_SYSWM_X11) {
        *nwh = reinterpret_cast<void*>(static_cast<uintptr_t>(wmi.info.x11.window));  // X11 Window (XID)
        *ndt = reinterpret_cast<void*>(wmi.info.x11.display);                          // X11 Display*
        return true;
    }
#endif
    return false;   // unsupported subsystem (e.g. Wayland) — run with SDL_VIDEODRIVER=x11
}

} // namespace mvdemo
} // namespace grove
