// Game-specific configuration and controls over the shared libretro backend.
#include "game_adapter.h"
#include "bridge.h"
#include "libretro.h"
#include "build.h"
#include "mmulti.h"
#include "baselayer.h"
#include "keyboard.h"
#include "control.h"
#include "sndcards.h"
#include <cstring>
#if defined(LR_GAME_BLOOD)
#include "config.h"
#include "globals.h"
#include "blood.h"
#include "function.h"
#include "levels.h"
#include "gamemenu.h"
#include "player.h"
extern int MixRate, NumChannels, MusicDevice;
extern void ShutDown();
#elif defined(LR_GAME_DUKE3D) || defined(LR_GAME_RR)
#include "duke3d.h"
#include "global.h"
#include "game.h"
#include "player.h"
#include "config.h"
#include "function.h"
#elif defined(LR_GAME_SW)
#include "game.h"
#include "config.h"
#include "menus.h"
#include "function.h"
#include "network.h"
extern SWBOOL InMenuLevel;
extern void TerminateGame();
#elif defined(LR_GAME_EXHUMED)
#include "config.h"
#include "exhumed.h"
extern void ShutDown();
#elif defined(LR_GAME_TEKWAR)
#include "config.h"
#include "tekwar.h"
extern int mouseaiming, aimmode, mouseflip;
extern void shutdown();
#elif defined(LR_GAME_WITCHAVEN)
#include "config.h"
#include "witchaven.h"
extern int escapetomenu;
extern void shutdown();
#elif defined(LR_GAME_KENBUILD)
extern int32_t xdimgame, ydimgame, bppgame, forcesetup;
extern void uninitsb();
#endif

#if defined(NBLOOD_LIBRETRO_TESTS) && (defined(LR_GAME_DUKE3D) || defined(LR_GAME_RR) || defined(LR_GAME_SW))
#include "tests/state.h"
extern "C" RETRO_API void nblood_test_state(BloodTestState *s)
{
    *s={}; s->clock=int32_t(totalclock); s->menu=retro_game_menu_active();
#if defined(LR_GAME_DUKE3D) || defined(LR_GAME_RR)
    auto p=g_player[myconnectindex].ps;
    if (!p) return;
    s->started=!!(p->gm & MODE_GAME); s->x=p->pos.x; s->y=p->pos.y; s->z=p->pos.z;
    s->angle=p->q16ang; s->pitch=p->q16horiz; s->health=sprite[p->i].extra;
    s->weapon=p->curr_weapon; s->freeaim=p->aim_mode; s->autoaim=p->auto_aim; s->level=ud.level_number;
#else
    extern SWBOOL InGame;
    auto &p=Player[myconnectindex]; s->started=InGame && !InMenuLevel && p.cursectnum>=0;
    s->x=p.posx; s->y=p.posy; s->z=p.posz; s->angle=p.q16ang; s->pitch=p.q16horiz;
    s->health=1; s->freeaim=gs.MouseAimingOn; s->autoaim=gs.AutoAim;
#endif
}
#endif

const char *retro_game_name() { return LIBRETRO_GAME_NAME; }
const char *retro_game_directory() { return LIBRETRO_CORE_ID; }
const char *const *retro_game_required_files()
{
#if defined(LR_GAME_BLOOD)
    static const char *files[] = {"blood.rff","sounds.rff","gui.rff","blood.ini","surface.dat","voxel.dat",
        "tiles000.art","tiles001.art","tiles002.art","tiles003.art","tiles004.art","tiles005.art","tiles006.art","tiles007.art",
        "tiles008.art","tiles009.art","tiles010.art","tiles011.art","tiles012.art","tiles013.art","tiles014.art","tiles015.art",nullptr};
#elif defined(LR_GAME_DUKE3D)
    static const char *files[] = {"duke3d.grp",nullptr};
#elif defined(LR_GAME_RR)
#if defined(LR_VARIANT_NAM)
    static const char *files[] = {"nam.grp",nullptr};
#elif defined(LR_VARIANT_NAPALM)
    static const char *files[] = {"napalm.grp",nullptr};
#elif defined(LR_VARIANT_WW2GI)
    static const char *files[] = {"ww2gi.grp",nullptr};
#else
    static const char *files[] = {"redneck.grp",nullptr};
#endif
#elif defined(LR_GAME_SW)
    static const char *files[] = {"sw.grp",nullptr};
#elif defined(LR_GAME_EXHUMED)
    static const char *files[] = {"stuff.dat",nullptr};
#elif defined(LR_GAME_TEKWAR)
    static const char *files[] = {"tekwar.pk3",nullptr};
#elif defined(LR_GAME_WITCHAVEN)
    static const char *files[] = {"witchaven.pk3",nullptr};
#else
    static const char *files[] = {"stuff.dat",nullptr};
#endif
    return files;
}
const char *const *retro_game_arguments(size_t *count)
{
#if defined(LR_GAME_BLOOD)
    static const char *args[] = {LIBRETRO_CORE_ID,"-usecwd","-nosetup","-quick","-nodemo","-noautoload"};
#elif defined(LR_GAME_KENBUILD)
    static const char *args[] = {LIBRETRO_CORE_ID};
#elif defined(LR_VARIANT_NAM)
    static const char *args[] = {LIBRETRO_CORE_ID,"-usecwd","-nosetup","-noautoload","-g","nam.grp"};
#elif defined(LR_VARIANT_NAPALM)
    static const char *args[] = {LIBRETRO_CORE_ID,"-usecwd","-nosetup","-noautoload","-g","napalm.grp"};
#elif defined(LR_VARIANT_WW2GI)
    static const char *args[] = {LIBRETRO_CORE_ID,"-usecwd","-nosetup","-noautoload","-g","ww2gi.grp"};
#else
    static const char *args[] = {LIBRETRO_CORE_ID,"-usecwd","-nosetup","-noautoload"};
#endif
    *count = sizeof(args)/sizeof(*args); return args;
}
bool retro_game_menu_active()
{
#if defined(LR_GAME_BLOOD)
    return gInputMode != INPUT_MODE_0 || !gGameStarted;
#elif defined(LR_GAME_DUKE3D) || defined(LR_GAME_RR)
    auto p = g_player[myconnectindex].ps;
    return !p || !(p->gm & MODE_GAME) || (p->gm & MODE_MENU);
#elif defined(LR_GAME_SW)
    return UsingMenus || InMenuLevel;
#elif defined(LR_GAME_TEKWAR)
    return activemenu != 0;
#elif defined(LR_GAME_WITCHAVEN)
    return escapetomenu != 0;
#elif defined(LR_GAME_EXHUMED)
    extern bool retro_modal_menu; return retro_modal_menu;
#else
    return false;
#endif
}
unsigned retro_game_pad_key(unsigned button)
{
#if defined(LR_GAME_KENBUILD) || defined(LR_GAME_TEKWAR)
    extern unsigned char keys[];
    // Native bindings: jump/stand high, crouch/stand low, use, fire, map.
    switch(button) {
    case RETRO_DEVICE_ID_JOYPAD_A: return keys[8];
    case RETRO_DEVICE_ID_JOYPAD_B: return keys[9];
    case RETRO_DEVICE_ID_JOYPAD_X: return keys[7];
    case RETRO_DEVICE_ID_JOYPAD_R2: return keys[6];
    case RETRO_DEVICE_ID_JOYPAD_L3: return keys[4];
    case RETRO_DEVICE_ID_JOYPAD_SELECT: return keys[14];
    case RETRO_DEVICE_ID_JOYPAD_UP: return keys[0];
    case RETRO_DEVICE_ID_JOYPAD_DOWN: return keys[1];
    case RETRO_DEVICE_ID_JOYPAD_L: return 2;
    case RETRO_DEVICE_ID_JOYPAD_R: return 3;
    default: return 0;
    }
#else
    const char *name = nullptr;
    switch (button)
    {
    case RETRO_DEVICE_ID_JOYPAD_A: name="Jump"; break;
    case RETRO_DEVICE_ID_JOYPAD_B: name="Crouch"; break;
    case RETRO_DEVICE_ID_JOYPAD_X: name="Open"; break;
    case RETRO_DEVICE_ID_JOYPAD_Y: name="Inventory"; break;
    case RETRO_DEVICE_ID_JOYPAD_SELECT: name="Map"; break;
    case RETRO_DEVICE_ID_JOYPAD_L: name="Previous_Weapon"; break;
    case RETRO_DEVICE_ID_JOYPAD_R: name="Next_Weapon"; break;
    case RETRO_DEVICE_ID_JOYPAD_L2: name="Alt_Fire"; break;
    case RETRO_DEVICE_ID_JOYPAD_R2: name="Fire"; break;
    case RETRO_DEVICE_ID_JOYPAD_L3: name="Run"; break;
    case RETRO_DEVICE_ID_JOYPAD_R3: name="Center_View"; break;
    case RETRO_DEVICE_ID_JOYPAD_LEFT: name="Inventory_Left"; break;
    case RETRO_DEVICE_ID_JOYPAD_RIGHT: name="Inventory_Right"; break;
    case RETRO_DEVICE_ID_JOYPAD_UP: name="Move_Forward"; break;
    case RETRO_DEVICE_ID_JOYPAD_DOWN: name="Move_Backward"; break;
    }
#if defined(LR_GAME_BLOOD)
    if (button==RETRO_DEVICE_ID_JOYPAD_Y) name="Inventory_Use";
    if (button==RETRO_DEVICE_ID_JOYPAD_R2) name="Weapon_Fire";
    if (button==RETRO_DEVICE_ID_JOYPAD_L2) name="Weapon_Special_Fire";
    if (button==RETRO_DEVICE_ID_JOYPAD_SELECT) name="Map_Toggle";
    if (button==RETRO_DEVICE_ID_JOYPAD_R3) name="Aim_Center";
#elif defined(LR_GAME_WITCHAVEN)
    if (button==RETRO_DEVICE_ID_JOYPAD_Y) name="Use_Potion";
    if (button==RETRO_DEVICE_ID_JOYPAD_L2) name="Cast_Spell";
    if (button==RETRO_DEVICE_ID_JOYPAD_LEFT) name="Potion_Left";
    if (button==RETRO_DEVICE_ID_JOYPAD_RIGHT) name="Potion_Right";
    if (button==RETRO_DEVICE_ID_JOYPAD_UP) name="Spell_Left";
    if (button==RETRO_DEVICE_ID_JOYPAD_DOWN) name="Spell_Right";
    if (button==RETRO_DEVICE_ID_JOYPAD_R3) name="Look_Straight";
#endif
    if (!name) return 0;
#if defined(LR_GAME_DUKE3D) || defined(LR_GAME_RR)
    auto &bindings = ud.config.KeyboardKeys;
#else
    auto &bindings = KeyboardKeys;
#endif
    for (unsigned i=0; i<sizeof(bindings)/sizeof(bindings[0]); ++i)
        if (!strcmp(gamefunctions[i],name)) return bindings[i][0];
    // Ports without a native weapon-cycle function use their default number keys.
    if (button==RETRO_DEVICE_ID_JOYPAD_L) return 2;
    if (button==RETRO_DEVICE_ID_JOYPAD_R) return 3;
    if (button==RETRO_DEVICE_ID_JOYPAD_R3) return 199;
    return 0;
#endif
}
unsigned retro_game_movement_key(const char *name)
{
#if defined(LR_GAME_KENBUILD) || defined(LR_GAME_TEKWAR)
    extern unsigned char keys[];
    if (!strcmp(name,"Move_Forward")) return keys[0];
    if (!strcmp(name,"Move_Backward")) return keys[1];
    if (!strcmp(name,"Strafe_Left")) return keys[12];
    if (!strcmp(name,"Strafe_Right")) return keys[13];
#elif defined(LR_GAME_WITCHAVEN)
    for(unsigned i=0;i<sizeof(KeyboardKeys)/sizeof(KeyboardKeys[0]);++i)
        if (!strcmp(name,gamefunctions[i])) return KeyboardKeys[i][0];
#endif
    return 0;
}
void retro_game_update(const RetroGameSettings &s)
{
#if defined(LR_GAME_BLOOD)
    gMouseAim=s.freeaim; gAutoAim=s.autoaim; gMouseAimingFlipped=0; gAimReticle=1;
#elif defined(LR_GAME_DUKE3D) || defined(LR_GAME_RR)
    ud.mouseaiming=0; ud.config.AutoAim=s.autoaim; ud.mouseflip=0; ud.crosshair=1;
    extern int32_t g_myAimMode;
    g_myAimMode=s.freeaim;
    if (g_player[myconnectindex].ps) { g_player[myconnectindex].ps->aim_mode=s.freeaim; g_player[myconnectindex].ps->auto_aim=s.autoaim; }
#elif defined(LR_GAME_SW)
    gs.MouseAimingOn=s.freeaim; gs.MouseAimingType=0; gs.MouseInvert=0; gs.AutoAim=s.autoaim;
    if (s.freeaim) Player[myconnectindex].Flags |= PF_MOUSE_AIMING_ON;
    else Player[myconnectindex].Flags &= ~PF_MOUSE_AIMING_ON;
#elif defined(LR_GAME_EXHUMED) || defined(LR_GAME_WITCHAVEN) || defined(LR_GAME_TEKWAR)
    mouseaiming=0; aimmode=s.freeaim; mouseflip=0;
#endif
}
void retro_game_apply(const RetroGameSettings &s)
{
#if defined(LR_GAME_KENBUILD)
    xdimgame=s.width; ydimgame=s.height; bppgame=8; fullscreen=0; forcesetup=0;
#else
#if defined(LR_GAME_DUKE3D) || defined(LR_GAME_RR)
    auto &setup=ud.setup;
    ud.config.MixRate=48000; ud.config.NumChannels=2; ud.config.MusicDevice=ASS_SDL;
#elif defined(LR_GAME_SW)
    auto &setup=ud_setup;
    MixRate=48000; NumChannels=2; MusicDevice=ASS_SDL;
#else
    auto &setup=gSetup;
    MixRate=48000; NumChannels=2;
    MusicDevice=ASS_SDL;
#endif
    setup.xdim=s.width; setup.ydim=s.height; setup.bpp=8; setup.fullscreen=0;
    setup.forcesetup=0; setup.usemouse=1; setup.usejoystick=1; setup.noautoload=1;
#endif
    r_maxfps=-2; retro_game_update(s);
}
void retro_game_shutdown()
{
    if (!in3dmode()) return;
#if defined(LR_GAME_BLOOD) || defined(LR_GAME_EXHUMED)
    ShutDown();
#elif defined(LR_GAME_DUKE3D) || defined(LR_GAME_RR)
    G_Shutdown(); qsetmode=0;
#elif defined(LR_GAME_SW)
    TerminateGame(); qsetmode=0;
#elif defined(LR_GAME_TEKWAR) || defined(LR_GAME_WITCHAVEN)
    shutdown(); qsetmode=0;
#else
    uninitsb(); engineUnInit(); qsetmode=0;
#endif
}
void retro_game_reset()
{
#if defined(LR_GAME_BLOOD)
    if (gGameStarted) gStartNewGame=true;
#elif defined(LR_GAME_SW)
    extern SWBOOL NewGame; NewGame=TRUE;
#else
    // Return to each port's menu with the same input path used by the user.
    keySetState(sc_Escape,1); if (keypresscallback) keypresscallback(sc_Escape,1);
#endif
}
#if defined(LR_GAME_BLOOD) && defined(NBLOOD_LIBRETRO_TESTS)
#include "tests/state.h"
extern "C" RETRO_API void nblood_test_state(BloodTestState *s)
{
    *s={}; s->started=gGameStarted; s->menu=gInputMode; s->clock=int32_t(totalclock);
    s->level=gGameOptions.nLevel; s->freeaim=gMouseAim; s->autoaim=gAutoAim;
    if (gGameStarted && gMe && gMe->pSprite) {
        s->x=gMe->pSprite->x; s->y=gMe->pSprite->y; s->z=gMe->pSprite->z;
        s->angle=gMe->q16ang; s->pitch=gMe->q16look; s->weapon=gMe->curWeapon; s->health=gMe->pXSprite->health;
    }
}
#endif

#ifdef LR_GAME_EXHUMED
bool retro_modal_menu=false;
#endif
