#include <lauxlib.h>
#include <limits.h>
#include <lua.h>
#include <lualib.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

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
    clock_t end_time = clock ();
    double total_elapsed = (double)(end_time - timer->start_time) / CLOCKS_PER_SEC;

    printf ("\n\nProcessing complete!\n");
    printf ("Total time: %.2f seconds\n", total_elapsed);
    printf ("Average time per pixel: %.2f μs\n", timer->avg_time_per_pixel * 1000000.0);
    printf ("Pixels per second: %.0f\n", timer->processed_pixels / total_elapsed);
}

int
main (int argc, char *argv[])
{
    if (argc != 4)
    {
        fprintf (stderr, "Usage: %s <width> <height> <lua file path>\n", argv[0]);
        return 1;
    }

    char *endptr = NULL;
    uint32_t w = strtoul (argv[1], &endptr, 10);
    if (endptr != argv[1] + strlen (argv[1]) || *endptr != 0)
    {
        fprintf (stderr, "Invalid width\n");
        return 1;
    }

    endptr = NULL;
    uint32_t h = strtoul (argv[2], &endptr, 10);
    if (endptr != argv[2] + strlen (argv[2]) || *endptr != 0)
    {
        fprintf (stderr, "Invalid height\n");
        return 1;
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

    if (luaL_dofile (L, argv[3]) != LUA_OK)
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

    if (!stbi_write_bmp ("output.bmp", w, h, 4, pixels))
    {
        fprintf (stderr, "Failed to write output.bmp");
    }

    free (pixels);
    lua_close (L);
    return 0;
}
