// Stub - TODO: Implement
// SDL.h must be included before main(): SDL2main expects SDL_main, and SDL.h
// does #define main SDL_main, satisfying the SDL2main entry-point wrapper at link time.
// The signature MUST be (int, char**): the macro rewrites this to SDL_main(int, char**),
// which matches SDL_main.h's `extern "C"` prototype and therefore links with C linkage.
// A no-arg main() would expand to an overloaded (C++-mangled) SDL_main() that SDL2main
// cannot find, yielding "undefined reference to SDL_main".
#include <SDL.h>
int main(int, char**) { return 0; }
