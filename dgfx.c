#include <lauxlib.h>
#include <limits.h>
#include <lua.h>
#include <lualib.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "ketopt.h"
#include "stb_image_write.h"

int do_time = 0;

typedef struct
{
    clock_t start_time;
    clock_t last_update;
    uint32_t total_pixels;
    uint32_t processed_pixels;
    double avg_time_per_pixel;
    int update_interval;
} progress_timer_t;

void
timer_init (progress_timer_t *timer, uint32_t total_pixels)
{
    if (!do_time)
        return;

    timer->start_time = clock ();
    timer->last_update = timer->start_time;
    timer->total_pixels = total_pixels;
    timer->processed_pixels = 0;
    timer->avg_time_per_pixel = 0.0;
    timer->update_interval = (total_pixels / 100 > 1000) ? 1000 : total_pixels / 100;
    if (timer->update_interval < 1)
        timer->update_interval = 1;

    printf ("Starting processing of %u pixels...\n", total_pixels);
}

void
timer_update (progress_timer_t *timer, uint32_t pixels_done)
{
    if (!do_time)
        return;

    timer->processed_pixels += pixels_done;

    if (timer->processed_pixels % timer->update_interval == 0 || timer->processed_pixels == timer->total_pixels)
    {

        clock_t current_time = clock ();
        double elapsed = (double)(current_time - timer->start_time) / CLOCKS_PER_SEC;

        if (timer->processed_pixels > 0)
        {
            timer->avg_time_per_pixel = elapsed / timer->processed_pixels;

            double percent = (100.0 * timer->processed_pixels) / timer->total_pixels;
            double eta = timer->avg_time_per_pixel * (timer->total_pixels - timer->processed_pixels);

            printf ("\rProgress: %6.2f%% (%u/%u pixels) | "
                    "%.2f μs/pixel | ETA: %.1fs | Elapsed: %.1fs",
                    percent, timer->processed_pixels, timer->total_pixels, timer->avg_time_per_pixel * 1000000.0, eta,
                    elapsed);
            fflush (stdout);
        }
    }
}

void
timer_finish (progress_timer_t *timer)
{
    if (!do_time)
        return;

    clock_t end_time = clock ();
    double total_elapsed = (double)(end_time - timer->start_time) / CLOCKS_PER_SEC;

    printf ("\n\nProcessing complete!\n");
    printf ("Total time: %.2f seconds\n", total_elapsed);
    printf ("Average time per pixel: %.2f μs\n", timer->avg_time_per_pixel * 1000000.0);
    printf ("Pixels per second: %.0f\n", timer->processed_pixels / total_elapsed);
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
}

int
main (int argc, char *argv[])
{
    ketopt_t s = KETOPT_INIT;
    int c = 0;
    const char *optstr = "hti:o:W:H:";

    char *input_path = NULL;
    char *output_path = "output.bmp";

    uint32_t w = 640, h = 480;

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

    progress_timer_t t = { 0 };
    timer_init (&t, w * h);

    lua_State *L = luaL_newstate ();
    if (!L)
    {
        fprintf (stderr, "Failed to create lua context.\n");
        return 1;
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
        return 1;
    }

    lua_getglobal (L, "rgb");
    if (!lua_isfunction (L, -1))
    {
        fprintf (stderr, "Invalid `rgb` type. Expected function, got %s\n", lua_typename (L, lua_type (L, -1)));
        lua_close (L);
        return 1;
    }

    // int func_ref = luaL_ref (L, LUA_REGISTRYINDEX); // pops

    uint8_t *pixels = calloc (w * h * 4, sizeof (uint8_t));
    if (!pixels)
    {
        perror ("malloc");
        lua_close (L);
        return 1;
    }

    for (uint32_t m = 0; m < h; ++m)
    {
        for (uint32_t n = 0; n < w; ++n)
        {
            // lua_rawgeti (L, LUA_REGISTRYINDEX, func_ref);
            lua_pushvalue (L, -1); // no noticable change vs registry

            lua_pushinteger (L, n);
            lua_pushinteger (L, m);

            lua_call (L, 2, 3); // unsafe - will crash if function is invalid

            double r = lua_tonumberx (L, -3, NULL);
            double g = lua_tonumberx (L, -2, NULL);
            double b = lua_tonumberx (L, -1, NULL);
            lua_pop (L, 3);

            uint8_t ir = (uint32_t)(fmax (0.0, fmin (1.0, r)) * 255.0 + 0.5);
            uint8_t ig = (uint32_t)(fmax (0.0, fmin (1.0, g)) * 255.0 + 0.5);
            uint8_t ib = (uint32_t)(fmax (0.0, fmin (1.0, b)) * 255.0 + 0.5);

            size_t idx = ((size_t)m * w + n) * 4;
            pixels[idx + 0] = ir;
            pixels[idx + 1] = ig;
            pixels[idx + 2] = ib;
            pixels[idx + 3] = 0xFF;

            timer_update (&t, 1);
        }
    }

    timer_finish (&t);

    // luaL_unref (L, LUA_REGISTRYINDEX, func_ref);

    if (!stbi_write_bmp (output_path, w, h, 4, pixels))
    {
        fprintf (stderr, "Failed to write image to %s\n", output_path);
    }

    free (pixels);
    lua_close (L);
    return 0;
}
