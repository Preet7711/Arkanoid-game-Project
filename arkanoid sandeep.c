// arkanoid_full.c
// Arkanoid-like game in C using SDL2 + SDL2_mixer
// Modular single-file version mapping to components:
// 1) Engine, 2) Graphics, 3) Input, 4) Levels/Scoring, 5) Sound & UI

#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <string.h>

/* --------------------- CONFIG --------------------- */
#define WIN_W 960
#define WIN_H 640

#define PADDLE_W 140
#define PADDLE_H 18
#define PADDLE_Y_OFFSET 64
#define PADDLE_SPEED 800.0f

#define BALL_SIZE 14
#define BALL_SPEED_INIT 420.0f
#define BALL_SPEED_INC 1.03f

#define BRICK_COLS 12
#define BRICK_ROWS 7
#define BRICK_W (WIN_W / BRICK_COLS)
#define BRICK_H 28
#define BRICK_PAD 4

#define MAX_LEVELS 5
#define MAX_LIVES 3

/* --------------------- TYPES --------------------- */

typedef struct { float x,y,w,h; } RectF;

typedef struct {
    RectF r;
    float vx, vy;
    int alive;
    int colorIdx;
} Brick;

typedef struct {
    RectF r;
    float vx;
} Paddle;

typedef struct {
    RectF r;
    float vx, vy;
    float speed;
    int held; // held on paddle (serve)
} Ball;

typedef struct {
    int score;
    int lives;
    int level;
    int bricksRemaining;
    int paused;
    int running;
    int showMenu;
} GameState;

/* --------------------- GLOBALS --------------------- */

SDL_Window *gWindow = NULL;
SDL_Renderer *gRenderer = NULL;
Mix_Chunk *s_bounce = NULL;
Mix_Chunk *s_break = NULL;
Mix_Music *bgm = NULL;

Paddle gPaddle;
Ball gBall;
Brick gBricks[BRICK_ROWS * BRICK_COLS];
SDL_Color palette[8];
GameState state;

/* --------------------- UTIL --------------------- */

static inline int brickIndex(int r, int c){ return r * BRICK_COLS + c; }

void clampPaddle() {
    if (gPaddle.r.x < 0) gPaddle.r.x = 0;
    if (gPaddle.r.x + gPaddle.r.w > WIN_W) gPaddle.r.x = WIN_W - gPaddle.r.w;
}

/* --------------------- 1) GAME ENGINE & LOGIC --------------------- */

void resetLevel(int level) {
    // simple color cycling and brick layout changes by level
    int aliveCount = 0;
    for (int r=0;r<BRICK_ROWS;r++){
        for (int c=0;c<BRICK_COLS;c++){
            Brick *b = &gBricks[brickIndex(r,c)];
            b->r.w = BRICK_W - BRICK_PAD;
            b->r.h = BRICK_H - BRICK_PAD;
            b->r.x = c * BRICK_W + BRICK_PAD/2;
            b->r.y = 80 + r * (BRICK_H + BRICK_PAD);
            // simple level patterns: every level remove some bricks
            if ((level <= 1) || ((r + c + level) % (1 + level/2) != 0)) {
                b->alive = 1;
                aliveCount++;
            } else {
                b->alive = 0;
            }
            b->colorIdx = (r + c + level) % 8;
        }
    }
    state.bricksRemaining = aliveCount;
    // paddle center
    gPaddle.r.x = (WIN_W - gPaddle.r.w)/2.0f;
    gPaddle.r.y = WIN_H - PADDLE_Y_OFFSET;
    // ball on paddle
    gBall.r.x = gPaddle.r.x + (gPaddle.r.w - gBall.r.w)/2.0f;
    gBall.r.y = gPaddle.r.y - gBall.r.h - 2;
    gBall.vx = 0;
    gBall.vy = -1;
    gBall.speed = BALL_SPEED_INIT;
    gBall.held = 1;
}

void resetGame() {
    state.score = 0;
    state.lives = MAX_LIVES;
    state.level = 1;
    state.paused = 0;
    state.running = 1;
    state.showMenu = 1;
    resetLevel(state.level);
}

/* collision helpers */
int rectOverlap(RectF *a, RectF *b){
    return !(a->x + a->w <= b->x || b->x + b->w <= a->x || a->y + a->h <= b->y || b->y + b->h <= a->y);
}

/* update physics */
void updateEngine(float dt) {
    if (!state.running || state.paused || state.showMenu) return;

    // move ball if released
    if (!gBall.held) {
        gBall.r.x += gBall.vx * gBall.speed * dt;
        gBall.r.y += gBall.vy * gBall.speed * dt;
    } else {
        // stick to paddle
        gBall.r.x = gPaddle.r.x + (gPaddle.r.w - gBall.r.w)/2.0f;
        gBall.r.y = gPaddle.r.y - gBall.r.h - 2;
    }

    // wall collision
    if (!gBall.held) {
        if (gBall.r.x <= 0) { gBall.r.x = 0; gBall.vx = fabsf(gBall.vx); Mix_PlayChannel(-1,s_bounce,0); }
        if (gBall.r.x + gBall.r.w >= WIN_W) { gBall.r.x = WIN_W - gBall.r.w; gBall.vx = -fabsf(gBall.vx); Mix_PlayChannel(-1,s_bounce,0); }
        if (gBall.r.y <= 0) { gBall.r.y = 0; gBall.vy = fabsf(gBall.vy); Mix_PlayChannel(-1,s_bounce,0); }
    }

    // paddle collision: only react if ball moving down
    if (!gBall.held && gBall.vy > 0) {
        RectF ballRect = gBall.r;
        RectF padRect = gPaddle.r;
        if (rectOverlap(&ballRect,&padRect)) {
            float impact = ((gBall.r.x + gBall.r.w/2.0f) - (gPaddle.r.x + gPaddle.r.w/2.0f)) / (gPaddle.r.w/2.0f);
            if (impact < -1) impact = -1;
            if (impact > 1) impact = 1;
            float angle = impact * (75.0f * (M_PI/180.0f));
            gBall.vx = sinf(angle);
            gBall.vy = -cosf(angle);
            gBall.speed *= BALL_SPEED_INC;
            gBall.r.y = gPaddle.r.y - gBall.r.h - 1;
            Mix_PlayChannel(-1,s_bounce,0);
        }
    }

    // brick collisions
    if (!gBall.held) {
        for (int r=0;r<BRICK_ROWS;r++){
            for (int c=0;c<BRICK_COLS;c++){
                Brick *b = &gBricks[brickIndex(r,c)];
                if (!b->alive) continue;
                RectF br = b->r;
                if (rectOverlap(&gBall.r, &br)) {
                    // reflect based on minimal overlap
                    float ol = (gBall.r.x + gBall.r.w) - br.x;
                    float or = (br.x + br.w) - gBall.r.x;
                    float ot = (gBall.r.y + gBall.r.h) - br.y;
                    float ob = (br.y + br.h) - gBall.r.y;
                    float min = ol; min = fminf(min, or); min = fminf(min, ot); min = fminf(min, ob);
                    if (min == ol) { gBall.r.x -= ol; gBall.vx = -fabsf(gBall.vx); }
                    else if (min == or) { gBall.r.x += or; gBall.vx = fabsf(gBall.vx); }
                    else if (min == ot) { gBall.r.y -= ot; gBall.vy = -fabsf(gBall.vy); }
                    else { gBall.r.y += ob; gBall.vy = fabsf(gBall.vy); }

                    b->alive = 0;
                    state.bricksRemaining--;
                    state.score += 10 + (state.level-1)*5;
                    Mix_PlayChannel(-1, s_break, 0);
                    gBall.speed *= 1.015f;
                    goto after_brick_hit;
                }
            }
        }
    }
after_brick_hit: ;

    // ball fell below
    if (!gBall.held && gBall.r.y > WIN_H) {
        state.lives--;
        Mix_PlayChannel(-1,s_bounce,0); // small sound for life lost (reuse)
        if (state.lives <= 0) {
            // game over => back to menu
            state.showMenu = 1;
            state.running = 0;
        } else {
            // reset ball on paddle
            gBall.held = 1;
            gBall.speed = BALL_SPEED_INIT;
            gBall.vx = 0; gBall.vy = -1;
            gPaddle.r.x = (WIN_W - gPaddle.r.w)/2.0f;
        }
    }

    // level cleared?
    if (state.bricksRemaining <= 0) {
        state.level++;
        if (state.level > MAX_LEVELS) {
            // victory -> show menu / restart
            state.showMenu = 1;
            state.running = 0;
        } else {
            resetLevel(state.level);
        }
    }
}

/* --------------------- 2) GRAPHICS & RENDERING --------------------- */

void drawRectF(SDL_Renderer *r, RectF *f) {
    SDL_Rect rr = { (int)f->x, (int)f->y, (int)f->w, (int)f->h };
    SDL_RenderFillRect(r, &rr);
}

void drawGradientBackground(SDL_Renderer *ren) {
    // simple two-color vertical gradient (top->bottom)
    for (int y=0;y<WIN_H;y+=4) {
        float t = (float)y / (float)WIN_H;
        // top: deep purple/navy, bottom: teal-ish
        Uint8 r = (Uint8)((1-t)*12 + t*12);
        Uint8 g = (Uint8)((1-t)*18 + t*60);
        Uint8 b = (Uint8)((1-t)*48 + t*90);
        SDL_SetRenderDrawColor(ren, r, g, b, 255);
        SDL_Rect line = {0,y,WIN_W,4};
        SDL_RenderFillRect(ren, &line);
    }
}

void renderGame() {
    // clear bg
    drawGradientBackground(gRenderer);

    // bricks
    for (int r=0;r<BRICK_ROWS;r++){
        for (int c=0;c<BRICK_COLS;c++){
            Brick *b = &gBricks[brickIndex(r,c)];
            if (!b->alive) continue;
            SDL_Color col = palette[b->colorIdx % 8];
            // slight brightness variation per column
            int dr = (c*8) % 40;
            int dg = (r*6) % 40;
            int rr = col.r + dr; if (rr>255) rr=255;
            int gg = col.g + dg; if (gg>255) gg=255;
            int bb = col.b + dr; if (bb>255) bb=255;
            SDL_SetRenderDrawColor(gRenderer, rr, gg, bb, 255);
            drawRectF(gRenderer, &b->r);
            // inner shine rectangle
            SDL_SetRenderDrawColor(gRenderer, rr/2+50, gg/2+30, bb/2+20, 140);
            RectF inner = { b->r.x + 4, b->r.y + 4, b->r.w - 8, b->r.h - 8 };
            drawRectF(gRenderer, &inner);
        }
    }

    // paddle (two-tone)
    SDL_SetRenderDrawColor(gRenderer, 240,248,255,255);
    RectF top = gPaddle.r; top.h = gPaddle.r.h/2;
    drawRectF(gRenderer, &top);
    SDL_SetRenderDrawColor(gRenderer, 40,130,180,255);
    RectF bot = { gPaddle.r.x, gPaddle.r.y + gPaddle.r.h/2, gPaddle.r.w, gPaddle.r.h/2 };
    drawRectF(gRenderer, &bot);

    // glossy ball
    SDL_SetRenderDrawColor(gRenderer, 255, 240, 180,255);
    drawRectF(gRenderer, &gBall.r);
    SDL_SetRenderDrawColor(gRenderer, 255,255,255,140);
    RectF hl = { gBall.r.x + gBall.r.w*0.2f, gBall.r.y + gBall.r.h*0.2f, gBall.r.w*0.35f, gBall.r.h*0.35f };
    drawRectF(gRenderer, &hl);

    // HUD: lives (as small rectangles) and score box
    // Lives
    for (int i=0;i<state.lives;i++){
        SDL_Rect l = { 12 + i*36, 8, 32, 20 };
        SDL_SetRenderDrawColor(gRenderer, 220,20,60,255);
        SDL_RenderFillRect(gRenderer, &l);
        SDL_SetRenderDrawColor(gRenderer,255,255,255,100);
        SDL_RenderDrawRect(gRenderer,&l);
    }

    // Score box (we aren't drawing text, so just a stylish panel)
    SDL_Rect scoreBox = { WIN_W - 220, 8, 200, 40 };
    SDL_SetRenderDrawColor(gRenderer, 20,20,40,200);
    SDL_RenderFillRect(gRenderer, &scoreBox);
    SDL_SetRenderDrawColor(gRenderer,255,255,255,80);
    SDL_RenderDrawRect(gRenderer,&scoreBox);

    // center messages (menu/paused)
    if (state.showMenu) {
        SDL_Rect panel = { WIN_W/2 - 260, WIN_H/2 - 120, 520, 240 };
        SDL_SetRenderDrawColor(gRenderer, 0,0,0,160);
        SDL_RenderFillRect(gRenderer,&panel);
        SDL_SetRenderDrawColor(gRenderer,255,255,255,200);
        SDL_RenderDrawRect(gRenderer,&panel);
        // we don't render text (no TTF). The console will show instructions; add TTF later.
    } else if (state.paused) {
        SDL_Rect p = { WIN_W/2 - 180, WIN_H/2 - 40, 360, 80 };
        SDL_SetRenderDrawColor(gRenderer, 0,0,0,160);
        SDL_RenderFillRect(gRenderer, &p);
        SDL_SetRenderDrawColor(gRenderer, 255,255,255,200);
        SDL_RenderDrawRect(gRenderer, &p);
    }
}

/* --------------------- 3) INPUT HANDLING --------------------- */

void handleInput(SDL_Event *ev) {
    if (ev->type == SDL_QUIT) {
        state.running = 0;
    } else if (ev->type == SDL_KEYDOWN) {
        SDL_Keycode k = ev->key.keysym.sym;
        if (k == SDLK_ESCAPE) {
            // toggle menu or quit if already in menu
            if (state.showMenu) state.running = 0;
            else state.showMenu = 1;
        } else if (k == SDLK_SPACE) {
            if (state.showMenu) {
                state.showMenu = 0;
                state.running = 1;
                // start level
                resetLevel(state.level);
                Mix_PlayMusic(bgm, -1);
            } else if (state.paused) {
                state.paused = 0;
            } else if (gBall.held) {
                // serve with random small angle
                float ang = ((rand() % 120) - 60) * (M_PI/180.0f);
                gBall.vx = sinf(ang);
                gBall.vy = -fabsf(cosf(ang));
                // normalize
                float m = sqrtf(gBall.vx*gBall.vx + gBall.vy*gBall.vy);
                gBall.vx /= m; gBall.vy /= m;
                gBall.held = 0;
            } else {
                // pause
                state.paused = !state.paused;
            }
        } else if (k == SDLK_r) {
            // restart game
            resetGame();
        } else if (k == SDLK_m) {
            // toggle music
            if (Mix_PlayingMusic()) Mix_PausedMusic() ? Mix_ResumeMusic() : Mix_PauseMusic();
            else if (bgm) Mix_PlayMusic(bgm, -1);
        }
    } else if (ev->type == SDL_MOUSEMOTION) {
        // optional: move paddle with mouse x
        int mx = ev->motion.x;
        gPaddle.r.x = mx - gPaddle.r.w/2;
        clampPaddle();
    }
}

/* --------------------- 4) LEVELS & SCORING (simple) --------------------- */

/* We used resetLevel() to create procedural layouts. If you want
   to load from text files, implement a simple parser to set .alive flags. */

/* --------------------- 5) SOUND, UI & MENU SYSTEM --------------------- */

int loadAudio() {
    // allow missing audio (silently)
    s_bounce = Mix_LoadWAV("bounce.wav");
    s_break  = Mix_LoadWAV("break.wav");
    bgm = Mix_LoadMUS("bgm.mp3");
    // if any failed, it's okay; game still runs
    return 1;
}

/* --------------------- INIT / CLEANUP --------------------- */

int initPalette() {
    // vibrant palette
    palette[0] = (SDL_Color){255,99,71,255};   // tomato
    palette[1] = (SDL_Color){255,215,0,255};   // gold
    palette[2] = (SDL_Color){60,179,113,255};  // medium sea green
    palette[3] = (SDL_Color){65,105,225,255};  // royal blue
    palette[4] = (SDL_Color){199,21,133,255};  // medium violet red
    palette[5] = (SDL_Color){30,144,255,255};  // dodger blue
    palette[6] = (SDL_Color){255,140,0,255};   // dark orange
    palette[7] = (SDL_Color){138,43,226,255};  // blue violet
    return 1;
}

int initAll() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr,"SDL_Init failed: %s\n", SDL_GetError());
        return 0;
    }
    gWindow = SDL_CreateWindow("Arkanoid - C + SDL2 (modular)",
                              SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              WIN_W, WIN_H, SDL_WINDOW_SHOWN);
    if (!gWindow) { fprintf(stderr,"Window create fail: %s\n", SDL_GetError()); return 0; }
    gRenderer = SDL_CreateRenderer(gWindow, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!gRenderer) { fprintf(stderr,"Renderer fail: %s\n", SDL_GetError()); return 0; }

    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 1024) < 0) {
        fprintf(stderr,"Mix_OpenAudio fail: %s\n", Mix_GetError());
        // continue without audio
    }

    initPalette();
    // default paddle and ball sizes
    gPaddle.r.w = PADDLE_W; gPaddle.r.h = PADDLE_H;
    gBall.r.w = BALL_SIZE; gBall.r.h = BALL_SIZE;
    srand((unsigned)time(NULL));
    // load audio (optional)
    loadAudio();
    return 1;
}

void cleanupAll() {
    if (s_bounce) Mix_FreeChunk(s_bounce);
    if (s_break) Mix_FreeChunk(s_break);
    if (bgm) Mix_FreeMusic(bgm);
    Mix_CloseAudio();
    if (gRenderer) SDL_DestroyRenderer(gRenderer);
    if (gWindow) SDL_DestroyWindow(gWindow);
    SDL_Quit();
}

/* --------------------- MAIN LOOP --------------------- */

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    if (!initAll()) return 1;

    // initialize state
    gPaddle.r.w = PADDLE_W; gPaddle.r.h = PADDLE_H;
    gPaddle.vx = 0;
    gBall.r.w = BALL_SIZE; gBall.r.h = BALL_SIZE;
    resetGame();

    Uint64 NOW = SDL_GetPerformanceCounter();
    Uint64 LAST = 0;
    double deltaTime = 0;

    SDL_Event ev;
    while (state.running) {
        LAST = NOW;
        NOW = SDL_GetPerformanceCounter();
        deltaTime = (double)((NOW - LAST) / (double)SDL_GetPerformanceFrequency());
        if (deltaTime > 0.05) deltaTime = 0.05;

        while (SDL_PollEvent(&ev)) {
            handleInput(&ev);
        }

        // keyboard state for continuous input
        const Uint8 *ks = SDL_GetKeyboardState(NULL);
        float vx = 0.0f;
        if (ks[SDL_SCANCODE_LEFT] || ks[SDL_SCANCODE_A]) vx = -PADDLE_SPEED;
        if (ks[SDL_SCANCODE_RIGHT] || ks[SDL_SCANCODE_D]) vx = PADDLE_SPEED;
        gPaddle.r.x += vx * (float)deltaTime;
        clampPaddle();

        // engine update
        updateEngine((float)deltaTime);

        // render
        SDL_SetRenderDrawBlendMode(gRenderer, SDL_BLENDMODE_BLEND);
        renderGame();
        SDL_RenderPresent(gRenderer);

        // occasional console logging
        static double acc = 0;
        acc += deltaTime;
        if (acc > 0.5) {
            acc = 0;
            printf("Score:%d Lives:%d Level:%d Bricks:%d\n", state.score, state.lives, state.level, state.bricksRemaining);
        }
        SDL_Delay(1);
    }

    cleanupAll();
    return 0;
}
