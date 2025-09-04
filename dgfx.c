#include <inttypes.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <time.h>

#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_ttf/SDL_ttf.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#define STB_DS_IMPLEMENTATION
#include <stb_ds.h>

#include <ketopt.h>

#include "config.h"

#define SARRLEN(arr) (sizeof (arr) / sizeof (arr[0]))
#define UNREACHABLE                                                                                                    \
    do                                                                                                                 \
    {                                                                                                                  \
        fprintf (stderr, "ENTERED UNREACHABLE PART OF CODE! Aborting...\n");                                           \
        abort ();                                                                                                      \
    } while (0)

enum
{
    MODE_SINGLE = 0,
    MODE_REALTIME,
};
const char *_mode_strings[] = { [MODE_SINGLE] = "single", [MODE_REALTIME] = "realtime" };

struct
{
    size_t w, h;
    int mode;
    const char *input_path;
    const char *output_path;
    size_t worker_n;
} dgfx_config = { .w = DGFX_RESOLUTION_W_DEFUALT,
                  .h = DGFX_RESOLUTION_H_DEFAULT,
                  .mode = 0,
                  .input_path = NULL,
                  .output_path = DGFX_OUTPUT_PATH_DEFAULT,
                  .worker_n = 1 };

struct dgfx_worker
{
    uint8_t id;
    pthread_t thrd;
    lua_State *L;

    uint8_t **pixels; // points to dgfx_ctx.pixels
    size_t start_idx;
    size_t work_len;  // pixel count assigned to worker
    size_t work_done; // count of pixels already done

    double t_param;

    int lua_cb_ref;

    pthread_mutex_t mutex;
    pthread_cond_t work_cond;
    pthread_cond_t done_cond;

    bool has_work;
    bool work_complete;
    bool should_exit;
    bool thread_running;
};

void *
dgfx_worker_work (void *arg)
{
    struct dgfx_worker *w = arg;

    while (true)
    {
        pthread_mutex_lock (&w->mutex);

        while (!w->has_work && !w->should_exit)
        {
            pthread_cond_wait (&w->work_cond, &w->mutex);
        }

        if (w->should_exit)
        {
            pthread_mutex_unlock (&w->mutex);
            break;
        }

        w->work_complete = false;
        w->work_done = 0;
        pthread_mutex_unlock (&w->mutex);

        lua_rawgeti (w->L, LUA_REGISTRYINDEX, w->lua_cb_ref);

        lua_pushnumber (w->L, (lua_Number)w->t_param);

        if (lua_pcall (w->L, 1, 1, 0) != LUA_OK)
        {
            fprintf (stderr, "Lua error in worker %u: %s\n", w->id, lua_tostring (w->L, -1));
            lua_pop (w->L, 1);
            pthread_exit (NULL);
        }

        size_t ret_len = 0;
        const char *buf = lua_tolstring (w->L, -1, &ret_len);
        uint8_t *pixels = *(w->pixels);

        size_t expected = w->work_len * 4;
        if (ret_len != expected)
        {
            size_t copy_len = ret_len < expected ? ret_len : expected;
            size_t byte_offset = w->start_idx * 4;
            memcpy (pixels + byte_offset, buf, copy_len);
            if (copy_len < expected)
                memset (pixels + byte_offset + copy_len, 0, expected - copy_len);
        }
        else
        {
            size_t byte_offset = w->start_idx * 4;
            memcpy (pixels + byte_offset, buf, expected);
        }

        lua_pop (w->L, 1);

        pthread_mutex_lock (&w->mutex);
        w->work_done = w->work_len;
        w->has_work = false;
        w->work_complete = true;
        pthread_cond_signal (&w->done_cond);
        pthread_mutex_unlock (&w->mutex);
    }

    pthread_exit (NULL);
}

bool
dgfx_worker_init (struct dgfx_worker *w, uint8_t id, uint8_t **pixels, size_t start_idx, size_t work_len)
{
    if (!w || !pixels)
        UNREACHABLE;

    w->id = id;
    w->pixels = pixels;
    w->start_idx = start_idx;
    w->work_len = work_len;

    if (pthread_mutex_init (&w->mutex, NULL) != 0)
    {
        return false;
    }

    if (pthread_cond_init (&w->work_cond, NULL) != 0)
    {
        pthread_mutex_destroy (&w->mutex);
        return false;
    }

    if (pthread_cond_init (&w->done_cond, NULL) != 0)
    {
        pthread_cond_destroy (&w->work_cond);
        pthread_mutex_destroy (&w->mutex);
        return false;
    }

    w->has_work = false;
    w->work_complete = false;
    w->should_exit = false;
    w->thread_running = false;

    w->L = luaL_newstate ();
    if (!w->L)
        return false;
    luaL_openlibs (w->L);

    lua_newtable (w->L); // dgfx

    lua_pushinteger (w->L, dgfx_config.w);
    lua_setfield (w->L, -2, "width");

    lua_pushinteger (w->L, dgfx_config.h);
    lua_setfield (w->L, -2, "height");

    lua_newtable (w->L); // worker

    lua_pushinteger (w->L, start_idx);
    lua_setfield (w->L, -2, "start");

    lua_pushinteger (w->L, work_len);
    lua_setfield (w->L, -2, "len");

    lua_pushinteger (w->L, id);
    lua_setfield (w->L, -2, "id");

    lua_setfield (w->L, -2, "worker");

    lua_setglobal (w->L, "dgfx");

    // fprintf(stdout, "id: %u, start: %lu, len: %lu\n", id, start_idx, work_len);

    if (luaL_loadfile (w->L, dgfx_config.input_path) != 0)
    {
        fprintf (stderr, "lua load error: %s\n", lua_tostring (w->L, -1));
        goto dgfx_worker_init_oopsie;
    }

    if (lua_pcall (w->L, 0, 0, 0) != 0)
    {
        fprintf (stderr, "lua runtime error: %s\n", lua_tostring (w->L, -1));
        goto dgfx_worker_init_oopsie;
    }

    lua_getglobal (w->L, "rgb");
    if (!lua_isfunction (w->L, -1))
    {
        fprintf (stderr, "lua user script must define rgb(n,m,t) function\n");
        goto dgfx_worker_init_oopsie;
    }
    lua_pop (w->L, 1);

    if (luaL_loadfile (w->L, DGFX_RESOURCE_LUA_WORKER_CB) != 0)
    {
        fprintf (stderr, "lua load error: %s\n", lua_tostring (w->L, -1));
        goto dgfx_worker_init_oopsie;
    }

    if (lua_pcall (w->L, 0, 0, 0) != 0)
    {
        fprintf (stderr, "lua runtime error: %s\n", lua_tostring (w->L, -1));
        goto dgfx_worker_init_oopsie;
    }

    lua_getglobal (w->L, "__dgfx_worker_cb");
    if (!lua_isfunction (w->L, -1))
    {
        fprintf (stderr, "lua script must define __dgfx_worker_cb\n");
        goto dgfx_worker_init_oopsie;
    }
    w->lua_cb_ref = luaL_ref (w->L, LUA_REGISTRYINDEX);

    int err = pthread_create (&w->thrd, NULL, dgfx_worker_work, w);
    if (err != 0)
        goto dgfx_worker_init_oopsie;

    w->thread_running = true;

    return true;

dgfx_worker_init_oopsie:
    lua_close (w->L);
    pthread_cond_destroy (&w->done_cond);
    pthread_cond_destroy (&w->work_cond);
    pthread_mutex_destroy (&w->mutex);
    return false;
}

bool
dgfx_worker_start_work (struct dgfx_worker *w, double cur_t)
{
    pthread_mutex_lock (&w->mutex);

    if (!w->thread_running)
    {
        pthread_mutex_unlock (&w->mutex);
        return false;
    }

    w->t_param = cur_t;
    w->has_work = true;
    pthread_cond_signal (&w->work_cond);

    pthread_mutex_unlock (&w->mutex);
    return true;
}

bool
dgfx_worker_wait_completion (struct dgfx_worker *w)
{
    pthread_mutex_lock (&w->mutex);

    while (!w->work_complete && w->thread_running)
    {
        pthread_cond_wait (&w->done_cond, &w->mutex);
    }

    bool success = w->work_complete && (w->work_done == w->work_len);
    pthread_mutex_unlock (&w->mutex);

    return success;
}

void
dgfx_worker_shutdown (struct dgfx_worker *w)
{
    if (!w->thread_running)
    {
        return;
    }

    pthread_mutex_lock (&w->mutex);
    w->should_exit = true;
    pthread_cond_signal (&w->work_cond);
    pthread_mutex_unlock (&w->mutex);

    pthread_join (w->thrd, NULL);
    w->thread_running = false;

    luaL_unref (w->L, LUA_REGISTRYINDEX, w->lua_cb_ref);
    lua_close (w->L);

    pthread_cond_destroy (&w->done_cond);
    pthread_cond_destroy (&w->work_cond);
    pthread_mutex_destroy (&w->mutex);
}

struct
{
    uint8_t *pixels;
    struct dgfx_worker *workers;
} dgfx_ctx = {
    .workers = NULL,
    .pixels = NULL,
};

bool
dgfx_init (uint8_t *init_pixels)
{
    dgfx_ctx.pixels = init_pixels;

    size_t total_pixels = dgfx_config.w * dgfx_config.h;
    size_t n_workers = dgfx_config.worker_n ? dgfx_config.worker_n : 1;

    arrsetcap (dgfx_ctx.workers, n_workers);

    for (size_t i = 0; i < n_workers; ++i)
    {
        size_t per = total_pixels / n_workers;
        size_t start = i * per;
        if (i == n_workers - 1)
            per = total_pixels - start;

        arrput (dgfx_ctx.workers, (struct dgfx_worker){ 0 });
        struct dgfx_worker *w = &dgfx_ctx.workers[arrlenu (dgfx_ctx.workers) - 1];

        if (!dgfx_worker_init (w, (uint8_t)i, &dgfx_ctx.pixels, start, per))
        {
            for (size_t j = 0; j < arrlenu (dgfx_ctx.workers); ++j)
                dgfx_worker_shutdown (&dgfx_ctx.workers[j]);

            arrfree (dgfx_ctx.workers);
            return false;
        }
    }

    return true;
}

uint8_t *
dgfx_pixels_set (void *new)
{
    if (!new)
        UNREACHABLE;

    uint8_t *t = dgfx_ctx.pixels;
    dgfx_ctx.pixels = new;
    return t;
}

void
dgfx_deinit (void)
{
    for (size_t i = 0; i < arrlenu (dgfx_ctx.workers); ++i)
    {
        dgfx_worker_shutdown (&dgfx_ctx.workers[i]);
    }

    arrfree (dgfx_ctx.workers);
}

bool
dgfx_doframe (double cur_t)
{
    for (uint8_t i = 0; i < dgfx_config.worker_n; ++i)
    {
        lua_gc (dgfx_ctx.workers[i].L, LUA_GCSTOP, 1);

        if (!dgfx_worker_start_work (&dgfx_ctx.workers[i], cur_t))
        {
            fprintf (stderr, "Failed to start work for worker %u\n", i);
            return false;
        }

        lua_gc (dgfx_ctx.workers[i].L, LUA_GCRESTART, 1);
    }

    for (uint8_t i = 0; i < dgfx_config.worker_n; ++i)
    {
        if (!dgfx_worker_wait_completion (&dgfx_ctx.workers[i]))
        {
            fprintf (stderr, "Worker %u failed to complete work\n", i);
            return false;
        }
    }

    return true;
}

void
dgfx_sdl_loop (void)
{
    if (!SDL_Init (SDL_INIT_VIDEO))
    {
        fprintf (stderr, "SDL_Init failed: %s\n", SDL_GetError ());
        return;
    }

    if (!TTF_Init ())
    {
        fprintf (stderr, "TTF_Init failed: %s\n", SDL_GetError ());
        SDL_Quit ();
        return;
    }

    SDL_Window *window = SDL_CreateWindow ("dgfx", dgfx_config.w, dgfx_config.h, 0);
    SDL_Renderer *renderer = SDL_CreateRenderer (window, NULL);
    SDL_Texture *texture = SDL_CreateTexture (renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING,
                                              dgfx_config.w, dgfx_config.h);

    TTF_Font *font = TTF_OpenFont (DGFX_RESOURCE_FONT, 24);
    if (!font)
    {
        fprintf (stderr, "TTF_OpenFont failed: %s\n", SDL_GetError ());
    }

    bool running = true;
    uint32_t frame_count = 0;
    uint32_t last_fps_time = SDL_GetTicks ();
    float fps = 0.0f;
    SDL_Texture *fps_texture = NULL;

    while (running)
    {
        SDL_Event event;
        while (SDL_PollEvent (&event))
        {
            if (event.type == SDL_EVENT_QUIT
                || (event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_ESCAPE))
            {
                running = false;
            }
        }

        uint32_t now = SDL_GetTicks ();
        double t = now / 1000.0;

        frame_count++;
        if (now - last_fps_time >= 200)
        {
            fps = frame_count * 1000.0f / (now - last_fps_time);
            frame_count = 0;
            last_fps_time = now;

            if (fps_texture)
            {
                SDL_DestroyTexture (fps_texture);
                fps_texture = NULL;
            }

            if (font)
            {
                char fps_text[32];
                snprintf (fps_text, sizeof (fps_text), "FPS: %.1f", fps);

                SDL_Color bright_color = { 255, 255, 0, 255 };
                SDL_Surface *fps_surface = TTF_RenderText_Solid (font, fps_text, 0, bright_color);
                if (fps_surface)
                {
                    fps_texture = SDL_CreateTextureFromSurface (renderer, fps_surface);
                    SDL_DestroySurface (fps_surface);
                }
            }
        }

        void *locked_ptr = NULL;
        int row_stride = 0;
        if (!SDL_LockTexture (texture, NULL, &locked_ptr, &row_stride))
        {
            fprintf (stderr, "SDL_LockTexture failed: %s\n", SDL_GetError ());
            break;
        }

        dgfx_pixels_set (locked_ptr);

        dgfx_doframe (t);

        SDL_UnlockTexture (texture);

        SDL_RenderClear (renderer);
        if (!SDL_RenderTexture (renderer, texture, NULL, NULL))
        {
            fprintf (stderr, "SDL_RenderTexture failed: %s\n", SDL_GetError ());
        }

        if (fps_texture)
        {
            SDL_FRect fps_rect = { 10, 10, 0, 0 };
            SDL_GetTextureSize (fps_texture, &fps_rect.w, &fps_rect.h);
            SDL_RenderTexture (renderer, fps_texture, NULL, &fps_rect);
        }

        SDL_RenderPresent (renderer);
    }

    if (font)
        TTF_CloseFont (font);
    if (texture)
        SDL_DestroyTexture (texture);
    if (renderer)
        SDL_DestroyRenderer (renderer);
    if (window)
        SDL_DestroyWindow (window);
    SDL_Quit ();
}

enum
{
    ARG_HELP = 256,
    ARG_WIDTH,
    ARG_HEIGHT,
    ARG_INPUT,
    ARG_OUTPUT,
    ARG_MODE,
    ARG_JOBS,
};

const ko_longopt_t longopts[]
    = { { "help", ko_no_argument, ARG_HELP },           { "mode", ko_required_argument, ARG_MODE },
        { "input", ko_required_argument, ARG_INPUT },   { "output", ko_required_argument, ARG_OUTPUT },
        { "jobs", ko_required_argument, ARG_JOBS },     { "width", ko_required_argument, ARG_WIDTH },
        { "heigth", ko_required_argument, ARG_HEIGHT }, { NULL, 0, 0 } };

void
usage (const char *progname)
{
    printf ("Usage: %s [FLAGS] [ARGS]\n", progname);
    printf ("FLAGS:\n");
    printf ("\t-h, --help   - display this message.\n");
    printf ("ARGS:\n");
    printf ("\t-W, --width  <integer> - specify output image width.                     DEFAULT: %u\n",
            DGFX_RESOLUTION_W_DEFUALT);
    printf ("\t-H, --height <integer> - specify output image height.                    DEFAULT: %u\n",
            DGFX_RESOLUTION_H_DEFAULT);
    printf ("\t-i, --input  <path>    - specify input lua file path.\n");
    printf ("\t-o, --output <path>    - specify output bitmap path.                     DEFAULT: "
            "\"" DGFX_OUTPUT_PATH_DEFAULT "\"\n");
    printf ("\t-j, --jobs   <integer> - specify number of threads to use for rendering. DEFAULT: 1\n");
    printf ("\t-m, --mode   <MODE>    - specify output mode.                            DEFAULT: %s\n",
            _mode_strings[0]);
    printf ("MODE:\n");
    printf ("\tsingle   - program outputs single frame, with t=0.0, to bitmap.\n");
    printf (
        "\trealtime - program displays pixels in SDL3 window, passing time from window creation in seconds to t.\n");
}

int
main (int argc, char *argv[])
{
    ketopt_t s = KETOPT_INIT;
    int c = 0;
    const char *optstr = "hi:o:W:H:j:m:";

    char *endptr = NULL;

    while ((c = ketopt (&s, argc, argv, 1, optstr, longopts)) != -1)
    {
        switch (c)
        {
        case 'h':
        case ARG_HELP:
            usage (argv[0]);
            return 0;
        case 'W':
        case ARG_WIDTH:
            endptr = NULL;
            dgfx_config.w = strtoul (s.arg, &endptr, 10);
            if (endptr != s.arg + strlen (s.arg) || *endptr != 0)
            {
                fprintf (stderr, "Invalid width\n");
                return 1;
            }
            break;
        case 'H':
        case ARG_HEIGHT:
            endptr = NULL;
            dgfx_config.h = strtoul (s.arg, &endptr, 10);
            if (endptr != s.arg + strlen (s.arg) || *endptr != 0)
            {
                fprintf (stderr, "Invalid height\n");
                return 1;
            }
            break;
        case 'm':
        case ARG_MODE:
            bool mode_found = false;

            for (int i = 0; i < (int)SARRLEN (_mode_strings); ++i)
            {
                if (strcasecmp (s.arg, _mode_strings[i]) == 0)
                {
                    dgfx_config.mode = i;
                    mode_found = true;
                    break;
                }
            }

            if (!mode_found)
            {
                fprintf (stderr, "Invalid mode: %s", s.arg);
                return 1;
            }

            break;
        case 'i':
        case ARG_INPUT:
            dgfx_config.input_path = s.arg;
            break;
        case 'j':
        case ARG_JOBS:
            endptr = NULL;
            dgfx_config.worker_n = strtoul (s.arg, &endptr, 10);
            if (endptr != s.arg + strlen (s.arg) || *endptr != 0 || dgfx_config.worker_n == 0)
            {
                fprintf (stderr, "Invalid job count\n");
                return 1;
            }
            break;
        case 'o':
        case ARG_OUTPUT:
            dgfx_config.output_path = s.arg;
            break;
        case '?':
            fprintf (stderr, "Unknown option: %s\n", argv[s.ind]);
            return 1;
        case ':':
            fprintf (stderr, "Option requires an argument: %s\n", argv[s.ind]);
            return 1;
        }
    }

    if (!dgfx_config.input_path)
    {
        fprintf (stderr, "No input file provided. Exiting\n");
        return 0;
    }

    if (!dgfx_init (NULL))
        return 1;

    switch (dgfx_config.mode)
    {
    case MODE_SINGLE: {
        uint32_t *pixels = malloc (dgfx_config.h * dgfx_config.w * sizeof (uint32_t));
        if (!pixels)
        {
            perror ("malloc");
            return 1;
        }
        dgfx_pixels_set ((uint8_t *)pixels);

        if (!dgfx_doframe (0))
        {
            free (pixels);

            fprintf (stderr, "frame generation failed\n");
            return 1;
        }

        if (!stbi_write_bmp (dgfx_config.output_path, dgfx_config.w, dgfx_config.h, 4, pixels))
        {
            fprintf (stderr, "Failed to write image to %s\n", dgfx_config.output_path);
        }

        free (pixels);
    }
    break;
    case MODE_REALTIME: {
        if (strcmp (dgfx_config.output_path, DGFX_OUTPUT_PATH_DEFAULT) != 0)
        {
            fprintf (stderr, "Warning: \"output\" argument is only intended for use in single mode. It's ignored in "
                             "realtime mode.\n");
        }

        dgfx_sdl_loop ();
    }
    break;
    default:
        UNREACHABLE;
    }

    dgfx_deinit ();

    return 0;
}
