#include "core.h"

#include "Config.hpp"
#include "Debug.hpp"
#include "gfx.h"
#include "gfx_structures.h"

#include "game_menu.h"
#include "game_qs.h"
#include "replay.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cmath>
#include <cstdint>
#include <cinttypes>

#include <iostream>
#include <vector>
#include <array>
#include <string>

#include "SDL.h"
#ifdef OPENGL_INTERPOLATION
#define GL_GLEXT_PROTOTYPES
#include "glad.h"
#endif
#include "SDL_image.h"
#include "SDL_mixer.h"
#include "SGUIL/SGUIL.hpp"
#include "GuiGridCanvas.hpp"
#include "GuiScreenManager.hpp"

#include "SPM_Spec.hpp"
#include "QRS.hpp"
#include "ShiroPhysoMino.hpp"

#define PENTOMINO_C_REVISION_STRING "rev 1.3"

using namespace Shiro;
using namespace std;
using namespace PDINI;

BindableVariables bindables;

#if(defined(_WIN64) || defined(_WIN32)) && !defined(__CYGWIN__) && !defined(__CYGWIN32__) && !defined(__MINGW32__) && \
    !defined(__MINGW64__)
#define _WIN32_WINNT 0x0400
#include <direct.h>
#define chdir _chdir

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#if 0
// TODO: Check Windows docs, and see if setting errno to EFAULT might make
// sense somewhere here. The EINTR case might be wrong, too.
// TODO: Add more granular waits for the seconds portion, then precise waiting
// for the rest.
int nanosleep(const struct timespec* req, struct timespec* rem) {
    LARGE_INTEGER startTime;
    if (req->tv_nsec < 0 || req->tv_nsec > 999999999ll || req->tv_sec < 0) {
        errno = EINVAL;
        return -1;
    }
    QueryPerformanceCounter(&startTime);
    LARGE_INTEGER waitTime = { .QuadPart = -((LONGLONG)req->tv_sec * 10000000ll + (LONGLONG)req->tv_nsec / 100ll) };
    const HANDLE timer = CreateWaitableTimer(NULL, TRUE, NULL);
    if (timer) {
        // Signal after waiting for the requested time, but if the signal took
        // too long, use millisecond precision with WaitForSingleObject.
        SetWaitableTimer(timer, &waitTime, 0, NULL, NULL, 0);
        WaitForSingleObject(timer, waitTime.QuadPart / 10000);
        CloseHandle(timer);
        return 0;
    }
    else {
        errno = EINTR;
        LARGE_INTEGER endTime;
        LARGE_INTEGER frequency;
        // Microsoft's docs guarantee these don't fail on XP or newer, and
        // that's all we're supporting. -nightmareci
        QueryPerformanceCounter(&endTime);
        QueryPerformanceFrequency(&frequency);
        LONGLONG nsWait = ((endTime.QuadPart - startTime.QuadPart) * 1000000000ll) / frequency.QuadPart;
        LONGLONG nsRem = ((LONGLONG)req->tv_sec * 1000000000ll + (LONGLONG)req->tv_nsec) - nsWait;
        rem->tv_sec = nsRem / 1000000000ll;
        rem->tv_nsec = nsRem % 1000000000ll;
        return -1;
    }
}
#endif

#define S_ISDIR(flags) ((flags) & S_IFDIR)
#else
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h> // For chdir
#endif

#if 0
static long framedelay(Uint64 ticks_elap, double fps)
{
    const Uint64 freq = SDL_GetPerformanceFrequency();
    const double start = (double)SDL_GetPerformanceCounter() / freq;
    if(fps < 1 || fps > 240)
        return FRAMEDELAY_ERR;

    struct timespec t = {0, 0};
    double sec_elap = (double)(ticks_elap) / freq;
    double spf = (1 / fps);

    if(sec_elap < spf)
    {
        t.tv_nsec = (long)((spf - sec_elap) * 1000000000ll);
#ifndef BUSYLOOP_DELAY

        struct timespec rem;
        if(nanosleep(&t, &rem))
        {
            // this can happen when the user presses Ctrl+C
            log_err("nanosleep() returned failure during frame length calculation");
            return FRAMEDELAY_ERR;
        }
#else
        const double end = start + spf - sec_elap;
        for (double newTime = start; newTime < end; newTime = (double)SDL_GetPerformanceCounter() / freq);
#endif
    }
    else
    {
        return (spf - sec_elap) * 1000000000.0;
    }

    if(t.tv_nsec)
        return t.tv_nsec;
    else
        return 1;
}
#endif

int is_left_input_repeat(coreState *cs, int delay)
{
    return cs->keys.left && cs->hold_dir == DAS_LEFT && cs->hold_time >= delay;
}

int is_right_input_repeat(coreState *cs, int delay)
{
    return cs->keys.right && cs->hold_dir == DAS_RIGHT && cs->hold_time >= delay;
}

int is_up_input_repeat(coreState *cs, int delay)
{
    return cs->keys.up && cs->hold_dir == DAS_UP && cs->hold_time >= delay;
}

int is_down_input_repeat(coreState *cs, int delay)
{
    return cs->keys.down && cs->hold_dir == DAS_DOWN && cs->hold_time >= delay;
}

#include "Path.hpp"
static string make_path(const char *base, const char *subdir, const char *name, const char *ext)
{
    string path = base ? string{base} : string{"."};
    path.append("/");
    path.append(subdir);
    path.append("/");
    path.append(name);
    path.append(ext);

    //printf("asset: %s\n", directory.c_str());
    return path;
}

gfx_animation *load_anim_bg(coreState *cs, const char *directory, int frame_multiplier)
{
    if(!directory)
        return NULL;

    gfx_animation *a = (gfx_animation *)malloc(sizeof(gfx_animation));
    a->frame_multiplier = frame_multiplier;
    a->x = 0;
    a->y = 0;
    a->flags = 0;
    a->counter = 0;
    a->rgba_mod = RGBA_DEFAULT;

    struct stat s;
    chdir("gfx");
    int err = stat(directory, &s);
    chdir("..");

    if(-1 == err)
    {
        if(ENOENT == errno)
        {
            return NULL;
        }
    }
    else
    {
        if(!S_ISDIR(s.st_mode))
        {
            return NULL;
        }
    }

    // TODO: convert to new asset system - create an animation asset
    /*
    for(int i = 0; i < 1000; i++) {
       full_path = bformat("%s/%05d", directory, i);
       if(load_asset(cs, ASSET_IMG, (char *)(full_path->data)) < 0) {
          a->num_frames = i;
          break;
       } else {
          //printf("Loaded frame #%d of animated bg: %s\n", i, full_path->data);
       }

        bdestroy(full_path);
     }
     */

    return a;
}

void coreState_initialize(coreState *cs)
{
    int i = 0;

    cs->fps = MENU_FPS;
    // cs->keyquit = SDLK_F11;
    cs->text_editing = 0;
    cs->text_insert = NULL;
    cs->text_backspace = NULL;
    cs->text_delete = NULL;
    cs->text_seek_left = NULL;
    cs->text_seek_right = NULL;
    cs->text_seek_home = NULL;
    cs->text_seek_end = NULL;
    cs->text_select_all = NULL;
    cs->text_copy = NULL;
    cs->text_cut = NULL;
    cs->left_arrow_das = 0;
    cs->right_arrow_das = 0;
    cs->backspace_das = 0;
    cs->delete_das = 0;
    cs->select_all = 0;
    cs->undo = 0;
    cs->redo = 0;

    cs->zero_pressed = 0;
    cs->one_pressed = 0;
    cs->two_pressed = 0;
    cs->three_pressed = 0;
    cs->four_pressed = 0;
    cs->five_pressed = 0;
    cs->six_pressed = 0;
    cs->seven_pressed = 0;
    cs->nine_pressed = 0;

    cs->assets = new assetdb;

    cs->joystick = NULL;
    cs->prev_keys_raw = {0};
    cs->keys_raw = {0};
    cs->prev_keys = {0};
    cs->keys = {0};
    cs->pressed = {0};
    cs->hold_dir = DAS_NONE;
    cs->hold_time = 0;

    cs->mouse_x = 0;
    cs->mouse_y = 0;
    cs->logical_mouse_x = 0;
    cs->logical_mouse_y = 0;
    cs->mouse_left_down = 0;
    cs->mouse_right_down = 0;

    cs->screen.name = "Shiromino " SHIROMINO_VERSION_STRING;
    cs->screen.w = 640;
    cs->screen.h = 480;
    cs->screen.window = NULL;
    cs->screen.renderer = NULL;
#ifdef OPENGL_INTERPOLATION
    cs->screen.target_tex = NULL;
#endif

    cs->bg = NULL;
    cs->bg_old = NULL;
    cs->bg_r = 0u;
    cs->bg_g = 0u;
    cs->bg_b = 0u;
    // cs->anim_bg = NULL;
    // cs->anim_bg_old = NULL;
    cs->gfx_messages_max = 0;
    cs->gfx_animations_max = 0;
    cs->gfx_buttons_max = 0;

    cs->settings = nullptr;
    cs->menu_input_override = 0;
    cs->button_emergency_override = 0;
    cs->p1game = NULL;
    cs->menu = NULL;

    cs->screenManager = new GuiScreenManager {};

    cs->displayMode = game_display_default;
    cs->motionBlur = false;
    cs->pracdata_mirror = NULL;

    //cs->avg_sleep_ms = 0;
    //cs->avg_sleep_ms_recent = 0;
    cs->frames = 0;

#if 0
    for(i = 0; i < RECENT_FRAMES; i++)
    {
        cs->avg_sleep_ms_recent_array[i] = 0;
    }

    cs->recent_frame_overload = -1;
#endif
}

void coreState_destroy(coreState *cs)
{
    if(!cs)
    {
        return;
    }

    delete cs->settings;

    if(cs->pracdata_mirror)
        pracdata_destroy(cs->pracdata_mirror);
}

static void load_image(coreState *cs, gfx_image *img, const char *filename)
{
    Path path(cs->settings->basePath);
    path << "gfx" << filename;
    if(!img_load(img, path, cs))
    {
        log_warn("Failed to load image '%s'", filename);
    }
}

static void load_bitfont(BitFont *font, gfx_image *sheetImg, gfx_image *outlineSheetImg, unsigned int charW, unsigned int charH)
{
    font->sheet = sheetImg->tex;
    font->outlineSheet = outlineSheetImg->tex;
    font->charW = charW;
    font->charH = charH;
    font->isValid = true;
}

static void load_sfx(coreState* cs, INI& ini, Sfx** s, const char* filename)
{
    //string directory = make_path(cs->settings->basePath.c_str(), "audio", filename, "");
    Path path(cs->settings->basePath);
    path << "audio" << filename;
    *s = new Sfx();
    if (!(*s)->load(path)) {
        log_warn("Failed to load sfx '%s'", filename);
    }

    float volume;
    if (!ini.get("", filename, volume) || volume < 0.0f || volume > 100.0f) {
        (*s)->volume = 100.0f;
    }
    else {
        (*s)->volume = volume;
    }
}

static void load_music(coreState* cs, INI& ini, Music*& m, const char* name)
{
    Path directory(cs->settings->basePath);
    directory << "audio";
    m = new Music();
    if (!m->load(directory, name)) {
        log_warn("Failed to load music '%s'", name);
    }

    float volume;
    if (!ini.get("", name, volume) || volume < 0.0f || volume > 100.0f) {
        m->volume = 100.0f;
    }
    else {
        m->volume = volume;
    }
}

int load_files(coreState *cs)
{
    if(!cs)
        return -1;

        // image assets

#define IMG(name, filename) load_image(cs, &cs->assets->name, filename);
#include "images.h"
#undef IMG

#define FONT(name, sheetName, outlineSheetName, charW, charH) \
    load_bitfont(&cs->assets->name, &cs->assets->sheetName, &cs->assets->outlineSheetName, charW, charH)
#include "fonts.h"
#undef FONT

        // audio assets

    {
        INI ini(false);
        //string audioINIPath = make_path(cs->settings->basePath.c_str(), "audio", "volume", ".ini");
        Path audioINIPath(cs->settings->basePath);
        audioINIPath << "audio" << "volume.ini";
        ini.read(audioINIPath);

#define MUSIC(name, i) load_music(cs, ini, cs->assets->name[i], #name #i);
#include "music.h"
#undef MUSIC

#define SFX(name) load_sfx(cs, ini, &cs->assets->name, #name);
#define SFX_ARRAY(name, i) load_sfx(cs, ini, &cs->assets->name[i], #name #i);
#include "sfx.h"
#undef SFX_ARRAY
#undef SFX
    }

#if 0
    #ifdef ENABLE_ANIM_BG
       string filename;

       for(int i = 0; i < 10; i++) {
          stringstream ss;
          ss << "g2_bg/bg" << i;
          filename = ss.str();
          cs->g2_bgs[i] = load_anim_bg(cs, filename.c_str(), 2);
          //if(cs->g2_bgs[i]) printf("Successfully loaded G2 bg #%d\n", i);
       }
    #endif
#endif
    return 0;
}

#ifdef OPENGL_INTERPOLATION
GLuint CompileShader(GLenum shaderType, const GLchar* shaderSource) {
    GLint compileOK;
    GLuint shader = glCreateShader(shaderType);
    glShaderSource(shader, 1, &shaderSource, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compileOK);
    if (compileOK) {
        return shader;
    }
    else {
        GLint infoLogLength;
        char* infoLog;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLogLength);
        infoLog = new char[infoLogLength];
        glGetShaderInfoLog(shader, infoLogLength, NULL, infoLog);
        fprintf(stderr, "Could not compile %s shader.\n", (shaderType == GL_VERTEX_SHADER) ? "vertex" : (shaderType == GL_GEOMETRY_SHADER) ? "geometry" : (shaderType == GL_FRAGMENT_SHADER) ? "fragment" : "unknown");
        fprintf(stderr, "%s\n", infoLog);
        delete[] infoLog;
        return 0u;
    }
}

GLuint CompileShadingProgram(const GLchar* vertexShaderSource, const GLchar* geometryShaderSource, const GLchar* fragmentShaderSource) {
    GLuint vertexShader = CompileShader(GL_VERTEX_SHADER, vertexShaderSource);
    if (vertexShader == 0u) {
        return 0u;
    }

    GLuint geometryShader = 0u;
    if (geometryShaderSource != NULL) {
        geometryShader = CompileShader(GL_GEOMETRY_SHADER, geometryShaderSource);
        if (geometryShader == 0u) {
            glDeleteShader(vertexShader);
            return 0u;
        }
    }

    GLuint fragmentShader = CompileShader(GL_FRAGMENT_SHADER, fragmentShaderSource);
    if (fragmentShader == 0u) {
        if (geometryShader != 0u) glDeleteShader(geometryShader);
        glDeleteShader(vertexShader);
        return 0u;
    }

    GLuint program = glCreateProgram();
    if (program == 0u) {
        log_err("Could not create shading program.");
        glDeleteShader(vertexShader);
        if (geometryShader != 0u) glDeleteShader(geometryShader);
        glDeleteShader(fragmentShader);
        return 0u;
    }

    glAttachShader(program, vertexShader);
    if (geometryShader != 0u) glAttachShader(program, geometryShader);
    glAttachShader(program, fragmentShader);

    glLinkProgram(program);

    glDetachShader(program, vertexShader);
    if (geometryShader != 0u) glDetachShader(program, geometryShader);
    glDetachShader(program, fragmentShader);

    glDeleteShader(vertexShader);
    if (geometryShader != 0u) glDeleteShader(geometryShader);
    glDeleteShader(fragmentShader);

    GLint linkOK;
    glGetProgramiv(program, GL_LINK_STATUS, &linkOK);
    if (linkOK) {
        return program;
    }
    else {
        GLint infoLogLength;
        char* infoLog;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &infoLogLength);
        infoLog = new char[infoLogLength];
        glGetProgramInfoLog(program, infoLogLength, NULL, infoLog);
        log_err("Could not link shading program.\n");
        log_err("%s", infoLog);
        delete[] infoLog;
        glDeleteProgram(program);
        return 0u;
    }
}
#endif

int init(coreState *cs, Settings* settings)
{
    try {
        if(!cs)
            return -1;

        const char *name = NULL;
        // SDL_Texture *blank = NULL;

        // copy settings into main game structure

        if(settings) {
            cs->settings = settings;
            printf("Base path is: %s\n", cs->settings->basePath.c_str());
            if(chdir(cs->settings->basePath.c_str()) < 0) { // chdir so relative paths later on make sense
                log_err("chdir() returned failure");
            }
        }
        else {
            cs->settings = new Settings();
        }

#ifdef OPENGL_INTERPOLATION
        if (cs->settings->interpolate) {
            SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl");
        }
#endif
        check(SDL_Init(SDL_INIT_EVENTS | SDL_INIT_TIMER | SDL_INIT_VIDEO | SDL_INIT_AUDIO) == 0,
              "SDL_Init: Error: %s\n",
              SDL_GetError());
        check(SDL_InitSubSystem(SDL_INIT_JOYSTICK) == 0, "SDL_InitSubSystem: Error: %s\n", SDL_GetError());
        if (SDL_SetThreadPriority(SDL_THREAD_PRIORITY_HIGH) != 0) {
            printf("Failed to set high thread priority; continuing without changing thread priority\n");
        }
        check(IMG_Init(IMG_INIT_PNG) == IMG_INIT_PNG,
            "IMG_Init: Failed to initialize PNG support: %s\n",
            IMG_GetError());  // IMG_GetError() not necessarily reliable here
        check(Mix_Init(MIX_INIT_OGG) == MIX_INIT_OGG,
            "Mix_Init: Failed to initialize OGG support: %s\n",
            Mix_GetError());
        check(Mix_OpenAudio(22050, MIX_DEFAULT_FORMAT, 2, 1024) != -1, "Mix_OpenAudio: Error\n");

        Mix_AllocateChannels(32);

        if (SDL_NumJoysticks()) {
            const int numJoysticks = SDL_NumJoysticks();
            for (int i = 0; i < numJoysticks; i++) {
                SDL_Joystick* joystick;
                if (joystick = SDL_JoystickOpen(i)) {
                    printf("Attached joystick: \"%s\" at index %d\n", SDL_JoystickName(joystick), i);
                    SDL_JoystickClose(joystick);
                }
                else {
                    printf("Joystick index %d not attached\n", i);
                }
            }

            cs->joystick = nullptr;
            if (cs->settings->joyBinds.name != "") {
                const int numJoysticks = SDL_NumJoysticks();
                for (int i = 0; i < numJoysticks; i++) {
                    if (cs->joystick = SDL_JoystickOpen(i)) {
                        if (SDL_JoystickName(cs->joystick) == cs->settings->joyBinds.name) {
                            cs->settings->joyBinds.joyIndex = i;
                            cs->settings->joyBinds.joyID = SDL_JoystickInstanceID(cs->joystick);
                            break;
                        }
                        else {
                            SDL_JoystickClose(cs->joystick);
                            cs->joystick = nullptr;
                        }
                    }
                }
            }

            if (!cs->joystick && cs->settings->joyBinds.joyIndex >= 0 && cs->settings->joyBinds.joyIndex < SDL_NumJoysticks()) {
                if (cs->joystick = SDL_JoystickOpen(cs->settings->joyBinds.joyIndex)) {
                    cs->settings->joyBinds.joyID = SDL_JoystickInstanceID(cs->joystick);
                }
            }

            if (cs->settings->joyBinds.joyIndex >= 0 && cs->joystick) {
                printf("Joysticks are enabled\n");
                printf("Name: %s\n", SDL_JoystickNameForIndex(cs->settings->joyBinds.joyIndex));
                printf("Index: %d\n", cs->settings->joyBinds.joyIndex);
                printf("Number of buttons: %d\n", SDL_JoystickNumButtons(cs->joystick));
                printf("Number of axes: %d\n", SDL_JoystickNumAxes(cs->joystick));
                printf("Number of hats: %d\n", SDL_JoystickNumHats(cs->joystick));
            }
            else {
                cs->joystick = nullptr;
                printf("Joysticks are disabled\n");
            }
        }
        else {
            printf("No joysticks are attached\n");
        }

        cs->screen.w = cs->settings->videoScale * 640u;
        cs->screen.h = cs->settings->videoScale * 480u;
        unsigned int w = cs->screen.w;
        unsigned int h = cs->screen.h;
        name = cs->screen.name;

        Uint32 windowFlags = SDL_WINDOW_RESIZABLE;
#ifdef OPENGL_INTERPOLATION
        // TODO: Figure out whether OpenGL 2.0 should be used for desktop. Also
        // support OpenGL ES 2.0.
        if (cs->settings->interpolate) {
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
            windowFlags |= SDL_WINDOW_OPENGL;
        }
#endif

        cs->screen.window = SDL_CreateWindow(name, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h, windowFlags);
        check(cs->screen.window != NULL, "SDL_CreateWindow: Error: %s\n", SDL_GetError());
        cs->screen.renderer =
            SDL_CreateRenderer(cs->screen.window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE | (cs->settings->vsync ? SDL_RENDERER_PRESENTVSYNC : 0));
        check(cs->screen.renderer != NULL, "SDL_CreateRenderer: Error: %s\n", SDL_GetError());

        SDL_SetRenderDrawBlendMode(cs->screen.renderer, SDL_BLENDMODE_BLEND);

        SDL_SetWindowMinimumSize(cs->screen.window, 640, 480);
        if(cs->settings->fullscreen)
        {
            SDL_SetWindowSize(cs->screen.window, 640, 480);
            SDL_SetWindowFullscreen(cs->screen.window, SDL_WINDOW_FULLSCREEN);
        }

        SDL_RenderSetLogicalSize(cs->screen.renderer, 640, 480);
        if(!cs->settings->videoStretch)
        {
            SDL_RenderSetIntegerScale(cs->screen.renderer, SDL_TRUE);
        }

#ifdef OPENGL_INTERPOLATION
        if (cs->settings->interpolate) {
            cs->screen.target_tex = SDL_CreateTexture(cs->screen.renderer, SDL_GetWindowPixelFormat(cs->screen.window),  SDL_TEXTUREACCESS_TARGET, 640, 480);
        }
        else {
            cs->screen.target_tex = NULL;
        }

        cs->screen.interpolate_shading_prog = 0u;

        if (cs->settings->interpolate) {
            if (!gladLoadGL()) {
                return 1;
            }

            glDisable(GL_DEPTH_TEST);
            glEnable(GL_TEXTURE_2D);

            /*
            Vertex layout (triangle strip):

            0 - 2
            | / |
            1 - 3
            */

            // By avoiding as much client-to-server data transfer as possible,
            // performance should be better; memory access on modern systems is much
            // more expensive than just generating the data with computation, in many
            // cases. So just calculate positions from gl_VertexID, rather than using
            // vertex arrays. And take advantage of hardware linear interpolation,
            // rather than sampling in the fragment shader multiple times and doing
            // our own interpolation.
            cs->screen.interpolate_shading_prog = CompileShadingProgram(
                "#version 330\n"
                "out vec2 texCoord;\n"
                "void main() {\n"
                "   vec4 pos = vec4(float(gl_VertexID / 2 != 0) * 2 - 1, float(gl_VertexID % 2 == 0) * 2 - 1, 0.0, 1.0);\n"
                "   gl_Position = pos;\n"
                "   texCoord = (pos.xy + vec2(1.0, -1.0)) / vec2(2.0, -2.0);\n"
                "}\n",

                NULL,

                "#version 330\n"
                "in vec2 texCoord;\n"
                "uniform sampler2D tex;\n"
                "uniform vec2 viewportSize;\n"
                "layout(location = 0) out vec4 outColor;\n"
                "const vec2 texSize = vec2(640.0, 480.0);\n"

                // filterCoord provided by PARTY MAN X ( https://github.com/PARTYMANX ).
                // It's better than the thing I hacked together before. -Brandon McGriff
                "vec2 filterCoord(vec2 uv, vec2 srcRes) {\n"
                    "vec2 invSrc = 1.0 / srcRes;\n"

                    // calculate destination resolution
                    "vec2 duv = vec2(dFdx(uv.x), dFdy(uv.y));\n"
                    "vec2 dstRes = 1.0 / abs(duv);\n"

                    "vec2 scale = dstRes * invSrc;\n"                   // (dstRes / invSrc)

                    "vec2 scaleFactor = floor(scale) + 1.0;\n"          // add one to integer scale factor so we scale down, not up

                    "vec2 texelCoord = uv * srcRes;\n"                  // coordinate in texel space

                    "vec2 roundCoord = floor(texelCoord) + 0.5;\n"      // center of nearest texel of uv
                    "vec2 fractCoord = fract(texelCoord) - 0.5;\n"      // offset from nearest texel

                    "vec2 offset = abs(fractCoord * scaleFactor);\n"    // absolute offset multiplied by scale factor
                    "offset -= 0.5 * (scaleFactor - 1.0);\n"            // subtract border of scaled texel
                    "offset = max(offset, 0.0) * sign(fractCoord);\n"   // get rid of wrong direction offsets, restore sign

                    "return (roundCoord + offset) * invSrc;\n"
                "}\n"

                "void main() {\n"
                "   outColor = texture(tex, filterCoord(texCoord, texSize));\n"
                "}\n");
            if (!cs->screen.interpolate_shading_prog) {
                return 1;
            }

            glUseProgram(cs->screen.interpolate_shading_prog);
            glUniform1i(glGetUniformLocation(cs->screen.interpolate_shading_prog, "tex"), 0);
            glUseProgram(0u);

            if (SDL_GL_BindTexture(cs->screen.target_tex, NULL, NULL) < 0) {
                fprintf(stderr, "Failed to bind target_tex.\n");
            }
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            if (SDL_GL_UnbindTexture(cs->screen.target_tex) < 0) {
                fprintf(stderr, "Failed to unbind target_tex.\n");
            }
        }
#endif

        check(load_files(cs) == 0, "load_files() returned failure\n");

        check(Gui_Init(cs->screen.renderer, NULL), "Gui_Init() returned failure\n");
        check(gfx_init(cs) == 0, "gfx_init returned failure\n");

        cs->bg = cs->assets->bg_temp.tex;
        cs->bg_old = cs->bg;
        cs->bg_r = 255;
        cs->bg_g = 255;
        cs->bg_b = 255;
        // blank = cs->assets->blank.tex;

        // check(SDL_RenderCopy(cs->screen.renderer, blank, NULL, NULL) > -1, "SDL_RenderCopy: Error: %s\n", SDL_GetError());

        cs->menu = menu_create(cs);
        check(cs->menu != NULL, "menu_create returned failure\n");

        cs->menu->init(cs->menu);

        cs->screenManager->addScreen("main", mainMenu_create(cs, cs->screenManager, cs->assets->fixedsys));
        //SDL_Rect gameScreenRect = {0, 0, 640, 480};
        //cs->screenManager->addScreen("game", new GuiScreen {cs, "game", NULL, gameScreenRect});
        cs->screenManager->loadScreen("main");

        // check(SDL_RenderCopy(cs->screen.renderer, blank, NULL, NULL) > -1, "SDL_RenderCopy: Error: %s\n", SDL_GetError());

        // TODO: Configurable scores.db directory
        static const char scoredb_file[] = "scores.db";
        scoredb_init(&cs->scores, scoredb_file);
        scoredb_create_player(&cs->scores, &cs->player, cs->settings->playerName.c_str());

        /*
        static const char archive_file[] = "archive.db";
        scoredb_init(&cs->archive, archive_file);
        scoredb_create_player(&cs->archive, &cs->player, cs->settings->player_name);
        */

        cs->menu = menu_create(cs);
        check(cs->menu != NULL, "menu_create returned failure\n");

        cs->menu->init(cs->menu);

        printf("\nPENTOMINO C: %s\n\n", PENTOMINO_C_REVISION_STRING);

        return 0;
    }
    catch (const logic_error& error) {
        return 1;
    }
}

void quit(coreState *cs)
{
    scoredb_terminate(&cs->scores);
    // scoredb_terminate(&cs->archive);

    if(cs->assets)
    {

        // Already destroyed in the BitFont destructor previously; this prevents a double-free.
        cs->assets->font_fixedsys_excelsior.tex = nullptr;

#define IMG(name, filename) img_destroy(&cs->assets->name);
#include "images.h"
#undef IMG

        // All the textures have been destroyed, so prevent them being freed by
        // the GuiWindow destructor.
#define FONT(name, sheetName, outlineSheetName, charW, charH) \
        cs->assets->name.isValid = false; \
        cs->assets->name.sheet = nullptr; \
        cs->assets->name.outlineSheet = nullptr
#include "fonts.h"
#undef FONT

#define MUSIC(name, i) delete cs->assets->name[i];
#include "music.h"
#undef MUSIC

#define SFX(name) delete cs->assets->name;
#define SFX_ARRAY(name, i) delete cs->assets->name[i];
#include "sfx.h"
#undef SFX_ARRAY
#undef SFX

        delete cs->assets;
    }

#ifdef OPENGL_INTERPOLATION
    if (cs->screen.target_tex) {
        SDL_DestroyTexture(cs->screen.target_tex);
    }
    if (cs->screen.interpolate_shading_prog) {
        glDeleteProgram(cs->screen.interpolate_shading_prog);
    }
#endif

    if(cs->screen.renderer)
        SDL_DestroyRenderer(cs->screen.renderer);

    if(cs->screen.window)
        SDL_DestroyWindow(cs->screen.window);

    cs->screen.window = NULL;
    cs->screen.renderer = NULL;

    if(cs->joystick && SDL_JoystickGetAttached(cs->joystick))
    {
        SDL_JoystickClose(cs->joystick);
    }

    if(cs->p1game)
    {
        printf("quit(): Found leftover game struct, attempting ->quit\n");
        cs->p1game->quit(cs->p1game);
        free(cs->p1game);
        cs->p1game = NULL;
    }

    if(cs->menu)
    {
        cs->menu->quit(cs->menu);
        free(cs->menu);
        cs->menu = NULL;
    }

    /*for(int i = 0; i < 10; i++) {
       if(cs->g2_bgs[i])
          gfx_animation_destroy(cs->g2_bgs[i]);
    }*/

    gfx_quit(cs);

    IMG_Quit();
    Mix_Quit();
    SDL_Quit();
}

int run(coreState *cs)
{
    int procStatus = 0;
    if(!cs)
        return -1;

    bool running = true;

    int windW = 620;
    int windH = 460;
    SDL_Rect gridRect = {16, 44, windW - 32, windH - 60};

    Grid* g = new Grid(gridRect.w / 16, gridRect.h / 16);
    SDL_Texture *paletteTex = cs->assets->tets_bright_qs.tex;

    BindableInt paletteVar {"paletteVar", 0, 25};

    GuiGridCanvas *gridCanvas = new GuiGridCanvas{
        0,
        g,
        paletteVar,
        paletteTex,
        32, 32,
        gridRect
    };

    SDL_Rect windowRect = {10, 10, windW, windH};
    // TODO: Fix how fixedsys is loaded/unloaded; currently, both SGUIL and
    // Shiromio try to free it, with SGUIL freeing it first in the GuiWindow
    // destructor.
    GuiWindow wind {cs, "Shiromino", &cs->assets->fixedsys, GUI_WINDOW_CALLBACK_NONE, windowRect};

    wind.addControlElement(gridCanvas);

    // SPM_Spec spec;
    QRS spec {qrs_variant_P, false};
    TestSPM SPMgame {*cs, &spec};
    SPMgame.init();

    //cs->p1game = qs_game_create(cs, 0, MODE_PENTOMINO, NO_REPLAY);
    //cs->p1game->init(cs->p1game);

    double currentTime = static_cast<double>(SDL_GetPerformanceCounter()) / SDL_GetPerformanceFrequency();
    double startTime = currentTime;
    double timeAccumulator = 0.0;
// #define DEBUG_FRAME_TIMING
#ifdef DEBUG_FRAME_TIMING
    // Due to limitations in SDL's display refresh rate reporting, FPS
    // debugging can't be made to display the correct monitor FPS when vsync
    // and vsyncTimestep are enabled. So use the VIDEOFPS INI option to get
    // correct FPS information.
    double videoFPS;
    {
        INI ini;
        ini.read(cs->iniFilename);
        if (!ini.get("SCREEN", "VIDEOFPS", videoFPS) || videoFPS <= 0.0) {
            videoFPS = 0.0;
        }
    }
    double timeFromFrames = 0.0;
    const double fpsTimeFrameDuration = 1.0;
    double fpsTimeFrameStart = 0.0;
#endif

    while(running)
    {
        double newTime = static_cast<double>(SDL_GetPerformanceCounter()) / SDL_GetPerformanceFrequency();
        double renderFrameTime = newTime - currentTime;
        if (renderFrameTime > 0.25) {
            renderFrameTime = 0.25;
        }
        currentTime = newTime;

        timeAccumulator += renderFrameTime;

        unsigned newFrames = 0u;
#ifdef DEBUG_FRAME_TIMING
        for (
            double gameFrameTime = 1.0 / (cs->settings->vsync && cs->settings->vsyncTimestep && videoFPS > 0.0 ? videoFPS : cs->fps);
            timeAccumulator >= gameFrameTime || (cs->settings->vsyncTimestep && cs->settings->vsync && newFrames == 0u);
            timeAccumulator -= gameFrameTime,
            newFrames++,
            cs->frames++
            )
#else
        for (
            double gameFrameTime = 1.0 / cs->fps;
            timeAccumulator >= gameFrameTime || (cs->settings->vsyncTimestep && cs->settings->vsync && newFrames == 0u);
            timeAccumulator -= gameFrameTime,
            newFrames++,
            cs->frames++
            )
#endif
        {
            // TODO: Move input polling out of the game frame update loop?
            // Requires reworking input handling, because moving this code
            // outside the game frame update loop results in incorrect input
            // behavior.
            cs->prev_keys_raw = cs->keys_raw;
            cs->prev_keys = cs->keys;

            if (procevents(cs, wind))
            {
                return 1;
            }

            handle_replay_input(cs);

            update_input_repeat(cs);
            update_pressed(cs);

            gfx_buttons_input(cs);

            /*
            SPMgame.input();
            SPMgame.frame();
            SPMgame.draw();
            */

            if (cs->p1game) {
                if (procgame(cs->p1game, !cs->button_emergency_override)) {
                    cs->p1game->quit(cs->p1game);
                    free(cs->p1game);
                    cs->p1game = NULL;

                    gfx_start_bg_fade_in(cs, cs->assets->bg_temp.tex);
                    break;
                }
            }
            if (cs->menu && ((!cs->p1game || cs->menu_input_override) ? 1 : 0)) {
                procStatus = procgame(cs->menu, !cs->button_emergency_override);
                if (procStatus) {
                    cs->menu->quit(cs->menu);
                    free(cs->menu);

                    cs->menu = NULL;

                    if (!cs->p1game) {
                        running = 0;
                    }
                }
            }

            if (!cs->menu && !cs->p1game) {
                running = false;
            }
            else if (cs->p1game) {
                cs->p1game->frame_counter++;
            }
            else if (cs->menu && (!cs->p1game || cs->menu_input_override)) {
                cs->menu->frame_counter++;
            }

            gfx_updatebg(cs);

#ifndef DEBUG_FRAME_TIMING
            gameFrameTime = 1.0 / cs->fps;
#else
            gameFrameTime = 1.0 / (cs->settings->vsync && cs->settings->vsyncTimestep && videoFPS > 0.0 ? videoFPS : cs->fps);
            timeFromFrames += gameFrameTime;
            // TODO: From testing with this, the game apparently doesn't reset
            // FPS to 60 when returning to the menu from a game, so fix that.
            // Though each mode does use its correct FPS.
            double realTime = static_cast<double>(SDL_GetPerformanceCounter()) / SDL_GetPerformanceFrequency() - startTime;
            printf("real FPS: %f\ngame FPS: %f\n\n", (realTime / timeFromFrames) * (cs->settings->vsync && cs->settings->vsyncTimestep && videoFPS > 0.0 ? videoFPS : cs->fps), cs->settings->vsync && cs->settings->vsyncTimestep && videoFPS > 0.0 ? videoFPS : cs->fps);
            if (realTime - fpsTimeFrameStart >= fpsTimeFrameDuration) {
                timeFromFrames = realTime;
                fpsTimeFrameStart = realTime;
            }
#endif
            if (cs->settings->vsync && cs->settings->vsyncTimestep) {
                timeAccumulator = 0.0;
            }
        }

#ifdef OPENGL_INTERPOLATION
        if (cs->settings->interpolate) {
            SDL_SetRenderTarget(cs->screen.renderer, cs->screen.target_tex);
        }
#endif
        SDL_RenderClear(cs->screen.renderer);

        gfx_drawbg(cs);

        if (cs->p1game) {
            cs->p1game->draw(cs->p1game);
        }
        else if (cs->menu && ((!cs->p1game || cs->menu_input_override) ? 1 : 0)) {
            cs->menu->draw(cs->menu);
        }

        gfx_drawbuttons(cs, 0);
        gfx_drawmessages(cs, 0);
        gfx_drawanimations(cs, 0);

        if (cs->button_emergency_override)
            gfx_draw_emergency_bg_darken(cs);

        gfx_drawbuttons(cs, EMERGENCY_OVERRIDE);
        gfx_drawmessages(cs, EMERGENCY_OVERRIDE);
        gfx_drawanimations(cs, EMERGENCY_OVERRIDE);

#ifdef OPENGL_INTERPOLATION
        if (cs->settings->interpolate) {
            SDL_RenderFlush(cs->screen.renderer);
            SDL_SetRenderTarget(cs->screen.renderer, NULL);

            glUseProgram(cs->screen.interpolate_shading_prog);
            int w, h;
            SDL_GL_GetDrawableSize(cs->screen.window, &w, &h);
            if ((SDL_GetWindowFlags(cs->screen.window) & ~SDL_WINDOW_FULLSCREEN_DESKTOP) != SDL_WINDOW_FULLSCREEN) {
                double widthFactor, heightFactor;
                if (cs->settings->videoStretch) {
                    widthFactor = w / 640.0;
                    heightFactor = h / 480.0;
                }
                else {
                    widthFactor = w / 640;
                    heightFactor = h / 480;
                    if (widthFactor > cs->settings->videoScale) {
                        widthFactor = cs->settings->videoScale;
                    }
                    if (heightFactor > cs->settings->videoScale) {
                        heightFactor = cs->settings->videoScale;
                    }
                }
                GLsizei viewportWidth, viewportHeight;
                if (widthFactor > heightFactor) {
                    viewportWidth = heightFactor * 640;
                    viewportHeight = heightFactor * 480;
                }
                else {
                    viewportWidth = widthFactor * 640;
                    viewportHeight = widthFactor * 480;
                }
                glViewport((w - viewportWidth) / 2, (h - viewportHeight) / 2, viewportWidth, viewportHeight);
                glUniform2f(glGetUniformLocation(cs->screen.interpolate_shading_prog, "viewportSize"), viewportWidth, viewportHeight);
            }
            else {
                // TODO: Change this once a fullscreen resolution setting is supported.
                glViewport(0, 0, w, h);
                glUniform2f(glGetUniformLocation(cs->screen.interpolate_shading_prog, "viewportSize"), w, h);
            }
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);

            glActiveTexture(GL_TEXTURE0);
            if (SDL_GL_BindTexture(cs->screen.target_tex, NULL, NULL) < 0) {
                fprintf(stderr, "Failed to bind target_tex.\n");
            }
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4u);
            glUseProgram(0u);
            if (SDL_GL_UnbindTexture(cs->screen.target_tex) < 0) {
                fprintf(stderr, "Failed to unbind target_tex.\n");
            }

            SDL_GL_SwapWindow(cs->screen.window);
        }
        else {
            SDL_RenderPresent(cs->screen.renderer);
        }
#else
        SDL_RenderPresent(cs->screen.renderer);
#endif

        if (newFrames > 0u) {
            for (size_t i = 0; i < cs->gfx_messages_max; i++) {
                if (cs->gfx_messages[i].counter >= newFrames) {
                    cs->gfx_messages[i].counter -= newFrames;
                }
                else {
                    cs->gfx_messages[i].counter = 0u;
                }
            }

            for (size_t i = 0; i < cs->gfx_animations_max; i++) {
                const unsigned maxFrames = static_cast<unsigned>(cs->gfx_animations[i].frame_multiplier * cs->gfx_animations[i].num_frames);
                if (cs->gfx_animations[i].counter + newFrames < maxFrames) {
                   cs->gfx_animations[i].counter += newFrames;
                }
                else {
                    cs->gfx_animations[i].counter = maxFrames;
                }
            }
        }
        if (!cs->settings->vsync) {
            SDL_Delay(cs->settings->frameDelay);
        }

#if 0
        {
            //cs->screenManager->drawScreen();
        }
#endif
        // TODO: Reimplement lag percentage display with the new fixed timestep code.
#if 0
        newTime = SDL_GetPerformanceCounter() - newTime;
        long sleep_ns = framedelay(newTime, cs->fps);

        if(sleep_ns == FRAMEDELAY_ERR)
            return 1;

        cs->avg_sleep_ms = ((cs->avg_sleep_ms * (long double)(cs->frames)) + ((long double)(sleep_ns) / 1000000.0L)) /
                           (long double)(cs->frames + 1);
        cs->frames++;

        if(cs->frames <= RECENT_FRAMES)
        {
            cs->avg_sleep_ms_recent_array[cs->frames - 1] = ((long double)(sleep_ns) / 1000000.0L);
            if(sleep_ns < 0)
            {
                // printf("Lag from last frame in microseconds: %ld (%f frames)\n", abs(sleep_ns/1000), fabs((long
                // double)(sleep_ns)/(1000000000.0L/(long double)(cs->fps))));
                cs->recent_frame_overload = cs->frames - 1;
            }
        }
        else
        {
            cs->avg_sleep_ms_recent_array[(cs->frames - 1) % RECENT_FRAMES] = ((long double)(sleep_ns) / 1000000.0L);
            if(sleep_ns < 0)
            {
                // printf("Lag from last frame in microseconds: %ld (%f frames)\n", abs(sleep_ns/1000), fabs((long
                // double)(sleep_ns)/(1000000000.0L/(long double)(cs->fps))));
                cs->recent_frame_overload = (cs->frames - 1) % RECENT_FRAMES;
            }
            else if(cs->recent_frame_overload == (cs->frames - 1) % RECENT_FRAMES)
                cs->recent_frame_overload = -1;
        }

        cs->avg_sleep_ms_recent = 0;
        for(int i = 0; i < RECENT_FRAMES; i++)
        {
            cs->avg_sleep_ms_recent += cs->avg_sleep_ms_recent_array[i];
        }

        cs->avg_sleep_ms_recent /= (cs->frames < RECENT_FRAMES ? cs->frames : RECENT_FRAMES);

        // printf("Frame elapsed.\n");
#endif
    }

    if (procStatus != 2)
        return 0;
    else
        return 2;
}

int procevents(coreState *cs, GuiWindow& wind)
{
    if(!cs)
        return -1;

    struct keyflags *k = NULL;
    //struct keyflags joyflags{0};

    SDL_Event event;
    SDL_Keycode kc;
    KeyBinds& kb = cs->settings->keyBinds;
    JoyBinds& jb = cs->settings->joyBinds;

    Uint8 rc = 0;

    if(cs->mouse_left_down == BUTTON_PRESSED_THIS_FRAME)
    {
        cs->mouse_left_down = 1;
    }
    if(cs->mouse_right_down == BUTTON_PRESSED_THIS_FRAME)
    {
        cs->mouse_right_down = 1;
    }

    if(cs->select_all)
    {
        cs->select_all = 0;
    }
    if(cs->undo)
    {
        cs->undo = 0;
    }
    if(cs->redo)
    {
        cs->redo = 0;
    }

    SDL_GetMouseState(&cs->mouse_x, &cs->mouse_y);

    int windowW;
    int windowH;
    SDL_GetWindowSize(cs->screen.window, &windowW, &windowH);

    if(windowW == (windowH * 4) / 3)
    {
        float scale_ = (float)windowW / 640.0;
        cs->logical_mouse_x = (int)((float)cs->mouse_x / scale_);
        cs->logical_mouse_y = (int)((float)cs->mouse_y / scale_);
    }
    else if(windowW < (windowH * 4) / 3) // squished horizontally (results in horizontal bars on the top and bottom of window)
    {
        float scale_ = (float)windowW / 640.0;
        int yOffset = (windowH - ((windowW * 3) / 4)) / 2;
        if(cs->mouse_y < yOffset || cs->mouse_y >= windowH - yOffset)
        {
            cs->logical_mouse_y = -1;
        }
        else
        {
            cs->logical_mouse_y = (int)((float)(cs->mouse_y - yOffset) / scale_);
        }

        cs->logical_mouse_x = (int)((float)cs->mouse_x / scale_);
    }
    else
    {
        float scale_ = (float)windowH / 480.0;
        int xOffset = (windowW - ((windowH * 4) / 3)) / 2;
        if(cs->mouse_x < xOffset || cs->mouse_x >= windowW - xOffset)
        {
            cs->logical_mouse_x = -1;
        }
        else
        {
            cs->logical_mouse_x = (int)((float)(cs->mouse_x - xOffset) / scale_);
        }

        cs->logical_mouse_y = (int)((float)cs->mouse_y / scale_);
    }

    while(SDL_PollEvent(&event))
    {
        // wind.handleSDLEvent(event, {cs->logical_mouse_x, cs->logical_mouse_y} );
        //printf("Handling SDL event\n");
        //cs->screenManager->handleSDLEvent(event, {cs->logical_mouse_x, cs->logical_mouse_y});

        switch (event.type) {
            case SDL_QUIT:
                return 1;

            case SDL_JOYAXISMOTION:
                k = &cs->keys_raw;

                if (event.jaxis.which == jb.joyID) {
                    if (event.jaxis.axis == jb.axes.x) {
                        if (event.jaxis.value > JOYSTICK_DEAD_ZONE * jb.axes.right) {
                            k->right = 1;
                            k->left = 0;
                        }
                        else if (event.jaxis.value < JOYSTICK_DEAD_ZONE * -jb.axes.right) {
                            k->left = 1;
                            k->right = 0;
                        }
                        else {
                            k->right = 0;
                            k->left = 0;
                        }
                    }
                    else if (event.jaxis.axis == jb.axes.y) {
                        if (event.jaxis.value > JOYSTICK_DEAD_ZONE * jb.axes.down) {
                            k->down = 1;
                            k->up = 0;
                        }
                        else if (event.jaxis.value < JOYSTICK_DEAD_ZONE * -jb.axes.down) {
                            k->up = 1;
                            k->down = 0;
                        }
                        else {
                            k->up = 0;
                            k->down = 0;
                        }
                    }
                }

                break;

            case SDL_JOYHATMOTION:
                k = &cs->keys_raw;

                if (event.jhat.which == jb.joyID && event.jhat.hat == jb.hatIndex) {
                    if (event.jhat.value == SDL_HAT_CENTERED) {
                        k->right = 0;
                        k->left = 0;
                        k->up = 0;
                        k->down = 0;
                    }
                    else {
                        if ((event.jhat.value & (SDL_HAT_LEFT | SDL_HAT_RIGHT)) != (SDL_HAT_LEFT | SDL_HAT_RIGHT)) {
                            k->left = !!(event.jhat.value & SDL_HAT_LEFT);
                            k->right = !!(event.jhat.value & SDL_HAT_RIGHT);
                        }
                        else {
                            k->left = 0;
                            k->right = 0;
                        }

                        if ((event.jhat.value & (SDL_HAT_UP | SDL_HAT_DOWN)) != (SDL_HAT_UP | SDL_HAT_DOWN)) {
                            k->up = !!(event.jhat.value & SDL_HAT_UP);
                            k->down = !!(event.jhat.value & SDL_HAT_DOWN);
                        }
                        else {
                            k->up = 0;
                            k->down = 0;
                        }
                    }
                }

                break;

            case SDL_JOYBUTTONDOWN:
            case SDL_JOYBUTTONUP:
                k = &cs->keys_raw;
                if (event.jbutton.which == jb.joyID) {
                    #define CHECK_BUTTON(name) \
                    if (event.jbutton.button == jb.buttons.name) { \
                        k->name = event.type == SDL_JOYBUTTONDOWN; \
                    }

                    CHECK_BUTTON(left);
                    CHECK_BUTTON(right);
                    CHECK_BUTTON(up);
                    CHECK_BUTTON(down);
                    CHECK_BUTTON(start);
                    CHECK_BUTTON(a);
                    CHECK_BUTTON(b);
                    CHECK_BUTTON(c);
                    CHECK_BUTTON(d);
                    CHECK_BUTTON(escape);

                    #undef CHECK_BUTTON
                }

                break;

            case SDL_KEYDOWN:
                if(event.key.repeat)
                    break;

                kc = event.key.keysym.sym;

                k = &cs->keys_raw;

                if(kc == kb.left)
                    k->left = 1;

                if(kc == kb.right)
                    k->right = 1;

                if(kc == kb.up)
                    k->up = 1;

                if(kc == kb.down)
                    k->down = 1;

                if(kc == kb.start)
                    k->start = 1;

                if(kc == kb.a)
                    k->a = 1;

                if(kc == kb.b)
                    k->b = 1;

                if(kc == kb.c)
                    k->c = 1;

                if(kc == kb.d)
                    k->d = 1;

                if(kc == kb.escape)
                    k->escape = 1;

                if(kc == SDLK_v && SDL_GetModState() & KMOD_CTRL)
                {
                    if(cs->text_editing && cs->text_insert)
                        cs->text_insert(cs, SDL_GetClipboardText());

                    break;
                }

                if(kc == SDLK_c && SDL_GetModState() & KMOD_CTRL)
                {
                    if(cs->text_editing && cs->text_copy)
                        cs->text_copy(cs);

                    break;
                }

                if(kc == SDLK_x && SDL_GetModState() & KMOD_CTRL)
                {
                    if(cs->text_editing && cs->text_cut)
                        cs->text_cut(cs);

                    break;
                }

                if(kc == SDLK_a && SDL_GetModState() & KMOD_CTRL)
                {
                    if(cs->text_editing && cs->text_select_all)
                        cs->text_select_all(cs);
                    cs->select_all = 1;
                    break;
                }

                if(kc == SDLK_z && SDL_GetModState() & KMOD_CTRL)
                {
                    cs->undo = 1;
                    break;
                }

                if(kc == SDLK_y && SDL_GetModState() & KMOD_CTRL)
                {
                    cs->redo = 1;
                    break;
                }

                if(kc == SDLK_BACKSPACE)
                {
                    cs->backspace_das = 1;
                }

                if(kc == SDLK_DELETE)
                {
                    cs->delete_das = 1;
                }

                if(kc == SDLK_HOME)
                {
                    if(cs->text_editing && cs->text_seek_home)
                        cs->text_seek_home(cs);
                }

                if(kc == SDLK_END)
                {
                    if(cs->text_editing && cs->text_seek_end)
                        cs->text_seek_end(cs);
                }

                if(kc == SDLK_RETURN)
                {
                    if(cs->text_toggle)
                        cs->text_toggle(cs);
                }

                if(kc == SDLK_LEFT)
                {
                    cs->left_arrow_das = 1;
                }

                if(kc == SDLK_RIGHT)
                {
                    cs->right_arrow_das = 1;
                }

                if(kc == SDLK_F8)
                {
                    switch(cs->displayMode)
                    {
                        case game_display_default:
                            cs->displayMode = game_display_detailed;
                            break;
                        case game_display_detailed:
                            cs->displayMode = game_display_centered;
                            break;
                        default:
                            cs->displayMode = game_display_default;
                            break;
                    }
                }

                if(kc == SDLK_F9)
                {
                    cs->motionBlur = !cs->motionBlur;
                }

                if(kc == SDLK_F11)
                {
                    if(cs->settings->fullscreen)
                    {
                        cs->settings->fullscreen = false;
                        SDL_SetWindowFullscreen(cs->screen.window, 0);
                        SDL_SetWindowSize(cs->screen.window, 640.0*cs->settings->videoScale, 480.0*cs->settings->videoScale);
                    }
                    else
                    {
                        cs->settings->fullscreen = true;
                        SDL_SetWindowSize(cs->screen.window, 640, 480);
                        SDL_WindowFlags flags = (SDL_GetModState() & KMOD_SHIFT) ? SDL_WINDOW_FULLSCREEN_DESKTOP : SDL_WINDOW_FULLSCREEN;
                        if(SDL_SetWindowFullscreen(cs->screen.window, flags) < 0)
                        {
                            cout << "SDL_SetWindowFullscreen(): Error: " << SDL_GetError() << endl;
                        }
                    }
                }

                if(kc == SDLK_F4 && SDL_GetModState() & KMOD_ALT)
                {
                    return 1;
                }

                if(kc == SDLK_0)
                {
                    cs->zero_pressed = 1;
                }

                if(kc == SDLK_1)
                {
                    if(SDL_GetModState() & KMOD_ALT)
                    {
                        cs->settings->videoScale = 1;
                        SDL_SetWindowSize(cs->screen.window, 640, 480);
                    }

                    cs->one_pressed = 1;
                }

                if(kc == SDLK_2)
                {
                    if(SDL_GetModState() & KMOD_ALT)
                    {
                        cs->settings->videoScale = 2;
                        SDL_SetWindowSize(cs->screen.window, 2*640, 2*480);
                    }

                    cs->two_pressed = 1;
                }

                if(kc == SDLK_3)
                {
                    if(SDL_GetModState() & KMOD_ALT)
                    {
                        cs->settings->videoScale = 3;
                        SDL_SetWindowSize(cs->screen.window, 3*640, 3*480);
                    }

                    cs->three_pressed = 1;
                }

                if(kc == SDLK_4)
                {
                    if(SDL_GetModState() & KMOD_ALT)
                    {
                        cs->settings->videoScale = 4;
                        SDL_SetWindowSize(cs->screen.window, 4*640, 4*480);
                    }

                    cs->four_pressed = 1;
                }

                if(kc == SDLK_5)
                {
                    if(SDL_GetModState() & KMOD_ALT)
                    {
                        cs->settings->videoScale = 5;
                        SDL_SetWindowSize(cs->screen.window, 5*640, 5*480);
                    }

                    cs->five_pressed = 1;
                }

                if(kc == SDLK_6)
                {
                    cs->six_pressed = 1;
                }

                if(kc == SDLK_7)
                {
                    cs->seven_pressed = 1;
                }

                if(kc == SDLK_9)
                {
                    cs->nine_pressed = 1;
                }

                break;

            case SDL_KEYUP:
                kc = event.key.keysym.sym;

                k = &cs->keys_raw;

                if(kc == kb.left)
                    k->left = 0;

                if(kc == kb.right)
                    k->right = 0;

                if(kc == kb.up)
                    k->up = 0;

                if(kc == kb.down)
                    k->down = 0;

                if(kc == kb.start)
                    k->start = 0;

                if(kc == kb.a)
                    k->a = 0;

                if(kc == kb.b)
                    k->b = 0;

                if(kc == kb.c)
                    k->c = 0;

                if(kc == kb.d)
                    k->d = 0;

                if(kc == kb.escape)
                    k->escape = 0;

                if(kc == SDLK_LEFT)
                    cs->left_arrow_das = 0;

                if(kc == SDLK_RIGHT)
                    cs->right_arrow_das = 0;

                if(kc == SDLK_BACKSPACE)
                    cs->backspace_das = 0;

                if(kc == SDLK_DELETE)
                    cs->delete_das = 0;

                if(kc == SDLK_0)
                {
                    cs->zero_pressed = 0;
                }

                if(kc == SDLK_1)
                {
                    cs->one_pressed = 0;
                }

                if(kc == SDLK_2)
                {
                    cs->two_pressed = 0;
                }

                if(kc == SDLK_3)
                {
                    cs->three_pressed = 0;
                }

                if(kc == SDLK_4)
                {
                    cs->four_pressed = 0;
                }

                if(kc == SDLK_5)
                {
                    cs->five_pressed = 0;
                }

                if(kc == SDLK_6)
                {
                    cs->six_pressed = 0;
                }

                if(kc == SDLK_7)
                {
                    cs->seven_pressed = 0;
                }

                if(kc == SDLK_9)
                {
                    cs->nine_pressed = 0;
                }

                break;

            case SDL_TEXTINPUT:
                if(cs->text_editing)
                {
                    if(!((event.text.text[0] == 'c' || event.text.text[0] == 'C') &&
                         (event.text.text[0] == 'v' || event.text.text[0] == 'V') &&
                         (event.text.text[0] == 'x' || event.text.text[0] == 'X') &&
                         (event.text.text[0] == 'a' || event.text.text[0] == 'A') && SDL_GetModState() & KMOD_CTRL))
                    {
                        if(cs->text_insert)
                            cs->text_insert(cs, event.text.text);
                    }
                }

                break;

            case SDL_MOUSEBUTTONDOWN:
                if(event.button.button == SDL_BUTTON_LEFT)
                {
                    cs->mouse_left_down = BUTTON_PRESSED_THIS_FRAME;
                }
                if(event.button.button == SDL_BUTTON_RIGHT)
                    cs->mouse_right_down = BUTTON_PRESSED_THIS_FRAME;
                break;

            case SDL_MOUSEBUTTONUP:
                if(event.button.button == SDL_BUTTON_LEFT)
                    cs->mouse_left_down = 0;
                if(event.button.button == SDL_BUTTON_RIGHT)
                    cs->mouse_right_down = 0;
                break;

            default:
                break;
        }
    }

    if(cs->left_arrow_das)
    {
        if(cs->left_arrow_das == 1 || cs->left_arrow_das == 30)
        {
            if(cs->text_editing && cs->text_seek_left)
                cs->text_seek_left(cs);
            if(cs->left_arrow_das == 1)
                cs->left_arrow_das = 2;
        }
        else
        {
            cs->left_arrow_das++;
        }
    }

    if(cs->right_arrow_das)
    {
        if(cs->right_arrow_das == 1 || cs->right_arrow_das == 30)
        {
            if(cs->text_editing && cs->text_seek_right)
                cs->text_seek_right(cs);
            if(cs->right_arrow_das == 1)
                cs->right_arrow_das = 2;
        }
        else
        {
            cs->right_arrow_das++;
        }
    }

    if(cs->backspace_das)
    {
        if(cs->backspace_das == 1 || cs->backspace_das == 30)
        {
            if(cs->text_editing && cs->text_backspace)
                cs->text_backspace(cs);
            if(cs->backspace_das == 1)
                cs->backspace_das = 2;
        }
        else
        {
            cs->backspace_das++;
        }
    }

    if(cs->delete_das)
    {
        if(cs->delete_das == 1 || cs->delete_das == 30)
        {
            if(cs->text_editing && cs->text_delete)
                cs->text_delete(cs);
            if(cs->delete_das == 1)
                cs->delete_das = 2;
        }
        else
        {
            cs->delete_das++;
        }
    }

    /*
        SDL_Joystick *joy = cs->joystick;
        const uint8_t *keystates = SDL_GetKeyboardState(NULL);

        if (cs->settings->keybinds) {
            k = &cs->keys_raw;
            kb = cs->settings->keybinds;

            *k = (struct keyflags) { 0 };

            if (joy) {
                Uint8 hat = SDL_JoystickGetHat(joy, 0);
                if(hat == SDL_HAT_LEFT)
                    k->left = 1;

                if(hat == SDL_HAT_RIGHT)
                    k->right = 1;

                if(hat == SDL_HAT_UP)
                    k->up = 1;

                if(hat == SDL_HAT_DOWN)
                    k->down = 1;

                rc = SDL_JoystickGetButton(joy, 0);
                if (!rc)
                    k->a = 0;
                if (rc && k->a == 0)
                    k->a = 1;

                rc = SDL_JoystickGetButton(joy, 3);
                if (!rc)
                    k->b = 0;
                if (rc && k->b == 0)
                    k->b = 1;

                rc = SDL_JoystickGetButton(joy, 5);
                if (!rc)
                    k->c = 0;
                if (rc && k->c == 0)
                    k->c = 1;

                rc = SDL_JoystickGetButton(joy, 1);
                if (!rc)
                    k->d = 0;
                if (rc && k->d == 0)
                    k->d = 1;
            }

            if (keystates[SDL_GetScancodeFromKey(kb->left)])
                k->left = 1;

            if (keystates[SDL_GetScancodeFromKey(kb->right)])
                k->right = 1;

            if (keystates[SDL_GetScancodeFromKey(kb->up)])
                k->up = 1;

            if (keystates[SDL_GetScancodeFromKey(kb->down)])
                k->down = 1;

            if (keystates[SDL_GetScancodeFromKey(kb->start)])
                k->start = 1;

            if (keystates[SDL_GetScancodeFromKey(kb->a)])
                k->a = 1;

            if (keystates[SDL_GetScancodeFromKey(kb->b)])
                k->b = 1;

            if (keystates[SDL_GetScancodeFromKey(kb->c)])
                k->c = 1;

            if (keystates[SDL_GetScancodeFromKey(kb->d)])
                k->d = 1;

            if (keystates[SDL_GetScancodeFromKey(kb->escape)])
                k->escape = 1;

            cs->keys = cs->keys_raw;
        }
    */

    cs->keys = cs->keys_raw;

    return 0;
}

int procgame(game_t *g, int input_enabled)
{
    if(!g)
        return -1;

    if(g->preframe)
    {
        if(g->preframe(g))
            return 1;
    }

    if(g->input && input_enabled)
    {
        int inputStatus = g->input(g);
        if (inputStatus) {
            return inputStatus;
        }
    }

    Uint64 benchmark = SDL_GetPerformanceCounter();

    if(g->frame)
    {
        if(g->frame(g))
            return 1;
    }

    benchmark = SDL_GetPerformanceCounter() - benchmark;
    //   printf("%fms\n", (double)(benchmark) * 1000 / (double)SDL_GetPerformanceFrequency());

    return 0;
}

void handle_replay_input(coreState *cs)
{
    game_t *g = cs->p1game;
    if(g != NULL)
    {
        qrsdata *q = (qrsdata *)g->data;

        if(q == NULL)
        {
            return;
        }

        if(q->playback)
        {
            if((unsigned int)(q->playback_index) == q->replay->len)
                qrs_end_playback(g);
            else
            {
                unpack_input(q->replay->pinputs[q->playback_index], &cs->keys);

                q->playback_index++;
            }
        }
        else if(q->recording)
        {
            q->replay->pinputs[q->replay->len] = pack_input(&cs->keys_raw);

            q->replay->len++;
        }
    }
}

void update_input_repeat(coreState *cs)
{
    struct keyflags *k = &cs->keys;

    if(cs->hold_dir == DAS_LEFT && k->right)
    {
        cs->hold_time = 0;
        cs->hold_dir = DAS_RIGHT;
    }
    else if(cs->hold_dir == DAS_RIGHT && k->left)
    {
        cs->hold_time = 0;
        cs->hold_dir = DAS_LEFT;
    }
    else if(cs->hold_dir == DAS_UP && k->down)
    {
        cs->hold_time = 0;
        cs->hold_dir = DAS_DOWN;
    }
    else if(cs->hold_dir == DAS_DOWN && k->up)
    {
        cs->hold_time = 0;
        cs->hold_dir = DAS_UP;
    }

    if(cs->hold_dir == DAS_LEFT && k->left)
        cs->hold_time++;
    else if(cs->hold_dir == DAS_RIGHT && k->right)
        cs->hold_time++;
    else if(cs->hold_dir == DAS_UP && k->up)
        cs->hold_time++;
    else if(cs->hold_dir == DAS_DOWN && k->down)
        cs->hold_time++;
    else
    {
        if(k->left)
            cs->hold_dir = DAS_LEFT;
        else if(k->right)
            cs->hold_dir = DAS_RIGHT;
        else if(k->up)
            cs->hold_dir = DAS_UP;
        else if(k->down)
            cs->hold_dir = DAS_DOWN;
        else
            cs->hold_dir = DAS_NONE;

        cs->hold_time = 0;
    }
}

void update_pressed(coreState *cs)
{
    cs->pressed.left = (cs->keys.left == 1 && cs->prev_keys.left == 0) ? 1 : 0;
    cs->pressed.right = (cs->keys.right == 1 && cs->prev_keys.right == 0) ? 1 : 0;
    cs->pressed.up = (cs->keys.up == 1 && cs->prev_keys.up == 0) ? 1 : 0;
    cs->pressed.down = (cs->keys.down == 1 && cs->prev_keys.down == 0) ? 1 : 0;
    cs->pressed.start = (cs->keys.start == 1 && cs->prev_keys.start == 0) ? 1 : 0;
    cs->pressed.a = (cs->keys.a == 1 && cs->prev_keys.a == 0) ? 1 : 0;
    cs->pressed.b = (cs->keys.b == 1 && cs->prev_keys.b == 0) ? 1 : 0;
    cs->pressed.c = (cs->keys.c == 1 && cs->prev_keys.c == 0) ? 1 : 0;
    cs->pressed.d = (cs->keys.d == 1 && cs->prev_keys.d == 0) ? 1 : 0;
    cs->pressed.escape = (cs->keys.escape == 1 && cs->prev_keys.escape == 0) ? 1 : 0;
}

int button_emergency_inactive(coreState *cs)
{
    if(cs->button_emergency_override)
        return 0;
    else
        return 1;

    return 1;
}

int gfx_buttons_input(coreState *cs)
{
    if(!cs)
        return -1;

    if(!cs->gfx_buttons.size())
        return 1;

    int i = 0;

    int scaled_x = 0;
    int scaled_y = 0;
    int scaled_w = 0;
    int scaled_h = 0;

    int scale = 1;
    if(cs->settings)
    {
        scale = cs->settings->videoScale;
    }

    for (auto it = cs->gfx_buttons.begin(); it != cs->gfx_buttons.end(); it++) {
        gfx_button& b = *it;
        scaled_x = scale * b.x;
        scaled_y = scale * b.y;
        scaled_w = scale * b.w;
        scaled_h = scale * b.h;

        if(cs->button_emergency_override && !(b.flags & BUTTON_EMERGENCY))
        {
            b.highlighted = 0;
            if(b.delete_check)
            {
                if(b.delete_check(cs))
                {
                    cs->gfx_buttons.erase(it);
                }
            }

            continue;
        }

        if(cs->mouse_x < scaled_x + scaled_w && cs->mouse_x >= scaled_x && cs->mouse_y < scaled_y + scaled_h &&
           cs->mouse_y >= scaled_y)
            b.highlighted = 1;
        else
            b.highlighted = 0;

        if(b.highlighted && cs->mouse_left_down == BUTTON_PRESSED_THIS_FRAME)
        {
            if(b.action)
            {
                b.action(cs, b.data);
            }

            b.clicked = 4;
        }
        if (b.clicked) {
            b.clicked--;
        }

        if(b.delete_check && (!b.clicked || b.flags & BUTTON_EMERGENCY))
        {
            if(b.delete_check(cs))
            {
                cs->gfx_buttons.erase(it);
            }
        }
    }

    for (auto it = cs->gfx_buttons.begin(); it != cs->gfx_buttons.end(); it++) {
        gfx_button& b = cs->gfx_buttons[i];

        if(cs->button_emergency_override && !(b.flags & BUTTON_EMERGENCY))
        {
            if(b.delete_check)
            {
                if(b.delete_check(cs))
                {
                    cs->gfx_buttons.erase(it);
                }
            }

            continue;
        }

        if(b.delete_check && (!b.clicked || b.flags & BUTTON_EMERGENCY))
        {
            if(b.delete_check(cs))
            {
                cs->gfx_buttons.erase(it);
            }
        }
    }

    return 0;
}

int request_fps(coreState *cs, double fps)
{
    if(!cs)
        return -1;
    if(fps != 60.00 && fps != G1_FPS && fps != G2_FPS && fps != G3_FPS)
        return 1;

    cs->fps = fps;
    return 0;
}
