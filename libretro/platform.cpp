// Libretro platform services for the Build software renderer. GPL-2.0-or-later.
#include "bridge.h"
#include "build.h"
#include "palette.h"
#include "baselayer.h"
#include "mutex.h"
#include <mutex>
#include <vector>
#include <cstdio>
#include <cstdarg>

char quitevent = 0, appactive = 1, modechange = 1, offscreenrendering = 0, videomodereset = 0;
int32_t xres = 640, yres = 480, bpp = 8, fullscreen = 0, bytesperline = 640;
intptr_t frameplace = 0;
int32_t lockcount = 0, nofog = 0, maxrefreshfreq = 60, inputchecked = 0;
double refreshfreq = 60.0;
bool g_ImGuiCaptureInput = false, g_ImGuiFrameActive = false;
uint8_t g_ImGuiCapturedDevices = 0;
extern "C" {
const char *s_buildRev = "497385d6-libretro";
const char *s_buildTimestamp = __DATE__ " " __TIME__;
}
static std::vector<uint8_t> framebuffer;
static int32_t axes[4];
static int32_t hat = -1;
static int clockRate = 120;
static uint64_t clockSample;
static void (*clockCallback)();

void eduke32_exit_return(int status) { throw RetroExit{status}; }
int32_t initsystem()
{
    r_maxfps = -2; quitevent = 0; appactive = 1;
    totalclock = 0; clockSample = retro_time_usec() * clockRate / 1000000; clockCallback = nullptr;
    memset(keystatus,0,sizeof(keystatus));
    g_mousePos = {0,0}; g_mouseBits = 0;
    return 0;
}
void uninitsystem() { framebuffer.clear(); frameplace = 0; }
void system_getcvars() {}
void engineBeginImGuiFrame() {}
void engineEndImGuiInput() {}
void engineBeginImGuiInput() {}
int32_t startwin_open() { return 0; }
int32_t startwin_close() { return 0; }
int32_t startwin_puts(const char *s) { retro_log_message(s); return 0; }
int32_t startwin_settitle(const char *) { return 0; }
int32_t startwin_idle(void *) { return 0; }
int32_t startwin_run() { return 1; }
bool startwin_isopen() { return true; }
void wm_setapptitle(const char *) {}
int debugprintf(const char *fmt, ...)
{
    char buf[1536]; va_list ap; va_start(ap,fmt); int n=vsnprintf(buf,sizeof(buf),fmt,ap); va_end(ap);
    retro_log_message(buf); return n;
}
int32_t wm_msgbox(const char *name, const char *fmt, ...)
{
    char buf[1536]; va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
    retro_log_message(name); retro_log_message(buf); return 0;
}
int32_t wm_ynbox(const char *name, const char *fmt, ...)
{
    char buf[1536]; va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
    retro_log_message(name); retro_log_message(buf); return 0;
}
char const *videoGetDisplayName(int) { return "Libretro"; }
void videoGetModes(int)
{
    validmodecnt = 0;
    for (auto const &mode : g_defaultVideoModes)
    {
        if (!mode.x) break;
        for (int fs = 0; fs < 2; ++fs)
        {
            auto &v = validmode[validmodecnt++];
            v.xdim = mode.x; v.ydim = mode.y; v.bpp = 8; v.fs = fs;
        }
    }
}
int32_t videoCheckMode(int32_t *w, int32_t *h, int32_t depth, int32_t fs, int32_t)
{
    if (depth != 8 || *w < 320 || *h < 200 || *w > 2560 || *h > 1600) return -1;
    videoGetModes();
    for (int i = 0; i < validmodecnt; ++i)
        if (validmode[i].xdim == *w && validmode[i].ydim == *h && validmode[i].fs == fs) return i;
    return 0x7fffffff;
}
int32_t videoSetMode(int32_t w, int32_t h, int32_t depth, int32_t fs)
{
    if (videoCheckMode(&w, &h, depth, fs, 1) < 0) return -1;
    framebuffer.assign(size_t(w) * h, 0);
    xres = w; yres = h; bpp = 8; fullscreen = fs; bytesperline = w;
    frameplace = 0; lockcount = 0; modechange = 1; numpages = 1;
    return 0;
}
void videoResetMode() { modechange = 1; }
void videoBeginDrawing()
{
    if (lockcount++ || offscreenrendering) return;
    frameplace = reinterpret_cast<intptr_t>(framebuffer.data());
    if (modechange) { bytesperline = xres; calc_ylookup(bytesperline, yres); modechange = 0; }
}
void videoEndDrawing()
{
    if (lockcount > 0) --lockcount;
    if (!lockcount && !offscreenrendering) frameplace = 0;
}
void videoShowFrame(int32_t)
{
    if (!framebuffer.empty()) retro_present(framebuffer.data(), xres, yres);
    retro_yield();
}
int32_t videoUpdatePalette(int32_t, int32_t) { return 0; }
int32_t videoSetGamma() { gammabrightness = 0; return 0; }
int32_t videoSetVsync(int32_t sync) { return vsync = sync; }
int32_t initinput(void (*callback)())
{
    for (int i = 0; i < NUMKEYS; ++i) g_keyRemapTable[i] = i;
    inputdevices = DEV_KEYBOARD | DEV_MOUSE | DEV_JOYSTICK;
    joystick.pAxis = axes; joystick.pHat = &hat; joystick.numAxes = 4;
    joystick.numButtons = 16; joystick.numHats = 0;
    joystick.validButtons = 0xffff; joystick.isGameController = 1;
    g_controllerHotplugCallback = callback;
    return 0;
}
void uninitinput() { keystatus[0] = 0; }
void joyScanDevices() {}
const char *joyGetName(int32_t, int32_t) { return "RetroPad"; }
void mouseInit() { g_mouseEnabled = true; g_mouseGrabbed = true; }
void mouseUninit() { g_mouseEnabled = false; }
void mouseGrabInput(bool grab) { g_mouseGrabbed = grab; }
void mouseLockToWindow(char lock) { g_mouseLockedToWindow = lock; }
void mouseMoveToCenter() { g_mouseAbs = {xres / 2, yres / 2}; }
int32_t handleevents_peekkeys() { return handleevents(); }
int32_t handleevents()
{
    static uint64_t lastEvents = UINT64_MAX;
    static unsigned polls;
    if (lastEvents != retro_time_usec()) polls = 0;
    if (++polls >= 256) { retro_yield(); polls = 0; }
    lastEvents = retro_time_usec();
    timerUpdateClock();
    return quitevent;
}
int timerInit(int rate) { clockRate = rate; clockSample = retro_time_usec() * rate / 1000000; clockCallback = nullptr; return 0; }
void timerUpdateClock()
{
    uint64_t ticks = retro_time_usec() * clockRate / 1000000;
    uint64_t count = ticks >= clockSample ? ticks - clockSample : 0; clockSample = ticks;
    totalclock += count;
    totalclock.setFraction((retro_time_usec() * clockRate % 1000000) * 65536 / 1000000);
    while (count-- && clockCallback) clockCallback();
}
int timerGetClockRate() { return clockRate; }
uint64_t timerGetNanoTickRate() { return 1000000ULL; }
uint64_t timerGetNanoTicks() { return retro_time_usec(); }
uint32_t timerGetTicks() { return retro_time_usec() / 1000; }
uint32_t timer120() { return retro_time_usec() * 120 / 1000000; }
double timerGetFractionalTicks() { return retro_time_usec() / 1000.0; }
uint64_t timerGetPerformanceCounter() { return retro_time_usec(); }
uint64_t timerGetPerformanceFrequency() { return 1000000; }
void (*timerSetCallback(void (*cb)()))() { auto old = clockCallback; clockCallback = cb; return old; }

int32_t mutex_init(mutex_t *m) { *m = new std::recursive_mutex; return 0; }
void mutex_destroy(mutex_t *m) { delete static_cast<std::recursive_mutex *>(*m); *m = nullptr; }
void retro_mutex_lock(mutex_t *m) { static_cast<std::recursive_mutex *>(*m)->lock(); }
void retro_mutex_unlock(mutex_t *m) { static_cast<std::recursive_mutex *>(*m)->unlock(); }
bool retro_mutex_try(mutex_t *m) { return static_cast<std::recursive_mutex *>(*m)->try_lock(); }
