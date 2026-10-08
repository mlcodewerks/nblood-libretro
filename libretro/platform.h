#pragma once
#include "baselayer.h"
extern int32_t maxrefreshfreq;
static inline void idle() { handleevents(); }
static inline void idle_waitevent() { handleevents(); }
static inline void idle_waitevent_timeout(uint32_t) { handleevents(); }
#ifdef _WIN32
static inline HWND win_gethwnd() { return nullptr; }
static inline HINSTANCE win_gethinstance() { return GetModuleHandle(nullptr); }
#endif
