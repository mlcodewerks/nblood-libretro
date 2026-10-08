// Per-game policy; all frontend services live in core/platform/audio.cpp.
#pragma once
#include <cstddef>
struct RetroGameSettings { unsigned width, height; bool freeaim, autoaim; };
const char *retro_game_name();
const char *retro_game_directory();
const char *const *retro_game_required_files();
const char *const *retro_game_arguments(size_t *count);
bool retro_game_menu_active();
unsigned retro_game_pad_key(unsigned button);
unsigned retro_game_movement_key(const char *function);
void retro_game_apply(const RetroGameSettings &settings);
void retro_game_update(const RetroGameSettings &settings);
void retro_game_shutdown();
void retro_game_reset();
