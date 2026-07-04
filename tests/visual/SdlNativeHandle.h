#pragma once

/**
 * tests/visual/SdlNativeHandle — extract the native window + display handles from an SDL_Window, cross-platform.
 *
 * WHY  : bgfx needs the platform window handle (nwh) and, on X11, the display (ndt = Display*). The test/app
 *        mains used to read `wmi.info.win.window` directly — Windows-only, which is exactly what blocked the
 *        Linux build from RUNNING. Centralising the #ifdef here means every GPU main gets the right handles on
 *        Windows (HWND) AND Linux (X11 Window + Display*) from one place, instead of copying the platform switch.
 *
 * HOW  : SDL_GetWindowWMInfo -> the platform union. Win32: nwh = HWND, ndt = null. Linux/X11: nwh = the X11
 *        Window (an XID), ndt = the X11 Display*. Returns false if WM info is unavailable or the subsystem is
 *        one we don't handle (e.g. Wayland — SDL_VIDEODRIVER=x11 sidesteps that; Xvfb is X11).
 */

#include <SDL.h>
#include <SDL_syswm.h>

namespace grove {
namespace mvdemo {

// Fill *nwh (native window handle) and *ndt (native display type, X11 Display* — null on Windows) for `win`.
// Returns false if the handles could not be obtained (caller should fail-franc rather than pass garbage to bgfx).
inline bool getSdlNativeHandles(SDL_Window* win, void** nwh, void** ndt) {
    if (!win || !nwh || !ndt) return false;
    SDL_SysWMinfo wmi;
    SDL_VERSION(&wmi.version);
    if (!SDL_GetWindowWMInfo(win, &wmi)) return false;

#if defined(_WIN32)
    *nwh = reinterpret_cast<void*>(wmi.info.win.window);   // HWND
    *ndt = nullptr;                                        // Windows: no display handle
    return true;
#elif defined(SDL_VIDEO_DRIVER_X11)
    if (wmi.subsystem == SDL_SYSWM_X11) {
        *nwh = reinterpret_cast<void*>(static_cast<uintptr_t>(wmi.info.x11.window));  // X11 Window (XID)
        *ndt = reinterpret_cast<void*>(wmi.info.x11.display);                          // X11 Display*
        return true;
    }
    return false;   // non-X11 subsystem (e.g. Wayland) — run with SDL_VIDEODRIVER=x11
#else
    (void)wmi;
    return false;
#endif
}

} // namespace mvdemo
} // namespace grove
