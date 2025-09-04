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

## License

See [LICENSE](LICENSE)

