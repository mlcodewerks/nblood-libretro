// Headless frontend exercising the public ABI with real Blood data.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <filesystem>
#include <vector>
#include <set>
#include <string>
#include <map>
#include "libretro.h"
#include "state.h"
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif
namespace fs = std::filesystem;
static std::string systemDir, saveDir, corePath, gameId="nblood", gameName="NBlood";
static std::map<std::string,std::string> variables;
static bool noMidi, keyboard, missing, noGame, shutdownRequested, variableUpdate;
static int stage, stageFrame, frame;
static size_t videos, pcmFrames, audible, midiBytes, midiTimed, midiNotes, flushes;
static unsigned videoWidth, videoHeight;
static std::vector<uint32_t> lastVideo;
static std::set<unsigned long long> hashes;
static void fail(const char *why) { fprintf(stderr,"FAIL: %s\n",why); std::exit(1); }
static void check(bool result, const char *why) { if (!result) fail(why); }
static void logMessage(enum retro_log_level, const char *fmt, ...) {
    va_list ap; va_start(ap,fmt); vfprintf(stderr,fmt,ap); va_end(ap);
}
static bool midiEnabled() { return true; }
static bool midiDisabled() { return false; }
static bool midiRead(uint8_t *) { return false; }
static bool midiWrite(uint8_t byte, uint32_t delta) {
    ++midiBytes; if (delta) ++midiTimed;
    if ((byte & 0xf0) == 0x90) ++midiNotes;
    return true;
}
static bool midiFlush() { ++flushes; return true; }
static bool env(unsigned cmd, void *data) {
    switch(cmd) {
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME: noGame = *static_cast<bool*>(data); return true;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY: *static_cast<const char**>(data) = systemDir.c_str(); return true;
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: *static_cast<const char**>(data) = saveDir.c_str(); return true;
    case RETRO_ENVIRONMENT_GET_LIBRETRO_PATH: *static_cast<const char**>(data) = corePath.c_str(); return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: return *static_cast<retro_pixel_format*>(data) == RETRO_PIXEL_FORMAT_XRGB8888;
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: static_cast<retro_log_callback*>(data)->log = logMessage; return true;
    case RETRO_ENVIRONMENT_GET_MIDI_INTERFACE:
        if (noMidi) return false;
        *static_cast<retro_midi_interface*>(data) = {midiDisabled,midiEnabled,midiRead,midiWrite,midiFlush}; return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        auto *v = static_cast<retro_variable*>(data); auto found = variables.find(v->key);
        if (found == variables.end()) return false;
        v->value = found->second.c_str(); return true;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE: *static_cast<bool*>(data) = variableUpdate; variableUpdate = false; return true;
    case RETRO_ENVIRONMENT_SHUTDOWN: shutdownRequested = true; return true;
    case RETRO_ENVIRONMENT_SET_VARIABLES:
    case RETRO_ENVIRONMENT_SET_GEOMETRY:
    case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS: return true;
    default: return false;
    }
}
static void video(const void *pixels, unsigned w, unsigned h, size_t pitch) {
    check(pixels != nullptr,"frame buffer is null"); check(pitch == w*4,"incorrect XRGB8888 pitch");
    ++videos; videoWidth = w; videoHeight = h;
    auto p = static_cast<const uint32_t*>(pixels); lastVideo.assign(p,p+size_t(w)*h);
    unsigned long long hash = 1469598103934665603ULL;
    for (size_t i=0; i<size_t(w)*h; ++i) { hash ^= p[i]; hash *= 1099511628211ULL; }
    hashes.insert(hash);
}
static size_t audio(const int16_t *data, size_t count) {
    pcmFrames += count; for(size_t i=0;i<count*2;i++) if(data[i]) ++audible; return count;
}
static void audioSample(int16_t l,int16_t r) { int16_t samples[] = {l,r}; audio(samples,1); }
static void poll() {}
static int16_t input(unsigned, unsigned device, unsigned index, unsigned id) {
    bool confirm = stage == 0 && (stageFrame == 50 || stageFrame == 90 || stageFrame == 130 || stageFrame == 170);
    if (device == RETRO_DEVICE_KEYBOARD) {
        if (keyboard && id == RETROK_RETURN && confirm) return 1;
        if (stage == 1 && id == RETROK_w && stageFrame < 40) return 1;
        if (stage == 5) {
            if (id == RETROK_F2 && stageFrame == 1) return 1;
            if (id == RETROK_RETURN && (stageFrame == 15 || stageFrame == 45)) return 1;
            if (id == RETROK_t && (stageFrame == 25 || stageFrame == 31)) return 1;
            if (id == RETROK_e && stageFrame == 27) return 1;
            if (id == RETROK_s && stageFrame == 29) return 1;
        }
        if (stage == 6 && ((id == RETROK_F3 && stageFrame == 1) || (id == RETROK_RETURN && stageFrame == 15))) return 1;
        return 0;
    }
    if (device == RETRO_DEVICE_JOYPAD) {
        if (!keyboard && id == RETRO_DEVICE_ID_JOYPAD_A && confirm) return 1;
        if (stage == 4 && id == RETRO_DEVICE_ID_JOYPAD_R2 && stageFrame < 30) return 1;
        return 0;
    }
    if (device == RETRO_DEVICE_MOUSE) {
        if(stage == 2 && id == RETRO_DEVICE_ID_MOUSE_X && stageFrame < 20) return 4;
        if(stage == 2 && id == RETRO_DEVICE_ID_MOUSE_Y && stageFrame < 20) return -3;
        return 0;
    }
    if (device == RETRO_DEVICE_ANALOG && stage == 3 && stageFrame < 40) {
        if(index == RETRO_DEVICE_INDEX_ANALOG_LEFT && id == RETRO_DEVICE_ID_ANALOG_Y) return -24000;
        if(index == RETRO_DEVICE_INDEX_ANALOG_RIGHT && id == RETRO_DEVICE_ID_ANALOG_X) return 12000;
        if(index == RETRO_DEVICE_INDEX_ANALOG_RIGHT && id == RETRO_DEVICE_ID_ANALOG_Y) return 9000;
    }
    return 0;
}
template<typename T> static T symbol(void *lib, const char *name) {
#ifdef _WIN32
    auto result = GetProcAddress(static_cast<HMODULE>(lib),name);
#else
    auto result = dlsym(lib,name);
#endif
    if (!result) fail(name); return reinterpret_cast<T>(result);
}
static void screenshot(const char *name) {
    auto path = fs::path(saveDir)/name; FILE *out = fopen(path.string().c_str(),"wb"); check(out != nullptr,"screenshot file");
    auto u16 = [&](uint16_t x) { unsigned char b[] = {uint8_t(x), uint8_t(x>>8)}; fwrite(b,1,2,out); };
    auto u32 = [&](uint32_t x) { unsigned char b[] = {uint8_t(x), uint8_t(x>>8), uint8_t(x>>16), uint8_t(x>>24)}; fwrite(b,1,4,out); };
    fwrite("BM",1,2,out); u32(54+videoWidth*videoHeight*4); u32(0); u32(54);
    u32(40); u32(videoWidth); u32(-int32_t(videoHeight)); u16(1); u16(32); u32(0);
    u32(videoWidth*videoHeight*4); u32(0); u32(0); u32(0); u32(0);
    fwrite(lastVideo.data(),4,lastVideo.size(),out);
    fclose(out);
}
int main(int argc, char **argv) {
    if(argc < 4) { fprintf(stderr,"Usage: libretro_smoke CORE SYSTEM SAVE [--keyboard|--no-midi|--missing|--reload]\n"); return 2; }
    bool reload = false;
    for(int i=4;i<argc;i++) {
        noMidi |= !strcmp(argv[i],"--no-midi"); keyboard |= !strcmp(argv[i],"--keyboard"); missing |= !strcmp(argv[i],"--missing"); reload |= !strcmp(argv[i],"--reload");
        if (!strcmp(argv[i],"--game") && i+1<argc) { gameId=argv[++i]; gameName=gameId=="voidsw"?"VoidSW":"EDuke32"; }
        if (!strcmp(argv[i],"--resolution") && i+1<argc) variables["nblood_resolution"]=argv[++i];
    }
    systemDir = fs::absolute(argv[2]).string(); saveDir = fs::absolute(argv[3]).string(); fs::create_directories(saveDir);
    corePath = fs::absolute(argv[1]).string();
#ifdef _WIN32
    void *lib = LoadLibraryA(fs::absolute(argv[1]).string().c_str());
    if(!lib) { fprintf(stderr,"LoadLibrary error %lu\n",GetLastError()); return 1; }
#else
    void *lib = dlopen(argv[1],RTLD_NOW|RTLD_LOCAL); if(!lib) { fprintf(stderr,"%s\n",dlerror()); return 1; }
#endif
#define API(name) auto name = symbol<decltype(&::name)>(lib,#name)
    API(retro_api_version); API(retro_set_environment); API(retro_init); API(retro_deinit); API(retro_load_game);
    API(retro_unload_game); API(retro_run); API(retro_get_system_info); API(retro_get_system_av_info); API(retro_reset);
    API(retro_set_video_refresh); API(retro_set_audio_sample); API(retro_set_audio_sample_batch);
    API(retro_set_input_poll); API(retro_set_input_state); API(retro_set_controller_port_device);
    API(retro_serialize_size); API(retro_serialize); API(retro_unserialize); API(retro_cheat_reset); API(retro_cheat_set);
    API(retro_load_game_special); API(retro_get_memory_data); API(retro_get_memory_size); API(retro_get_region);
    void (*snapshot)(BloodTestState*) = nullptr;
    check(retro_api_version() == 1,"ABI version"); retro_set_environment(env); check(noGame,"contentless flag");
    retro_set_video_refresh(video); retro_set_audio_sample(audioSample); retro_set_audio_sample_batch(audio);
    retro_set_input_poll(poll); retro_set_input_state(input); retro_init();
    retro_system_info info{}; retro_get_system_info(&info); check(info.library_name && *info.library_name,"name");
    retro_game_info unwanted{}; check(!retro_load_game(&unwanted),"core must reject supplied content");
    if (missing) { check(!retro_load_game(nullptr),"missing data accepted"); retro_deinit(); puts("PASS: missing data rejected without terminating frontend"); return 0; }
    snapshot = symbol<void(*)(BloodTestState*)>(lib,"nblood_test_state");
    for (auto it=variables.begin(); it!=variables.end();) {
        if(it->first.rfind("nblood_",0)==0 && gameId!="nblood") { auto key=gameId+it->first.substr(6); auto value=it->second; it=variables.erase(it); variables[key]=value; } else ++it;
    }
    check(retro_load_game(nullptr),"contentless load");
    auto originalCwd = fs::current_path(); BloodTestState s{};
    auto run = [&]() { ++frame; ++stageFrame; retro_run(); check(fs::current_path()==originalCwd,"core changed frontend working directory"); check(!shutdownRequested,"unexpected core shutdown"); snapshot(&s); };
    auto boot = [&]() {
        stage=0; stageFrame=0;
        for(int i=0;i<4000;i++) {
            run();
            if (s.started && !s.menu && i>=230) break;
            if(gameId!="nblood" && stageFrame%40==0 && stageFrame>180) stageFrame=49;
        }
        check(s.started && !s.menu,"reloaded core could not start a new game");
    };
    for(int i=0;i<4000;i++) {
        run();
        if (s.started && !s.menu && i>=230) break;
        if(gameId!="nblood" && stageFrame%40==0 && stageFrame>180) stageFrame=49;
    }
    screenshot("level-start.bmp");
    printf("After menu: started=%d mode=%d clock=%d level=%d\n",s.started,s.menu,s.clock,s.level);
    check(s.started,"joypad/keyboard menu did not start a game"); check(s.freeaim==1 && s.autoaim==0,"free aim defaults");
    BloodTestState initial=s;
    stage=1; stageFrame=0; for(int i=0;i<60;i++) run();
    check(s.x!=initial.x || s.y!=initial.y,"keyboard movement did not reach player");
    initial=s; stage=2; stageFrame=0; for(int i=0;i<35;i++) run();
    printf("Mouse: yaw %d -> %d, pitch %d -> %d, aim %d\n",initial.angle,s.angle,initial.pitch,s.pitch,s.freeaim);
    check(s.angle!=initial.angle && s.pitch!=initial.pitch,"mouse free aim did not change yaw and pitch");
    initial=s; stage=3; stageFrame=0; for(int i=0;i<60;i++) run();
    check(s.x!=initial.x || s.y!=initial.y,"left analog stick did not move player");
    check(s.angle!=initial.angle && s.pitch!=initial.pitch,"right analog stick did not aim");
    stage=4; stageFrame=0; for(int i=0;i<60;i++) run(); screenshot("gameplay.bmp");
    if (gameId=="nblood") {
    stage=5; stageFrame=0; for(int i=0;i<90;i++) run();
    check(fs::is_regular_file(fs::path(saveDir)/"nblood/game0000.sav"),"native save was not written to frontend save directory");
    initial=s; stage=1; stageFrame=0; for(int i=0;i<60;i++) run();
    check(s.x!=initial.x || s.y!=initial.y,"movement before loading save");
    stage=6; stageFrame=0; for(int i=0;i<90;i++) run();
    check(std::abs(s.x-initial.x)<512 && std::abs(s.y-initial.y)<512,"native load did not restore player position");
    }
    variables[gameId+"_freeaim"]="disabled"; variables[gameId+"_autoaim"]="enabled"; variableUpdate=true;
    for(int i=0;i<4;i++) run(); check(s.freeaim==0 && s.autoaim==1,"live core option updates");
    check(videos==size_t(frame),"video callback count"); check(pcmFrames==size_t(frame)*800,"48 kHz PCM count");
    check(audible>0,"all sound effects are silent"); check(hashes.size()>20,"software rendering did not change");
    if(!noMidi && gameId!="voidsw") check(midiNotes>0 && flushes>0 && midiTimed>0,"no timed MIDI note output or flush");
    else if(noMidi) check(midiBytes==0,"MIDI called with no frontend interface");
    if(gameId=="nblood") { retro_reset(); for(int i=0;i<90;i++) run(); check(s.started && s.level==0,"reset did not restart level"); }
    retro_unload_game();
    if(reload) {
        for (int session=0; session<3; ++session) {
            stage=0; stageFrame=0;
            variables[gameId+"_freeaim"]="enabled"; variables[gameId+"_autoaim"]="disabled";
            check(retro_load_game(nullptr),"reload rejected");
            boot();
            check(s.started && s.freeaim==1,"reloaded core could not start a new game");
            if(gameId=="nblood") {
            stage=6; stageFrame=0; for(int i=0;i<90;i++) run();
            check(s.started && s.health>0,"save load after reopening core");
            }
            retro_unload_game();
        }
        retro_deinit(); retro_init();
        check(retro_load_game(nullptr),"load after deinit/init rejected");
        stage=0; stageFrame=0; run(); run(); retro_unload_game();
        check(retro_load_game(nullptr),"load after early unload rejected");
        boot(); check(s.started,"restart after early unload"); retro_unload_game();
    }
    retro_deinit();
    printf("PASS: %zu video frames, %zu unique frames, %zu PCM frames, %zu nonzero samples, %zu MIDI bytes, %zu notes, %zu timed bytes\n",videos,hashes.size(),pcmFrames,audible,midiBytes,midiNotes,midiTimed);
    return 0;
}
