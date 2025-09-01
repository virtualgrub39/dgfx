#include <inttypes.h>
#include <lauxlib.h>
#include <limits.h>
#include <lua.h>
#include <lualib.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "ketopt.h"
#include "stb_image_write.h"

int do_time = 0;
char *input_path = NULL;
char *output_path = "output.bmp";
uint32_t w = 640, h = 480;
size_t j = 1;

typedef struct
{
    struct timespec start_time;
    uint32_t total_pixels;
    atomic_uint processed_pixels;
    atomic_uint last_reported_block;
    int update_interval;
} progress_timer_t;

static double
timespec_to_seconds (const struct timespec *t)
{
    return (double)t->tv_sec + (double)t->tv_nsec / 1e9;
}

void
timer_init (progress_timer_t *timer, uint32_t total_pixels)
{
    if (!do_time)
        return;

    clock_gettime (CLOCK_MONOTONIC, &timer->start_time);
    timer->total_pixels = total_pixels;
    atomic_store_explicit (&timer->processed_pixels, 0u, memory_order_relaxed);
    atomic_store_explicit (&timer->last_reported_block, 0u, memory_order_relaxed);

    int interval = (total_pixels / 100 > 1000) ? 1000 : (int)(total_pixels / 100);
    if (interval < 1)
        interval = 1;
    timer->update_interval = interval;

    printf ("Starting processing of %u pixels...\n", total_pixels);
}

void
timer_update (progress_timer_t *timer, uint32_t pixels_done)
{
    if (!do_time)
        return;

    unsigned prev = atomic_fetch_add_explicit (&timer->processed_pixels, pixels_done, memory_order_relaxed);
    unsigned now = prev + pixels_done;
    unsigned total = timer->total_pixels;
    int interval = timer->update_interval;

    unsigned prev_block = prev / interval;
    unsigned new_block = now / interval;

    if (new_block > prev_block || now >= total)
    {
        unsigned expected = prev_block;
        if (atomic_compare_exchange_strong_explicit (&timer->last_reported_block, &expected, new_block,
                                                     memory_order_acq_rel, memory_order_relaxed)
            || now >= total)
        {
            struct timespec cur;
            clock_gettime (CLOCK_MONOTONIC, &cur);
            double elapsed = timespec_to_seconds (&cur) - timespec_to_seconds (&timer->start_time);

            double processed = (double)now;
            double avg_time_per_pixel = (processed > 0.0) ? (elapsed / processed) : 0.0;
            double percent = (100.0 * processed) / (double)total;
            double eta = avg_time_per_pixel * (double)(total - now);

            printf ("\rProgress: %6.2f%% (%u/%u pixels) | %.2f μs/pixel | ETA: %.1fs | Elapsed: %.1fs", percent, now,
                    total, avg_time_per_pixel * 1e6, eta, elapsed);
            fflush (stdout);
        }
    }
}

void
timer_finish (progress_timer_t *timer)
{
    if (!do_time)
        return;

    struct timespec end;
    clock_gettime (CLOCK_MONOTONIC, &end);
    double total_elapsed = timespec_to_seconds (&end) - timespec_to_seconds (&timer->start_time);

    unsigned processed = atomic_load_explicit (&timer->processed_pixels, memory_order_relaxed);

    double avg_time_per_pixel = (processed > 0) ? (total_elapsed / (double)processed) : 0.0;
    double pixels_per_second = (total_elapsed > 0.0) ? (processed / total_elapsed) : 0.0;

    printf ("\n\nProcessing complete!\n");
    printf ("Total time: %.2f seconds\n", total_elapsed);
    printf ("Average time per pixel: %.2f μs\n", avg_time_per_pixel * 1e6);
    printf ("Pixels per second: %.0f\n", pixels_per_second);
}

progress_timer_t t = { 0 };

typedef struct
{
    uint8_t id;
    lua_State *L;
    pthread_t t;

    uint8_t *pixels;
    size_t pixels_count;
    size_t pixels_done;
    size_t start_idx;

    int rgb_ref;
} DgfxWorker;

int
dgfx_worker_init (DgfxWorker *worker, uint8_t idx, uint8_t *pixels, size_t start_idx, size_t pixel_count)
{
    worker->id = idx;

    lua_State *L = luaL_newstate ();
    if (!L)
    {
        fprintf (stderr, "Failed to create lua context.\n");
        return 0;
    }

    luaL_openlibs (L);
    lua_settop (L, 0);

    lua_newtable (L);
    lua_pushinteger (L, w);
    lua_setfield (L, -2, "w");
    lua_pushinteger (L, h);
    lua_setfield (L, -2, "h");
    lua_setglobal (L, "config");

    if (luaL_dofile (L, input_path) != LUA_OK)
    {
        fprintf (stderr, "Error while reading lua file: %s\n", lua_tostring (L, -1));
        lua_close (L);
        return 0;
    }

    lua_getglobal (L, "rgb");
    if (!lua_isfunction (L, -1))
    {
        fprintf (stderr, "Invalid `rgb` type. Expected function, got %s\n", lua_typename (L, lua_type (L, -1)));
        lua_close (L);
        return 0;
    }

    int ref = luaL_ref (L, LUA_REGISTRYINDEX);

    worker->L = L;
    worker->rgb_ref = ref;

    worker->pixels = pixels;
    worker->pixels_count = pixel_count;
    worker->pixels_done = 0;
    worker->start_idx = start_idx;

    return 1;
}

void *
dgfx_worker_work (void *arg)
{
    DgfxWorker *worker = arg;

    size_t x = worker->start_idx % w;
    size_t y = worker->start_idx / w;

    size_t written = 0;

    for (size_t yy = y; yy < (size_t)h && written < worker->pixels_count; ++yy)
    {
        size_t xx_start = (yy == y) ? x : 0;
        for (size_t xx = xx_start; xx < (size_t)w && written < worker->pixels_count; ++xx)
        {
            lua_rawgeti (worker->L, LUA_REGISTRYINDEX, worker->rgb_ref);

            lua_pushinteger (worker->L, xx);
            lua_pushinteger (worker->L, yy);

            if (lua_pcall (worker->L, 2, 3, 0) != LUA_OK)
            {
                fprintf (stderr, "Lua error in worker %u: %s\n", worker->id, lua_tostring (worker->L, -1));
                lua_pop (worker->L, 1);
                pthread_exit (NULL);
            }

            double r = lua_tonumberx (worker->L, -3, NULL);
            double g = lua_tonumberx (worker->L, -2, NULL);
            double b = lua_tonumberx (worker->L, -1, NULL);
            lua_pop (worker->L, 3);

            uint8_t ir = (uint8_t)((uint32_t)(fmax (0.0, fmin (1.0, r)) * 255.0 + 0.5));
            uint8_t ig = (uint8_t)((uint32_t)(fmax (0.0, fmin (1.0, g)) * 255.0 + 0.5));
            uint8_t ib = (uint8_t)((uint32_t)(fmax (0.0, fmin (1.0, b)) * 255.0 + 0.5));

            size_t idx = ((size_t)yy * w + xx) * 4;
            worker->pixels[idx + 0] = ir;
            worker->pixels[idx + 1] = ig;
            worker->pixels[idx + 2] = ib;
            worker->pixels[idx + 3] = 0xFF;

            timer_update (&t, 1); 
            ++written;
            worker->pixels_done = written;
        }
    }

    pthread_exit (NULL);
}

int
dgfx_worker_start (DgfxWorker *worker)
{
    int err = pthread_create (&worker->t, NULL, dgfx_worker_work, worker);
    if (err != 0)
        return 0;

    return 1;
}

int
dgfx_worker_join (DgfxWorker *worker)
{
    pthread_join (worker->t, NULL);

    luaL_unref (worker->L, LUA_REGISTRYINDEX, worker->rgb_ref);
    lua_close (worker->L);
    return worker->pixels_done == worker->pixels_count;
}

enum
{
    ARG_HELP = 256,
    // ARG_WIDTH,
    // ARG_HEIGTH,
    ARG_TIME,
    ARG_INPUT,
    ARG_OUTPUT,
};

const ko_longopt_t longopts[] = {
    { "help", ko_no_argument, ARG_HELP },
    { "time", ko_required_argument, ARG_TIME },
    { "input", ko_required_argument, ARG_INPUT },
    { "output", ko_required_argument, ARG_OUTPUT },
};

void
usage (const char *progname)
{
    printf ("Usage: %s [FLAGS] [ARGS]\n", progname);
    printf ("FLAGS:\n");
    printf ("\t-h, --help   - display this message.\n");
    printf ("\t-t, --time   - display timing information.\n");
    printf ("ARGS:\n");
    printf ("\t-W <integer> - specify output image width.  DEFAULT: 640\n");
    printf ("\t-H <integer> - specify output image height. DEFAULT: 480\n");
    printf ("\t-i, --input  - specify input lua file path.\n");
    printf ("\t-o, --output - specify output bitmap path.  DEFAULT: \"output.bmp\"\n");
    printf ("\t-j <integer> - specify number of threads to use for rendering. DEFAULT: 1\n");
}

int
main (int argc, char *argv[])
{
    ketopt_t s = KETOPT_INIT;
    int c = 0;
    const char *optstr = "hti:o:W:H:j:";

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
            endptr = NULL;
            w = strtoul (s.arg, &endptr, 10);
            if (endptr != s.arg + strlen (s.arg) || *endptr != 0)
            {
                fprintf (stderr, "Invalid width\n");
                return 1;
            }
            break;
        case 'H':
            endptr = NULL;
            h = strtoul (s.arg, &endptr, 10);
            if (endptr != s.arg + strlen (s.arg) || *endptr != 0)
            {
                fprintf (stderr, "Invalid height\n");
                return 1;
            }
            break;
        case 't':
        case ARG_TIME:
            do_time = 1;
            break;
        case 'i':
        case ARG_INPUT:
            input_path = s.arg;
            break;
        case 'j':
            endptr = NULL;
            j = strtoul (s.arg, &endptr, 10);
            if (endptr != s.arg + strlen (s.arg) || *endptr != 0)
            {
                fprintf (stderr, "Invalid job count\n");
                return 1;
            }
            if (j == 0) j = 1;
            break;
        break;
        case 'o':
        case ARG_OUTPUT:
            output_path = s.arg;
            break;
        case '?':
            fprintf (stderr, "Unknown option: %s\n", argv[s.ind - 1]);
            return 1;
        case ':':
            fprintf (stderr, "Option requires an argument: %s\n", argv[s.ind - 1]);
            return 1;
        }
    }

    if (!input_path)
    {
        fprintf (stderr, "No input provided.\n");
        return 0;
    }

    timer_init (&t, w * h);

    size_t pixel_count = w * h;
    uint8_t *pixels = calloc (pixel_count * 4, sizeof (uint8_t));
    if (!pixels)
    {
        perror ("malloc");
        return 1;
    }

    DgfxWorker *workers = calloc(j, sizeof(DgfxWorker));
    size_t num_workers = j;

    for (uint8_t i = 0; i < num_workers; ++i)
    {
        size_t per = pixel_count / num_workers;
        size_t start = i * per;
        if (!dgfx_worker_init (&workers[i], i, pixels, start, per))
        {
            perror ("dgfx_worker_init");
            free (pixels);
            return 1;
        }
        if (!dgfx_worker_start (&workers[i]))
        {
            perror ("dgfx_worker_start");
            free (pixels);
            return 1;
        }
    }

    for (uint8_t i = 0; i < num_workers; ++i)
    {
        dgfx_worker_join (&workers[i]);
    }

    timer_finish (&t);

    if (!stbi_write_bmp (output_path, w, h, 4, pixels))
    {
        fprintf (stderr, "Failed to write image to %s\n", output_path);
    }

    free (pixels);
    return 0;
}
