# DGFX

![gallery/example_animated.gif](gallery/example_animated.gif)

Tool, that allows You to write simple procedural graphics in Lua.

See [gallery](<gallery/gallery.md>)

## Building

**This project depends on: `lua-jit` (5.1), `sdl3` and `sdl3-ttf`** (and all of their dependencies) - most of which should be available in Your package manager. `pkg-config` is used to search for them.

Building is as simple as running `make` in root of the project.

This will generate `dgfx` executable. **WARNING** - this executable depends on everything in `resources` directory **at runtime**.

## Usage

**WIP** - use `dgfx --help`, [examples](examples) and see [config header](confgi.def.h) for now :3

## Notes

At this point I think I've optimized it as much as possible, without making the project too complicated.

The next step would probably involve transpiling lua into actual shader language, to make use of GPU. I won't be doing that (for now :3).

I've experimented with `ffi` to eliminate memory copying, but it made program much slower for some reason. Leaving everything to C compiler and lua-jit resulted in the best performance.

This project is a toy - a challenge to create a fast lua -> C integration - not a serious project with many usecases.

## License

See [LICENSE](LICENSE)

