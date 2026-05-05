/*
 * examples/raylib_game/main.c
 *
 * Thin coupling layer between beerlang and raylib.
 * This file is the ONLY place that knows about both — game logic lives
 * entirely in game.beer; the rl/* native namespace is registered here.
 *
 * Build:   make          (see Makefile)
 * Run:     make run      (sets BEERPATH so core library is found)
 *
 * Architecture:
 *   - C owns: window init, the frame loop, and native registration
 *   - Beerlang owns: all game logic, state (atoms), update, draw
 *   - Communication: beer_call(B, tick, dt) once per frame
 *
 * Live REPL (future):
 *   Spawn a beer.tcp REPL server before the game loop.
 *   REPL tasks only touch atoms — never call rl/* directly.
 *   The next frame's tick() picks up the new atom state safely.
 *   (Requires namespace registry to be thread-safe — planned.)
 */

#include <stdio.h>
#include <string.h>
#include "beer.h"
#include "raylib.h"

/* ------------------------------------------------------------------ */
/* Color helper                                                        */
/* ------------------------------------------------------------------ */

/* Accept a beerlang vector [r g b a] (fixnums 0-255).
 * Falls back to WHITE for anything unrecognised. */
static Color beer_to_color(BeerValue v) {
    if (beer_is_vec(v) && beer_length(v) == 4) {
        return (Color){
            (uint8_t)beer_to_int(beer_nth(v, 0)),
            (uint8_t)beer_to_int(beer_nth(v, 1)),
            (uint8_t)beer_to_int(beer_nth(v, 2)),
            (uint8_t)beer_to_int(beer_nth(v, 3)),
        };
    }
    if (beer_is_keyword(v)) {
        const char* kw = beer_to_cstring(v);
        if (!strcmp(kw, "black"))   return BLACK;
        if (!strcmp(kw, "white"))   return WHITE;
        if (!strcmp(kw, "red"))     return RED;
        if (!strcmp(kw, "green"))   return GREEN;
        if (!strcmp(kw, "blue"))    return BLUE;
        if (!strcmp(kw, "yellow"))  return YELLOW;
        if (!strcmp(kw, "magenta")) return MAGENTA;
        if (!strcmp(kw, "raywhite"))return RAYWHITE;
        if (!strcmp(kw, "darkgray"))return DARKGRAY;
        if (!strcmp(kw, "gray"))    return GRAY;
    }
    return WHITE;
}

/* ------------------------------------------------------------------ */
/* rl/* native functions                                              */
/* ------------------------------------------------------------------ */

/* (rl/begin-drawing) */
static BeerValue rl_begin_drawing(BeerVM* vm, int argc, BeerValue* argv) {
    (void)vm; (void)argc; (void)argv;
    BeginDrawing();
    return beer_nil();
}

/* (rl/end-drawing) */
static BeerValue rl_end_drawing(BeerVM* vm, int argc, BeerValue* argv) {
    (void)vm; (void)argc; (void)argv;
    EndDrawing();
    return beer_nil();
}

/* (rl/clear-background color) */
static BeerValue rl_clear_background(BeerVM* vm, int argc, BeerValue* argv) {
    (void)vm;
    ClearBackground(argc >= 1 ? beer_to_color(argv[0]) : BLACK);
    return beer_nil();
}

/* (rl/draw-circle x y radius color) */
static BeerValue rl_draw_circle(BeerVM* vm, int argc, BeerValue* argv) {
    (void)vm;
    if (argc < 4) return beer_nil();
    DrawCircle((int)beer_to_int(argv[0]),
               (int)beer_to_int(argv[1]),
               (float)beer_to_double(argv[2]),
               beer_to_color(argv[3]));
    return beer_nil();
}

/* (rl/draw-rectangle x y w h color) */
static BeerValue rl_draw_rectangle(BeerVM* vm, int argc, BeerValue* argv) {
    (void)vm;
    if (argc < 5) return beer_nil();
    DrawRectangle((int)beer_to_int(argv[0]),
                  (int)beer_to_int(argv[1]),
                  (int)beer_to_int(argv[2]),
                  (int)beer_to_int(argv[3]),
                  beer_to_color(argv[4]));
    return beer_nil();
}

/* (rl/draw-line x1 y1 x2 y2 color) */
static BeerValue rl_draw_line(BeerVM* vm, int argc, BeerValue* argv) {
    (void)vm;
    if (argc < 5) return beer_nil();
    DrawLine((int)beer_to_int(argv[0]),
             (int)beer_to_int(argv[1]),
             (int)beer_to_int(argv[2]),
             (int)beer_to_int(argv[3]),
             beer_to_color(argv[4]));
    return beer_nil();
}

/* (rl/draw-text text x y size color) */
static BeerValue rl_draw_text(BeerVM* vm, int argc, BeerValue* argv) {
    (void)vm;
    if (argc < 5 || !beer_is_string(argv[0])) return beer_nil();
    DrawText(beer_to_cstring(argv[0]),
             (int)beer_to_int(argv[1]),
             (int)beer_to_int(argv[2]),
             (int)beer_to_int(argv[3]),
             beer_to_color(argv[4]));
    return beer_nil();
}

/* (rl/window-should-close?) → true/false */
static BeerValue rl_window_should_close(BeerVM* vm, int argc, BeerValue* argv) {
    (void)vm; (void)argc; (void)argv;
    return beer_bool(WindowShouldClose());
}

/* (rl/get-fps) → int */
static BeerValue rl_get_fps(BeerVM* vm, int argc, BeerValue* argv) {
    (void)vm; (void)argc; (void)argv;
    return beer_int(GetFPS());
}

/* (rl/get-screen-width) → int */
static BeerValue rl_get_screen_width(BeerVM* vm, int argc, BeerValue* argv) {
    (void)vm; (void)argc; (void)argv;
    return beer_int(GetScreenWidth());
}

/* (rl/get-screen-height) → int */
static BeerValue rl_get_screen_height(BeerVM* vm, int argc, BeerValue* argv) {
    (void)vm; (void)argc; (void)argv;
    return beer_int(GetScreenHeight());
}

/* (rl/key-down? key) — key is a keyword e.g. :right :space :escape */
static int keyword_to_key(BeerValue v) {
    if (!beer_is_keyword(v)) return KEY_NULL;
    const char* k = beer_to_cstring(v);
    if (!strcmp(k, "space"))   return KEY_SPACE;
    if (!strcmp(k, "escape"))  return KEY_ESCAPE;
    if (!strcmp(k, "enter"))   return KEY_ENTER;
    if (!strcmp(k, "up"))      return KEY_UP;
    if (!strcmp(k, "down"))    return KEY_DOWN;
    if (!strcmp(k, "left"))    return KEY_LEFT;
    if (!strcmp(k, "right"))   return KEY_RIGHT;
    if (!strcmp(k, "w"))       return KEY_W;
    if (!strcmp(k, "a"))       return KEY_A;
    if (!strcmp(k, "s"))       return KEY_S;
    if (!strcmp(k, "d"))       return KEY_D;
    if (!strcmp(k, "r"))       return KEY_R;
    if (!strcmp(k, "q"))       return KEY_Q;
    if (beer_is_int(v))        return (int)beer_to_int(v);
    return KEY_NULL;
}

static BeerValue rl_key_down(BeerVM* vm, int argc, BeerValue* argv) {
    (void)vm;
    return beer_bool(argc >= 1 && IsKeyDown(keyword_to_key(argv[0])));
}

static BeerValue rl_key_pressed(BeerVM* vm, int argc, BeerValue* argv) {
    (void)vm;
    return beer_bool(argc >= 1 && IsKeyPressed(keyword_to_key(argv[0])));
}

/* (rl/mouse-x) (rl/mouse-y) */
static BeerValue rl_mouse_x(BeerVM* vm, int argc, BeerValue* argv) {
    (void)vm; (void)argc; (void)argv;
    return beer_int(GetMouseX());
}
static BeerValue rl_mouse_y(BeerVM* vm, int argc, BeerValue* argv) {
    (void)vm; (void)argc; (void)argv;
    return beer_int(GetMouseY());
}

/* (rl/random min max) → int  — thin wrapper around raylib's GetRandomValue */
static BeerValue rl_random(BeerVM* vm, int argc, BeerValue* argv) {
    (void)vm;
    int lo = argc >= 1 ? (int)beer_to_int(argv[0]) : 0;
    int hi = argc >= 2 ? (int)beer_to_int(argv[1]) : 100;
    return beer_int(GetRandomValue(lo, hi));
}

/* ------------------------------------------------------------------ */
/* Native registration                                                 */
/* ------------------------------------------------------------------ */

static void register_rl_natives(BeerState* B) {
    beer_register(B, "rl", "begin-drawing",      rl_begin_drawing);
    beer_register(B, "rl", "end-drawing",        rl_end_drawing);
    beer_register(B, "rl", "clear-background",   rl_clear_background);
    beer_register(B, "rl", "draw-circle",        rl_draw_circle);
    beer_register(B, "rl", "draw-rectangle",     rl_draw_rectangle);
    beer_register(B, "rl", "draw-line",          rl_draw_line);
    beer_register(B, "rl", "draw-text",          rl_draw_text);
    beer_register(B, "rl", "window-should-close?", rl_window_should_close);
    beer_register(B, "rl", "get-fps",            rl_get_fps);
    beer_register(B, "rl", "get-screen-width",   rl_get_screen_width);
    beer_register(B, "rl", "get-screen-height",  rl_get_screen_height);
    beer_register(B, "rl", "key-down?",          rl_key_down);
    beer_register(B, "rl", "key-pressed?",       rl_key_pressed);
    beer_register(B, "rl", "mouse-x",            rl_mouse_x);
    beer_register(B, "rl", "mouse-y",            rl_mouse_y);
    beer_register(B, "rl", "random",             rl_random);
}

/* ------------------------------------------------------------------ */
/* Entry point                                                         */
/* ------------------------------------------------------------------ */

int main(void) {
    /* --- beerlang setup --- */
    BeerState* B = beer_open();
    beer_add_load_path(B, ".");   /* find rl.beer, game.beer in CWD */

    register_rl_natives(B);

    if (beer_do_file(B, "rl.beer")) {
        fprintf(stderr, "error loading rl.beer: %s\n", beer_error(B));
        beer_close(B);
        return 1;
    }
    if (beer_do_file(B, "game.beer")) {
        fprintf(stderr, "error loading game.beer: %s\n", beer_error(B));
        beer_close(B);
        return 1;
    }

    /* --- window --- */
    InitWindow(800, 600, "beerlang + raylib");
    SetTargetFPS(60);

    /* --- call game/init! to set up initial state --- */
    BeerValue init_fn = beer_lookup(B, "game/init!");
    if (!beer_is_nil(init_fn)) {
        beer_call(B, init_fn, 0, NULL);
        beer_release(init_fn);
    }

    /* --- game loop: C drives the frame, beerlang drives everything inside --- */
    BeerValue tick_fn = beer_lookup(B, "game/tick");

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        BeerValue dt_val = beer_float((double)dt);
        beer_call(B, tick_fn, 1, &dt_val);

        /* Keep going even if beerlang errors — don't crash the game */
        if (beer_error(B)) {
            fprintf(stderr, "frame error: %s\n", beer_error(B));
            beer_clear_error(B);
        }
    }

    beer_release(tick_fn);

    /* --- shutdown --- */
    CloseWindow();
    beer_close(B);
    return 0;
}
