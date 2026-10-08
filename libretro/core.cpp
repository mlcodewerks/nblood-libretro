// NBlood libretro frontend. GPL-2.0-or-later.
#include <vector>
#include <string>
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "libretro.h"
#include "bridge.h"
#include "build.h"
#include "palette.h"
#include "baselayer.h"
#include "keyboard.h"
#include "control.h"
#ifndef LR_GAME_KENBUILD
#include "common_game.h"
#else
extern int g_useCwd;
#endif
#include "game_adapter.h"
#define CORE_OPTION(key) LIBRETRO_CORE_ID "_" key
#define MINICORO_IMPL
#include "minicoro.h"

namespace fs = std::filesystem;
static retro_environment_t environment;
static retro_video_refresh_t video;
static retro_audio_sample_t audio;
static retro_audio_sample_batch_t audioBatch;
static retro_input_poll_t inputPoll;
static retro_input_state_t inputState;
static retro_log_printf_t logger;
static retro_midi_interface midi;
static mco_coro *game;
static bool stopping, loaded, shutdownSent, padEnabled = true;
static uint64_t frameNumber;
static uint64_t midiTime, midiLastTime;
static unsigned width = 640, height = 480;
static unsigned optionWidth = 640, optionHeight = 480;
static bool freeaim = true, invert = false;
static float mouseSensitivity = 1, stickSensitivity = 1, deadzone = 0.15f;
static fs::path dataDir, saveDir, coreDir;
static std::vector<uint32_t> rgb;
static bool previousKeys[256];
static int previousMouse;
static uint64_t inputFrame = UINT64_MAX;
static const retro_variable options[] = {
    {CORE_OPTION("resolution"), "Software resolution (restart); 640x480|320x200|320x240|800x600|1024x768|1280x720|1920x1080"},
    {CORE_OPTION("freeaim"), "Free aim; enabled|disabled"},
    {CORE_OPTION("autoaim"), "Auto aim; disabled|enabled"},
    {CORE_OPTION("invert_y"), "Invert aim Y; disabled|enabled"},
    {CORE_OPTION("mouse_sensitivity"), "Mouse sensitivity; 1.0|0.5|1.5|2.0|3.0"},
    {CORE_OPTION("stick_sensitivity"), "Right stick sensitivity; 1.0|0.5|1.5|2.0|3.0"},
    {CORE_OPTION("deadzone"), "Analog deadzone; 15|0|5|10|20|25"},
    {nullptr, nullptr}
};
static const char *variable(const char *name, const char *fallback)
{
    retro_variable v{name, nullptr};
    return environment && environment(RETRO_ENVIRONMENT_GET_VARIABLE, &v) && v.value ? v.value : fallback;
}
static void readOptions()
{
    unsigned w=640,h=480;
    if (sscanf(variable(CORE_OPTION("resolution"), "640x480"), "%ux%u", &w, &h)==2 &&
        ((w==640 && h==480) || (w==320 && (h==200 || h==240)) || (w==800 && h==600) ||
         (w==1024 && h==768) || (w==1280 && h==720) || (w==1920 && h==1080))) {
        optionWidth=w; optionHeight=h;
    } else { optionWidth=640; optionHeight=480; }
    freeaim = strcmp(variable(CORE_OPTION("freeaim"), "enabled"), "disabled") != 0;
    invert = strcmp(variable(CORE_OPTION("invert_y"), "disabled"), "enabled") == 0;
    mouseSensitivity = strtof(variable(CORE_OPTION("mouse_sensitivity"), "1.0"), nullptr);
    stickSensitivity = strtof(variable(CORE_OPTION("stick_sensitivity"), "1.0"), nullptr);
    deadzone = strtof(variable(CORE_OPTION("deadzone"), "15"), nullptr) / 100;
    if (!std::isfinite(mouseSensitivity)) mouseSensitivity=1;
    if (!std::isfinite(stickSensitivity)) stickSensitivity=1;
    if (!std::isfinite(deadzone)) deadzone=.15f;
    mouseSensitivity=std::clamp(mouseSensitivity,.1f,5.f);
    stickSensitivity=std::clamp(stickSensitivity,.1f,5.f);
    deadzone=std::clamp(deadzone,0.f,.5f);
}
bool retro_freeaim_enabled() { return freeaim; }
static RetroGameSettings settings() { return {optionWidth,optionHeight,freeaim,strcmp(variable(CORE_OPTION("autoaim"),"disabled"),"enabled")==0}; }
void retro_apply_settings() { retro_game_apply(settings()); }
uint64_t retro_time_usec() { return frameNumber * 1000000ULL / 60; }
void retro_log_message(const char *s)
{
    if (logger) logger(RETRO_LOG_INFO, "%s\n", s);
    else fprintf(stderr, "%s\n", s);
}
void retro_yield()
{
    if (stopping) throw RetroExit{0};
    if (game && mco_running() == game) mco_yield(game);
    if (stopping) throw RetroExit{0};
    timerUpdateClock();
    retro_poll_build_input();
}
void retro_present(const uint8_t *pixels, unsigned w, unsigned h)
{
    width = w; height = h; rgb.resize(size_t(w) * h);
    uint32_t palette[256];
    // curpalettefaded includes Blood's brightness and damage/underwater fades.
    float gamma = std::max(0.1f, g_videoGamma), contrast = std::max(0.1f, g_videoContrast);
    for (unsigned i = 0; i < 256; ++i)
    {
        auto color = [=](unsigned char v) {
            float c = std::max(0.f, std::min(255.f, v * contrast - (contrast - 1.f) * 127.f));
            return unsigned(std::pow(c / 255.f, 1.f / gamma) * 255.f + 0.5f);
        };
        palette[i] = color(curpalettefaded[i].r) << 16 | color(curpalettefaded[i].g) << 8 | color(curpalettefaded[i].b);
    }
    for (size_t i = 0; i < rgb.size(); ++i) rgb[i] = palette[pixels[i]];
}
void retro_midi_write(uint8_t byte, uint32_t delta)
{
    if (midi.output_enabled && midi.write && midi.output_enabled()) {
        uint64_t now = std::max(midiTime, midiLastTime);
        if (midi.write(byte, delta + uint32_t(std::min(now-midiLastTime,uint64_t(UINT32_MAX))))) midiLastTime = now;
    }
}
void retro_midi_set_time(uint64_t usec) { midiTime = usec; }
static int16_t state(unsigned device, unsigned index, unsigned id)
{
    return inputState ? inputState(0, device, index, id) : 0;
}
struct Key { unsigned retro; unsigned scan; };
static const Key keys[] = {
    {RETROK_ESCAPE,1}, {RETROK_1,2}, {RETROK_2,3}, {RETROK_3,4}, {RETROK_4,5}, {RETROK_5,6}, {RETROK_6,7},
    {RETROK_7,8}, {RETROK_8,9}, {RETROK_9,10}, {RETROK_0,11}, {RETROK_MINUS,12}, {RETROK_EQUALS,13},
    {RETROK_BACKSPACE,14}, {RETROK_TAB,15}, {RETROK_q,16}, {RETROK_w,17}, {RETROK_e,18}, {RETROK_r,19},
    {RETROK_t,20}, {RETROK_y,21}, {RETROK_u,22}, {RETROK_i,23}, {RETROK_o,24}, {RETROK_p,25},
    {RETROK_LEFTBRACKET,26}, {RETROK_RIGHTBRACKET,27}, {RETROK_RETURN,28}, {RETROK_LCTRL,29},
    {RETROK_a,30}, {RETROK_s,31}, {RETROK_d,32}, {RETROK_f,33}, {RETROK_g,34}, {RETROK_h,35},
    {RETROK_j,36}, {RETROK_k,37}, {RETROK_l,38}, {RETROK_SEMICOLON,39}, {RETROK_QUOTE,40},
    {RETROK_BACKQUOTE,41}, {RETROK_LSHIFT,42}, {RETROK_BACKSLASH,43}, {RETROK_z,44}, {RETROK_x,45},
    {RETROK_c,46}, {RETROK_v,47}, {RETROK_b,48}, {RETROK_n,49}, {RETROK_m,50}, {RETROK_COMMA,51},
    {RETROK_PERIOD,52}, {RETROK_SLASH,53}, {RETROK_RSHIFT,54}, {RETROK_KP_MULTIPLY,55}, {RETROK_LALT,56},
    {RETROK_SPACE,57}, {RETROK_CAPSLOCK,58}, {RETROK_F1,59}, {RETROK_F2,60}, {RETROK_F3,61},
    {RETROK_F4,62}, {RETROK_F5,63}, {RETROK_F6,64}, {RETROK_F7,65}, {RETROK_F8,66}, {RETROK_F9,67},
    {RETROK_F10,68}, {RETROK_NUMLOCK,69}, {RETROK_SCROLLOCK,70}, {RETROK_KP7,71}, {RETROK_KP8,72},
    {RETROK_KP9,73}, {RETROK_KP_MINUS,74}, {RETROK_KP4,75}, {RETROK_KP5,76}, {RETROK_KP6,77},
    {RETROK_KP_PLUS,78}, {RETROK_KP1,79}, {RETROK_KP2,80}, {RETROK_KP3,81}, {RETROK_KP0,82},
    {RETROK_KP_PERIOD,83}, {RETROK_F11,87}, {RETROK_F12,88}, {RETROK_KP_ENTER,156}, {RETROK_RCTRL,157},
    {RETROK_KP_DIVIDE,181}, {RETROK_RALT,184}, {RETROK_PAUSE,197}, {RETROK_HOME,199}, {RETROK_UP,200},
    {RETROK_PAGEUP,201}, {RETROK_LEFT,203}, {RETROK_RIGHT,205}, {RETROK_END,207}, {RETROK_DOWN,208},
    {RETROK_PAGEDOWN,209}, {RETROK_INSERT,210}, {RETROK_DELETE,211}
};
static bool pad(unsigned id) { return padEnabled && state(RETRO_DEVICE_JOYPAD, 0, id); }
static float axis(unsigned stick, unsigned id)
{
    float v = padEnabled ? state(RETRO_DEVICE_ANALOG, stick, id) / 32768.f : 0;
    float a = std::fabs(v);
    return a <= deadzone ? 0 : std::copysign((a - deadzone) / (1 - deadzone), v);
}
void retro_poll_build_input()
{
    if (inputFrame == frameNumber) return;
    inputFrame = frameNumber;
    bool wanted[256] = {};
    for (auto const &key : keys) wanted[key.scan] = state(RETRO_DEVICE_KEYBOARD, 0, key.retro) != 0;
    bool menu = retro_game_menu_active();
    wanted[1] |= pad(RETRO_DEVICE_ID_JOYPAD_START);
    if (menu)
    {
        wanted[28] |= pad(RETRO_DEVICE_ID_JOYPAD_A);
        wanted[1] |= pad(RETRO_DEVICE_ID_JOYPAD_B);
        wanted[200] |= pad(RETRO_DEVICE_ID_JOYPAD_UP);
        wanted[208] |= pad(RETRO_DEVICE_ID_JOYPAD_DOWN);
        wanted[203] |= pad(RETRO_DEVICE_ID_JOYPAD_LEFT);
        wanted[205] |= pad(RETRO_DEVICE_ID_JOYPAD_RIGHT);
    }
    else
    {
        for (unsigned button=0; button<16; ++button) {
            unsigned scan=retro_game_pad_key(button);
            if (scan && scan!=255 && pad(button)) wanted[scan]=true;
        }
#if defined(LR_GAME_TEKWAR) || defined(LR_GAME_WITCHAVEN) || defined(LR_GAME_KENBUILD)
        // These older ports read movement keys instead of ControlInfo axes.
        const char *moves[]={"Move_Forward","Move_Backward","Strafe_Left","Strafe_Right"};
        const float values[]={-axis(0,1),axis(0,1),-axis(0,0),axis(0,0)};
        for (unsigned i=0;i<4;++i) {
            unsigned scan=retro_game_movement_key(moves[i]);
            if (scan && scan<255 && values[i]>.1f) wanted[scan]=true;
        }
#endif
    }
    for (unsigned i = 1; i < 256; ++i)
    {
        if (wanted[i] == previousKeys[i]) continue;
        keySetState(i, wanted[i]);
        if (keypresscallback) keypresscallback(i, wanted[i]);
        if (wanted[i])
        {
            char c = i < 128 ? (wanted[42] || wanted[54] ? g_keyAsciiTableShift[i] : g_keyAsciiTable[i]) : 0;
            if (i == 28 || i == 156) c = '\r';
            if (i == 14) c = '\b';
            if (c && !keyBufferFull()) keyBufferInsert(c);
        }
        previousKeys[i] = wanted[i];
    }
    int buttons = (state(RETRO_DEVICE_MOUSE,0,RETRO_DEVICE_ID_MOUSE_LEFT) ? 1 : 0)
        | (state(RETRO_DEVICE_MOUSE,0,RETRO_DEVICE_ID_MOUSE_RIGHT) ? 2 : 0)
        | (state(RETRO_DEVICE_MOUSE,0,RETRO_DEVICE_ID_MOUSE_MIDDLE) ? 4 : 0)
        | (state(RETRO_DEVICE_MOUSE,0,RETRO_DEVICE_ID_MOUSE_WHEELUP) ? 16 : 0)
        | (state(RETRO_DEVICE_MOUSE,0,RETRO_DEVICE_ID_MOUSE_WHEELDOWN) ? 32 : 0);
    g_mouseBits = buttons;
    g_mouseClickState = buttons & 1 ? (previousMouse & 1 ? MOUSE_HELD : MOUSE_PRESSED)
        : (previousMouse & 1 ? MOUSE_RELEASED : MOUSE_IDLE);
    for (int i = 0; i < 6; ++i)
        if (((buttons ^ previousMouse) & (1 << i)) && g_mouseCallback) g_mouseCallback(i+1, !!(buttons & (1<<i)));
    previousMouse = buttons;
    int mx = int(state(RETRO_DEVICE_MOUSE,0,RETRO_DEVICE_ID_MOUSE_X) * mouseSensitivity);
    int my = int(state(RETRO_DEVICE_MOUSE,0,RETRO_DEVICE_ID_MOUSE_Y) * mouseSensitivity);
    if (!menu) { mx += int(axis(RETRO_DEVICE_INDEX_ANALOG_RIGHT,0) * 18 * stickSensitivity); my += int(axis(RETRO_DEVICE_INDEX_ANALOG_RIGHT,1) * 18 * stickSensitivity); }
    if (invert) my = -my;
    g_mousePos.x += mx; g_mousePos.y += my;
    g_mouseAbs.x = std::max(0, std::min(xres - 1, g_mouseAbs.x + mx));
    g_mouseAbs.y = std::max(0, std::min(yres - 1, g_mouseAbs.y + my));
    retro_game_update(settings());
}
// Called after CONTROL_GetInput, retaining native mouse and keyboard binds.
void retro_analog_input(ControlInfo *info)
{
#ifdef LR_GAME_BLOOD
    constexpr int extent=1024;
#else
    constexpr int extent=32767;
#endif
    info->dz += int(axis(RETRO_DEVICE_INDEX_ANALOG_LEFT,1) * extent);
    info->dx += int(axis(RETRO_DEVICE_INDEX_ANALOG_LEFT,0) * extent);
}
struct WorkingDirectory
{
    fs::path old;
    WorkingDirectory() : old(fs::current_path()) { fs::current_path(saveDir); }
    ~WorkingDirectory() { std::error_code ec; fs::current_path(old, ec); }
};
static void entry(mco_coro *)
{
    try
    {
        static bool allocatorReady;
        if (!allocatorReady) { engineSetupAllocator(); allocatorReady = true; }
        initsystem();
        addsearchpath(dataDir.string().c_str());
        if (!coreDir.empty()) addsearchpath(coreDir.string().c_str());
        g_useCwd = 1;
        size_t argc; auto argv=retro_game_arguments(&argc);
        app_main(int(argc), argv);
    }
    catch (const RetroExit &) {}
    catch (const std::exception &e) { retro_log_message(e.what()); }
    try { retro_game_shutdown(); } catch (const RetroExit &) {}
    uninitsystem();
    uninitgroupfile();
    removesearchpath(dataDir.string().c_str());
    if (!coreDir.empty()) removesearchpath(coreDir.string().c_str());
}
extern "C" {
RETRO_API unsigned retro_api_version() { return RETRO_API_VERSION; }
RETRO_API void retro_set_environment(retro_environment_t cb)
{
    environment = cb; bool noGame = true;
    cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &noGame);
    cb(RETRO_ENVIRONMENT_SET_VARIABLES, const_cast<retro_variable *>(options));
    static const retro_controller_description devices[] = {{"RetroPad", RETRO_DEVICE_JOYPAD}, {"Keyboard and mouse", RETRO_DEVICE_NONE}};
    static const retro_controller_info ports[] = {{devices,2},{nullptr,0}};
    cb(RETRO_ENVIRONMENT_SET_CONTROLLER_INFO, const_cast<retro_controller_info *>(ports));
    static const retro_input_descriptor descriptors[] = {
        {0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_A,"Jump / Confirm"},
        {0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_B,"Crouch / Back"},
        {0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_X,"Use / Open"},
        {0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_Y,"Use inventory"},
        {0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_L,"Previous weapon"},
        {0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_R,"Next weapon"},
        {0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_L2,"Alternate fire"},
        {0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_R2,"Fire"},
        {0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_START,"Menu"},
        {0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_SELECT,"Map"},
        {0,RETRO_DEVICE_ANALOG,RETRO_DEVICE_INDEX_ANALOG_LEFT,0,"Strafe"},
        {0,RETRO_DEVICE_ANALOG,RETRO_DEVICE_INDEX_ANALOG_LEFT,1,"Move"},
        {0,RETRO_DEVICE_ANALOG,RETRO_DEVICE_INDEX_ANALOG_RIGHT,0,"Turn"},
        {0,RETRO_DEVICE_ANALOG,RETRO_DEVICE_INDEX_ANALOG_RIGHT,1,"Aim"},
        {0,0,0,0,nullptr}};
    cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, const_cast<retro_input_descriptor *>(descriptors));
}
RETRO_API void retro_set_video_refresh(retro_video_refresh_t cb) { video = cb; }
RETRO_API void retro_set_audio_sample(retro_audio_sample_t cb) { audio = cb; }
RETRO_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audioBatch = cb; }
RETRO_API void retro_set_input_poll(retro_input_poll_t cb) { inputPoll = cb; }
RETRO_API void retro_set_input_state(retro_input_state_t cb) { inputState = cb; }
RETRO_API void retro_init()
{
    retro_log_callback log{};
    if (environment(RETRO_ENVIRONMENT_GET_LOG_INTERFACE,&log)) logger = log.log;
    midi = {}; environment(RETRO_ENVIRONMENT_GET_MIDI_INTERFACE,&midi);
    const char *path = nullptr;
    std::error_code ec;
    coreDir.clear();
    if (environment(RETRO_ENVIRONMENT_GET_LIBRETRO_PATH, &path) && path && *path)
        coreDir = fs::absolute(path, ec).parent_path();
}
RETRO_API void retro_get_system_info(retro_system_info *info)
{
    *info = {}; info->library_name = retro_game_name(); info->library_version = "497385d6-libretro";
    info->valid_extensions = ""; info->need_fullpath = false; info->block_extract = false;
}
RETRO_API void retro_get_system_av_info(retro_system_av_info *info)
{
    *info = {{width,height,2560,1600, float(width)/height},{60.0,48000.0}};
    if (width == 320 && height == 200) info->geometry.aspect_ratio = 4.f/3.f;
}
RETRO_API bool retro_load_game(const retro_game_info *content)
{
    if (loaded || content) return false;
    const char *system = nullptr;
    if (!environment(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY,&system) || !system || !*system) { retro_log_message("Set a libretro system directory containing this game's data."); return false; }
    retro_pixel_format format = RETRO_PIXEL_FORMAT_XRGB8888;
    if (!environment(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT,&format)) return false;
    std::error_code ec;
    dataDir = fs::absolute(system,ec);
    // Prefer a game-specific system subdirectory, then the system root.
    for (const char *sub : {retro_game_directory(), ""})
    {
        fs::path candidate = dataDir / sub;
        if (!fs::is_directory(candidate,ec)) continue;
        bool found = false;
        for (auto const &file : fs::directory_iterator(candidate,ec))
        {
            std::string name = file.path().filename().string();
            std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c) { return std::tolower(c); });
            if (name == *retro_game_required_files()) found = true;
        }
        if (found) { dataDir = candidate; break; }
    }
    std::vector<std::string> names;
    if (fs::is_directory(dataDir,ec)) for (auto const &file : fs::directory_iterator(dataDir,ec))
    {
        std::string n = file.path().filename().string();
        std::transform(n.begin(),n.end(),n.begin(),[](unsigned char c) { return std::tolower(c); }); names.push_back(n);
    }
    for (auto required = retro_game_required_files(); *required; ++required)
        if (std::find(names.begin(),names.end(),*required) == names.end()) { std::string msg = std::string("Missing game data: ") + *required; retro_log_message(msg.c_str()); return false; }
    const char *save = nullptr;
    environment(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY,&save);
    saveDir = fs::absolute(save && *save ? save : system,ec) / retro_game_directory();
    if (ec) return false;
    fs::create_directories(saveDir,ec);
    if (ec) { retro_log_message("Cannot create game save directory."); return false; }
    readOptions(); width = optionWidth; height = optionHeight;
    rgb.assign(size_t(width)*height,0); frameNumber = 0; inputFrame = UINT64_MAX;
    std::memset(previousKeys,0,sizeof(previousKeys)); previousMouse = 0;
    stopping = false; shutdownSent = false; midiTime = midiLastTime = 0;
    auto desc = mco_desc_init(entry, 8*1024*1024);
    if (mco_create(&game,&desc) != MCO_SUCCESS) return false;
    loaded = true; return true;
}
RETRO_API void retro_run()
{
    if (!loaded) return;
    bool updated = false;
    if (environment(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE,&updated) && updated) readOptions();
    if (inputPoll) inputPoll();
    ++frameNumber;
    {
        WorkingDirectory cwd;
        retro_midi_set_time((frameNumber-1) * 1000000ULL / 60);
        if (mco_status(game) == MCO_SUSPENDED) mco_resume(game);
        int16_t samples[800*2];
        retro_midi_advance(unsigned(retro_time_usec() - (frameNumber-1)*1000000ULL/60));
        retro_audio_render(samples,800);
        if (audioBatch) audioBatch(samples,800);
        else if (audio) for (unsigned i = 0; i < 800; ++i) audio(samples[i*2],samples[i*2+1]);
    }
    retro_system_av_info av; retro_get_system_av_info(&av);
    static unsigned reportedWidth, reportedHeight;
    if (reportedWidth!=width || reportedHeight!=height) {
        environment(RETRO_ENVIRONMENT_SET_GEOMETRY,&av.geometry);
        reportedWidth=width; reportedHeight=height;
    }
    if (video) video(rgb.data(),width,height,width*sizeof(uint32_t));
    if (midi.flush && midi.output_enabled && midi.output_enabled()) midi.flush();
    if (mco_status(game) == MCO_DEAD && !shutdownSent) { shutdownSent = true; environment(RETRO_ENVIRONMENT_SHUTDOWN,nullptr); }
}
RETRO_API void retro_unload_game()
{
    if (!loaded) return;
    stopping = true;
    { WorkingDirectory cwd; if (mco_status(game) == MCO_SUSPENDED) mco_resume(game); }
    mco_destroy(game); game = nullptr; loaded = false; rgb.clear();
    for (int c = 0; c < 16; ++c) { retro_midi_write(0xb0|c); retro_midi_write(123); retro_midi_write(0); }
    if (midi.flush && midi.output_enabled && midi.output_enabled()) midi.flush();
}
RETRO_API void retro_deinit() { retro_unload_game(); }
RETRO_API void retro_set_controller_port_device(unsigned port, unsigned device) { if (!port) padEnabled = device != RETRO_DEVICE_NONE; }
RETRO_API void retro_reset()
{
    if (loaded) { WorkingDirectory cwd; retro_game_reset(); }
}
RETRO_API unsigned retro_get_region() { return RETRO_REGION_NTSC; }
RETRO_API size_t retro_serialize_size() { return 0; }
RETRO_API bool retro_serialize(void *, size_t) { return false; }
RETRO_API bool retro_unserialize(const void *, size_t) { return false; }
RETRO_API void retro_cheat_reset() {}
RETRO_API void retro_cheat_set(unsigned, bool, const char *) {}
RETRO_API bool retro_load_game_special(unsigned, const retro_game_info *, size_t) { return false; }
RETRO_API void *retro_get_memory_data(unsigned) { return nullptr; }
RETRO_API size_t retro_get_memory_size(unsigned) { return 0; }
}
