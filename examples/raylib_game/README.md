# beerlang + raylib starfield demo

A port of the [Clojure + libGDX game skeleton](https://github.com/nvlass/game-skeleton) —
beerlang drives everything: main loop, state, drawing, input.
Raylib owns only the window and the graphics context.

```
C main()
  └─ beer_open() + register rl/* natives
  └─ InitWindow(800, 600, ...)
  └─ beer_lookup("game/init!") → call once
  └─ loop: beer_call("game/tick", dt)   ← beerlang drives every frame
```

## Dependencies

| Dependency | Install |
|------------|---------|
| beerlang (libbeerlang.a) | `make libbeerlang` in the repo root |
| raylib | `brew install raylib` (macOS) / `apt install libraylib-dev` (Linux) |

## Build & run

```bash
# 1. Build libbeerlang (from repo root, once)
cd ../..
make libbeerlang

# 2. Build and run the game
cd examples/raylib_game
make run
```

`make run` sets `BEERPATH` so the game can find the beerlang standard library.

## Controls

| Key | Action |
|-----|--------|
| Up arrow | Increase star speed |
| Down arrow | Decrease star speed |
| Close window / Escape | Quit |

## Live REPL (optional)

The network REPL block at the bottom of `game.beer` is commented out.
Uncomment it to start a TCP REPL on port 7888:

```clojure
(require 'beer.tcp)
(spawn (fn [] (repl-server 7888)))
```

Connect from another terminal:

```bash
nc localhost 7888
```

Then modify game state live:

```clojure
(swap! game/state assoc :star-speed 10)
(def game/screen-fnmap (assoc game/screen-fnmap :render my-new-render))
```

> **Thread safety**: REPL tasks run on scheduler worker threads.
> Only `swap!` atoms from the REPL — never call `rl/*` directly.
> Raylib requires the main OS thread; the next frame's `tick` picks up
> the new atom state safely.

## Architecture

```
game.beer          — all game logic (state, update, draw)
rl.beer            — stub ns so (require 'rl) resolves
main.c             — thin coupling layer (the only file that knows about both)
```

### Why this split?

- **C owns**: window lifecycle, the frame loop, native registration
- **Beerlang owns**: all game state (atoms), update logic, draw calls
- **Hot-swap**: redefine `game/screen-fnmap` from the REPL; `OP_LOAD_VAR`
  always fetches the current var, so the next frame picks it up instantly

This mirrors the Clojure + libGDX pattern:
atoms for state, a map of keyword→fn for the screen lifecycle,
and a network REPL for live modification — no restart needed.
