/**
 * Headless capture of the VESSEL SCREEN drawer to a PNG. Proves the off-screen fleet drawer opened, showing
 * its horizontal arrangement of vertical "control group" lists (each multi-item, with ship icons). Renders
 * the real pipeline offscreen + writes a PNG (deterministic — no window focus needed).
 *
 * Run from the project root: capture_vessel_screen [out.png] [W] [H]
 */

#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_syswm.h>
#include "BgfxRendererModule.h"
#include "UIModule.h"
#include "RHI/RHIDevice.h"
#include "RHI/RHITypes.h"
#include <grove/JsonDataNode.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <nlohmann/json.hpp>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>
#include <memory>

using namespace grove;
using nlohmann::json;

static void svpng(FILE* fp, unsigned w, unsigned h, const unsigned char* img, int alpha) {
    static const unsigned t[] = { 0,0x1db71064,0x3b6e20c8,0x26d930ac,0x76dc4190,0x6b6b51f4,0x4db26158,0x5005713c,
        0xedb88320,0xf00f9344,0xd6d6a3e8,0xcb61b38c,0x9b64c2b0,0x86d3d2d4,0xa00ae278,0xbdbdf21c };
    unsigned a = 1, b = 0, c, p = w * (alpha ? 4 : 3) + 1, x, y, i;
#define PUT(u) fputc(u, fp)
#define U8A(ua, l) for (i = 0; i < l; i++) PUT((ua)[i]);
#define U32(u) do { PUT((u)>>24); PUT(((u)>>16)&255); PUT(((u)>>8)&255); PUT((u)&255); } while(0)
#define U8C(u) do { PUT(u); c ^= (u); c = (c>>4)^t[c&15]; c = (c>>4)^t[c&15]; } while(0)
#define U8AC(ua, l) for (i = 0; i < l; i++) U8C((ua)[i])
#define U16LC(u) do { U8C((u)&255); U8C(((u)>>8)&255); } while(0)
#define U32C(u) do { U8C((u)>>24); U8C(((u)>>16)&255); U8C(((u)>>8)&255); U8C((u)&255); } while(0)
#define U8ADLER(u) do { U8C(u); a=(a+(u))%65521; b=(b+a)%65521; } while(0)
#define BEG(s, l) do { U32(l); c = ~0U; U8AC(s, 4); } while(0)
#define END() U32(~c)
    U8A("\x89PNG\r\n\32\n", 8); BEG("IHDR", 13); U32C(w); U32C(h); U8C(8); U8C(alpha?6:2); U8AC("\0\0\0",3); END();
    BEG("IDAT", 2 + h*(5+p) + 4); U8AC("\x78\1", 2);
    for (y=0; y<h; y++) { U8C(y==h-1); U16LC(p); U16LC(~p & 0xffff); U8ADLER(0);
        for (x=0; x<w*(alpha?4:3); x++, img++) U8ADLER(*img); }
    U32C((b<<16)|a); END(); BEG("IEND", 0); END();
}

int main(int argc, char** argv) {
    const std::string out = argc > 1 ? argv[1] : "vessel_capture.png";
    const int W = argc > 2 ? std::atoi(argv[2]) : 1280;
    const int H = argc > 3 ? std::atoi(argv[3]) : 720;
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { std::fprintf(stderr, "no SDL\n"); return 1; }
    SDL_Window* win = SDL_CreateWindow("cap", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H, SDL_WINDOW_HIDDEN);
    SDL_SysWMinfo wmi; SDL_VERSION(&wmi.version); SDL_GetWindowWMInfo(win, &wmi);

    auto& mgr = IntraIOManager::getInstance();
    auto rIO = mgr.createInstance("vs_r"); auto uIO = mgr.createInstance("vs_u"); auto gIO = mgr.createInstance("vs_g");

    auto renderer = std::make_unique<BgfxRendererModule>();
    { JsonDataNode c("config");
      c.setDouble("nativeWindowHandle", static_cast<double>(reinterpret_cast<uintptr_t>(wmi.info.win.window)));
      c.setInt("windowWidth", W); c.setInt("windowHeight", H); c.setBool("vsync", false);
      c.setString("texture1", "assets/textures/ship/cockpit.png");
      c.setString("texture2", "assets/textures/ship/reactor.png");
      c.setString("texture3", "assets/textures/ship/engine.png");
      c.setString("texture4", "assets/textures/ship/gun.png");
      renderer->setConfiguration(c, rIO.get(), nullptr); }

    auto ui = std::make_unique<UIModule>();
    { JsonDataNode c("config"); c.setString("layoutFile", "assets/ui/demo_vessel_screen.json");
      c.setInt("windowWidth", W); c.setInt("windowHeight", H); c.setInt("baseLayer", 1000);
      ui->setConfiguration(c, uIO.get(), nullptr); }

    auto frame = [&]{
        { auto cam = std::make_unique<JsonDataNode>("camera");
          cam->setDouble("x",0); cam->setDouble("y",0); cam->setDouble("zoom",1.0);
          cam->setInt("viewportX",0); cam->setInt("viewportY",0); cam->setInt("viewportW",W); cam->setInt("viewportH",H);
          gIO->publish("render:camera", std::move(cam)); }
        JsonDataNode in("input"); in.setDouble("deltaTime", 0.016); ui->process(in); renderer->process(in);
    };

    // Push the fleet: 3 control groups (5/4/3) stacked vertically; each a horizontal row of ship icons. Then
    // SHOW the top-left panel (in-process — deterministic, no focus needed).
    {
        const int sizes[3] = {5, 4, 3};
        const char* names[3] = {"Alpha", "Bravo", "Reserve"};
        json groups = json::array();
        json slots  = json::array();
        int idx = 0;
        for (int g = 0; g < 3; ++g) {
            groups.push_back({ {"name", names[g]}, {"ly", g * 64} });
            for (int k = 0; k < sizes[g]; ++k) {
                // ship0 = SELECTED (gold border), the rest at rest -> shows the selection highlight in the PNG.
                const char* border = (idx == 0) ? "0xffd166FF" : "0x2a3650FF";
                slots.push_back({ {"id","ship"+std::to_string(idx)}, {"name","Vaisseau "+std::to_string(idx)},
                                  {"border", border}, {"ix", k*46}, {"iy", g*64+20}, {"icon", 1+(idx%4)} });
                ++idx;
            }
        }
        gIO->publish("ui:data", std::make_unique<JsonDataNode>("d", json{ {"groups", groups}, {"slots", slots} }));
    }
    { auto d = std::make_unique<JsonDataNode>("d"); d->setString("id","fleetPanel"); d->setBool("visible", true);
      gIO->publish("ui:set_visible", std::move(d)); }

    // Simulate a fleet-icon click: open the inspector on a ship (push its blueprint + reveal the window), so
    // the PNG shows the WHOLE vessel screen — the fleet menu AND the opened inspector with its maquette.
    {
        auto part = [](const char* id,int x,int y,int w,int h,const char* col,int tex,const char* lbl,const char* st){
            return json{ {"id",id},{"x",x},{"y",y},{"w",w},{"h",h},{"color",col},{"tex",tex},{"label",lbl},{"stat",st} };
        };
        json parts = json::array({
            part("cockpit",160,10,80,80,  "0xFFFFFFFF",1,"Cockpit","PV 120  Equipage 2"),
            part("hullA",  140,90,120,44, "0x3a4a63FF",0,"Coque avant","PV 80"),
            part("gunL",   66,100,64,64,  "0xFFFFFFFF",4,"Canon babord","Degats 14"),
            part("gunR",   270,100,64,64, "0xFFFFFFFF",4,"Canon tribord","Degats 14"),
            part("hullM",  130,134,140,60,"0x46587aFF",0,"Coque centrale","PV 140"),
            part("reactor",160,190,80,80, "0xFFFFFFFF",2,"Reacteur","Energie +60  PV 90"),
            part("wingL",  40,196,70,44,  "0x2e3c54FF",0,"Aile babord","PV 50"),
            part("wingR",  290,196,70,44, "0x2e3c54FF",0,"Aile tribord","PV 50"),
            part("hullB",  140,272,120,44,"0x3a4a63FF",0,"Coque arriere","PV 80"),
            part("engL",   108,312,70,92, "0xFFFFFFFF",3,"Moteur babord","Poussee +35"),
            part("engR",   222,312,70,92, "0xFFFFFFFF",3,"Moteur tribord","Poussee +35")
        });
        gIO->publish("ui:data:merge", std::make_unique<JsonDataNode>("d", json{
            {"ship", {{"name","S.S. Aurora"},{"parts",parts}}},
            {"noPart", false}, {"selectedPart", {{"label","Reacteur"},{"stat","Energie +60  PV 90"}}} }));
        json inv = json::array();
        for (int i=0;i<50;++i){ char id[16]; std::snprintf(id,sizeof id,"r%02d",i);
            inv.push_back({ {"id",id},{"name","Res "+std::to_string(i+1)},{"icon",1+(i%4)},{"count",(i*37+12)%980+7},
                            {"cx",(i%4)*54+2},{"cy",(i/4)*54+2} }); }
        gIO->publish("ui:data:merge", std::make_unique<JsonDataNode>("d", json{ {"inventory", inv} }));
        { auto d = std::make_unique<JsonDataNode>("d"); d->setString("id","inspector"); d->setBool("visible", true);
          gIO->publish("ui:set_visible", std::move(d)); }
    }
    for (int i=0;i<8;i++) frame();

    rhi::IRHIDevice* dev = renderer->getDevice();
    if (!dev) { std::fprintf(stderr, "no device\n"); return 2; }
    rhi::FramebufferHandle fb = dev->createFramebuffer(static_cast<uint16_t>(W), static_cast<uint16_t>(H));
    dev->setViewFramebuffer(0, fb); dev->setViewFramebuffer(1, fb);
    frame(); frame();

    std::vector<uint8_t> rgba(static_cast<size_t>(W)*H*4, 0);
    if (!dev->readFramebuffer(fb, rgba.data(), static_cast<uint32_t>(rgba.size()))) { std::fprintf(stderr,"readback failed\n"); return 3; }
    std::vector<uint8_t> rgb(static_cast<size_t>(W)*H*3);
    for (size_t i=0;i<static_cast<size_t>(W)*H;++i){ rgb[i*3]=rgba[i*4]; rgb[i*3+1]=rgba[i*4+1]; rgb[i*3+2]=rgba[i*4+2]; }
    FILE* fp = std::fopen(out.c_str(), "wb"); if(!fp){ std::fprintf(stderr,"open fail\n"); return 4; }
    svpng(fp, W, H, rgb.data(), 0); std::fclose(fp);
    std::fprintf(stdout, "wrote %s\n", out.c_str());

    ui->shutdown(); renderer->shutdown();
    mgr.removeInstance("vs_r"); mgr.removeInstance("vs_u"); mgr.removeInstance("vs_g");
    SDL_DestroyWindow(win); SDL_Quit();
    return 0;
}
