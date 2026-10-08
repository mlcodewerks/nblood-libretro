#pragma once
#include <cstdint>
#include <cstddef>
struct RetroExit { int status; };
void retro_yield();
void retro_poll_build_input();
void retro_present(const uint8_t *pixels, unsigned width, unsigned height);
void retro_audio_render(int16_t *samples, size_t frames);
void retro_midi_advance(unsigned usec);
void retro_midi_write(uint8_t byte, uint32_t delta = 0);
void retro_midi_set_time(uint64_t usec);
void retro_apply_settings();
bool retro_freeaim_enabled();
uint64_t retro_time_usec();
void retro_log_message(const char *message);
