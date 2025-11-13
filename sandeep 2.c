// arkanoid_readable.c
// Arkanoid-like game in C using SDL2 + SDL2_mixer
// Upgrades (no extra SDL libs):
// - smoother ball glow (concentric translucent rings)
// - improved paddle (rounded-ish ends & highlight)
// - particle effects on brick break
// - level loading from text files (level1.txt ... levelN.txt) with fallback procedural generator
// - soundtrack looping handled robustly
// - animated background (subtle nebula drift + parallax star movement)

#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <string.h>

/* --------------------- CONFIG --------------------- */
#define WINDOW_WIDTH 960
#define WINDOW_HEIGHT 640

#define PADDLE_WIDTH 140
#define PADDLE_HEIGHT 18
#define PADDLE_Y_OFFSET 64
#define PADDLE_SPEED 800.0f

#define BALL_SIZE 14
#define BALL_SPEED_INITIAL 420.0f
#define BALL_SPEED_GROWTH 1.0f

#define BRICK_COLUMNS 12
#define BRICK_ROWS 7
#define BRICK_WIDTH (WINDOW_WIDTH / BRICK_COLUMNS)
#define BRICK_HEIGHT 28
#define BRICK_PADDING 4

#define MAX_LEVELS 10
#define STARTING_LIVES 3

#define NUM_STARS 240
#define STAR_LAYERS 3

#define MAX_PARTICLES 512

/* --------------------- TYPES --------------------- */

typedef struct { float x, y, w, h; } RectF;

typedef struct { RectF rect; int is_alive; int color_index; } Brick;

typedef struct { RectF rect; float velocity_x; } Paddle;

typedef struct { RectF rect; float vx, vy; float speed; int is_held; } Ball;

typedef struct { int score; int lives; int level; int bricks_remaining; int is_paused; int is_running; int show_menu; } GameState;

typedef struct { float x, y; float size; int layer; float vx, vy; } Star; // vx for parallax drift

typedef struct { float x, y; float vx, vy; float life; float max_life; SDL_Color col; int alive; } Particle;

/* --------------------- GLOBALS --------------------- */
SDL_Window *window = NULL;
SDL_Renderer *renderer = NULL;
Mix_Chunk *sfx_bounce = NULL;
Mix_Chunk *sfx_break = NULL;
Mix_Music *music_bgm = NULL;

Paddle paddle;
Ball ball;
Brick bricks[BRICK_ROWS * BRICK_COLUMNS];
SDL_Color color_palette[10];
GameState game_state;
Star stars[NUM_STARS];
Particle particles[MAX_PARTICLES];

/* --------------------- UTIL --------------------- */
static inline int brick_index(int row, int col) { return row * BRICK_COLUMNS + col; }
void clamp_paddle_position() { if (paddle.rect.x < 0) paddle.rect.x = 0; if (paddle.rect.x + paddle.rect.w > WINDOW_WIDTH) paddle.rect.x = WINDOW_WIDTH - paddle.rect.w; }

/* --------------------- LEVEL LOADING (text file) --------------------- */
// level files: "level1.txt", ... rows=BRICK_ROWS, cols=BRICK_COLUMNS. Use '#' for brick, '.' for empty.
int load_level_from_file(int level) {
    char name[128]; snprintf(name, sizeof(name), "level%d.txt", level);
    FILE *f = fopen(name, "r");
    if (!f) return 0; // not found
    char line[256];
    int r = 0;
    for (r = 0; r < BRICK_ROWS; r++) {
        if (!fgets(line, sizeof(line), f)) break;
        for (int c = 0; c < BRICK_COLUMNS; c++) {
            Brick *b = &bricks[brick_index(r,c)];
            b->rect.w = BRICK_WIDTH - BRICK_PADDING; b->rect.h = BRICK_HEIGHT - BRICK_PADDING;
            b->rect.x = c * BRICK_WIDTH + BRICK_PADDING/2; b->rect.y = 80 + r * (BRICK_HEIGHT + BRICK_PADDING);
            char ch = (c < (int)strlen(line)) ? line[c] : '.';
            if (ch == '#') { b->is_alive = 1; } else { b->is_alive = 0; }
            b->color_index = (r + c + level) % 10;
        }
    }
    fclose(f);
    // count alive
    int alive = 0; for (int i=0;i<BRICK_ROWS*BRICK_COLUMNS;i++) if (bricks[i].is_alive) alive++;
    game_state.bricks_remaining = alive;
    return 1;
}

/* --------------------- GAME ENGINE --------------------- */
void reset_level(int level) {
    // try file first
    if (load_level_from_file(level)) {
        // file loaded counts as resetting positions already
    } else {
        // procedural fallback
        int alive_count = 0;
        for (int r = 0; r < BRICK_ROWS; r++) {
            for (int c = 0; c < BRICK_COLUMNS; c++) {
                Brick *b = &bricks[brick_index(r,c)];
                b->rect.w = BRICK_WIDTH - BRICK_PADDING; b->rect.h = BRICK_HEIGHT - BRICK_PADDING;
                b->rect.x = c * BRICK_WIDTH + BRICK_PADDING/2; b->rect.y = 80 + r * (BRICK_HEIGHT + BRICK_PADDING);
                if ((level <= 1) || ((r + c + level) % (1 + level / 2) != 0)) { b->is_alive = 1; alive_count++; } else b->is_alive = 0;
                b->color_index = (r + c + level) % 10;
            }
        }
        game_state.bricks_remaining = alive_count;
    }
    paddle.rect.x = (WINDOW_WIDTH - paddle.rect.w) / 2.0f; paddle.rect.y = WINDOW_HEIGHT - PADDLE_Y_OFFSET;
    ball.rect.x = paddle.rect.x + (paddle.rect.w - ball.rect.w) / 2.0f; ball.rect.y = paddle.rect.y - ball.rect.h - 2; ball.vx = 0; ball.vy = -1; ball.speed = BALL_SPEED_INITIAL; ball.is_held = 1;
}

void reset_game() { game_state.score = 0; game_state.lives = STARTING_LIVES; game_state.level = 1; game_state.is_paused = 0; game_state.is_running = 1; game_state.show_menu = 1; reset_level(game_state.level); }

int rect_overlap(RectF *a, RectF *b) { return !(a->x + a->w <= b->x || b->x + b->w <= a->x || a->y + a->h <= b->y || b->y + b->h <= a->y); }

/* particle helpers */
void spawn_particles(float x, float y, SDL_Color col, int count) {
    for (int i = 0; i < MAX_PARTICLES && count > 0; i++) {
        if (!particles[i].alive) {
            particles[i].alive = 1; particles[i].x = x; particles[i].y = y;
            float ang = ((rand()%360) * (M_PI/180.0f));
            float sp = 60 + (rand()%120);
            particles[i].vx = cosf(ang)*sp; particles[i].vy = sinf(ang)*sp;
            particles[i].life = 0.0f; particles[i].max_life = 0.5f + ((rand()%100)/200.0f);
            particles[i].col = col; particles[i].alive = 1; count--; }
    }
}

void update_particles(float dt) {
    for (int i=0;i<MAX_PARTICLES;i++) {
        if (!particles[i].alive) continue;
        particles[i].x += particles[i].vx * dt; particles[i].y += particles[i].vy * dt; particles[i].vy += 200.0f * dt; // gravity-ish
        particles[i].life += dt; if (particles[i].life >= particles[i].max_life) particles[i].alive = 0;
    }
}

/* engine update */
void update_engine(float dt) {
    if (!game_state.is_running || game_state.is_paused || game_state.show_menu) return;
    // ball movement
    if (!ball.is_held) { ball.rect.x += ball.vx * ball.speed * dt; ball.rect.y += ball.vy * ball.speed * dt; }
    else { ball.rect.x = paddle.rect.x + (paddle.rect.w - ball.rect.w)/2.0f; ball.rect.y = paddle.rect.y - ball.rect.h - 2; }
    // wall
    if (!ball.is_held) {
        if (ball.rect.x <= 0) { ball.rect.x = 0; ball.vx = fabsf(ball.vx); if (sfx_bounce) Mix_PlayChannel(-1, sfx_bounce, 0); }
        if (ball.rect.x + ball.rect.w >= WINDOW_WIDTH) { ball.rect.x = WINDOW_WIDTH - ball.rect.w; ball.vx = -fabsf(ball.vx); if (sfx_bounce) Mix_PlayChannel(-1, sfx_bounce, 0); }
        if (ball.rect.y <= 0) { ball.rect.y = 0; ball.vy = fabsf(ball.vy); if (sfx_bounce) Mix_PlayChannel(-1, sfx_bounce, 0); }
    }
    // paddle
    if (!ball.is_held && ball.vy > 0 && rect_overlap(&ball.rect, &paddle.rect)) {
        float impact = ((ball.rect.x + ball.rect.w/2.0f) - (paddle.rect.x + paddle.rect.w/2.0f)) / (paddle.rect.w/2.0f);
        if (impact < -1) impact = -1; if (impact > 1) impact = 1; float angle = impact * (75.0f * (M_PI/180.0f));
        ball.vx = sinf(angle); ball.vy = -cosf(angle); ball.speed *= BALL_SPEED_GROWTH; ball.rect.y = paddle.rect.y - ball.rect.h - 1; if (sfx_bounce) Mix_PlayChannel(-1, sfx_bounce, 0);
    }
    // bricks
    if (!ball.is_held) {
        for (int r=0;r<BRICK_ROWS;r++) for (int c=0;c<BRICK_COLUMNS;c++) {
            Brick *b = &bricks[brick_index(r,c)]; if (!b->is_alive) continue; RectF br = b->rect;
            if (rect_overlap(&ball.rect, &br)) {
                // reflect by minimal overlap
                float ol = (ball.rect.x + ball.rect.w) - br.x; float or = (br.x + br.w) - ball.rect.x; float ot = (ball.rect.y + ball.rect.h) - br.y; float ob = (br.y + br.h) - ball.rect.y; float m = ol; m = fminf(m, or); m = fminf(m, ot); m = fminf(m, ob);
                if (m == ol) { ball.rect.x -= ol; ball.vx = -fabsf(ball.vx); }
                else if (m == or) { ball.rect.x += or; ball.vx = fabsf(ball.vx); }
                else if (m == ot) { ball.rect.y -= ot; ball.vy = -fabsf(ball.vy); }
                else { ball.rect.y += ob; ball.vy = fabsf(ball.vy); }
                b->is_alive = 0; game_state.bricks_remaining--; game_state.score += 10 + (game_state.level-1)*5; if (sfx_break) Mix_PlayChannel(-1, sfx_break, 0);
                // spawn particles
                SDL_Color pc = color_palette[b->color_index % 10]; spawn_particles(ball.rect.x + ball.rect.w/2, ball.rect.y + ball.rect.h/2, pc, 18);
                ball.speed *= 1.015f; goto after_brick;
            }
        }
    }
after_brick: ;
    // fell
    if (!ball.is_held && ball.rect.y > WINDOW_HEIGHT) {
        game_state.lives--; if (sfx_bounce) Mix_PlayChannel(-1, sfx_bounce, 0);
        if (game_state.lives <= 0) { game_state.show_menu = 1; game_state.is_running = 0; }
        else { ball.is_held = 1; ball.speed = BALL_SPEED_INITIAL; ball.vx = 0; ball.vy = -1; paddle.rect.x = (WINDOW_WIDTH - paddle.rect.w)/2.0f; }
    }
    if (game_state.bricks_remaining <= 0) { game_state.level++; if (game_state.level > MAX_LEVELS) { game_state.show_menu = 1; game_state.is_running = 0; } else reset_level(game_state.level); }
    update_particles(dt);
    // update stars for animated background (parallax drift)
    for (int i=0;i<NUM_STARS;i++) {
        stars[i].x += stars[i].vx * dt;
        stars[i].y += stars[i].vy * dt;
        if (stars[i].x < -20) stars[i].x = WINDOW_WIDTH + 20; if (stars[i].x > WINDOW_WIDTH+20) stars[i].x = -20;
        if (stars[i].y < -20) stars[i].y = WINDOW_HEIGHT + 20; if (stars[i].y > WINDOW_HEIGHT+20) stars[i].y = -20;
    }
}

/* --------------------- RENDER HELPERS --------------------- */
void draw_rectf(SDL_Renderer *ren, RectF *f) { SDL_Rect rr = { (int)f->x, (int)f->y, (int)f->w, (int)f->h }; SDL_RenderFillRect(ren, &rr); }

// ball glow: draw concentric translucent rectangles (approximating glow)
void draw_ball_with_glow(Ball *b) {
    int rings = 6;
    for (int i = rings; i >= 1; i--) {
        float t = (float)i / (float)rings;
        Uint8 a = (Uint8)(40 * t);
        // color shifts toward white for inner rings
        int r = 255, g = 240, bl = 180;
        SDL_SetRenderDrawColor(renderer, r, g, bl, a);
        RectF gr = { b->rect.x - (rings-i)*2.0f, b->rect.y - (rings-i)*2.0f, b->rect.w + (rings-i)*4.0f, b->rect.h + (rings-i)*4.0f };
        draw_rectf(renderer, &gr);
    }
    SDL_SetRenderDrawColor(renderer, 255, 240, 180, 255); draw_rectf(renderer, &b->rect);
}

// improved paddle: rounded-ish ends by drawing small vertical strips
void draw_paddle(Paddle *p) {
    // base
    SDL_SetRenderDrawColor(renderer, 30, 90, 140, 255);
    draw_rectf(renderer, &p->rect);
    // highlight top
    SDL_SetRenderDrawColor(renderer, 220, 240, 255, 255);
    RectF top = { p->rect.x + 4, p->rect.y + 2, p->rect.w - 8, p->rect.h/2 - 2 }; draw_rectf(renderer, &top);
    // rounded ends (simple approximation)
    SDL_SetRenderDrawColor(renderer, 30, 90, 140, 255);
    int endw = 8; for (int i=0;i<endw;i++) {
        float alpha = (float)(endw - i) / (float)endw;
        SDL_SetRenderDrawColor(renderer, 30, 90, 140, (Uint8)(255*alpha));
        SDL_Rect left = { (int)(p->rect.x - endw + i), (int)(p->rect.y + i/2), (int)(endw - i), (int)(p->rect.h - i) };
        SDL_RenderFillRect(renderer, &left);
        SDL_Rect right = { (int)(p->rect.x + p->rect.w + i), (int)(p->rect.y + i/2), (int)(endw - i), (int)(p->rect.h - i) };
        SDL_RenderFillRect(renderer, &right);
    }
}

void draw_textured_brick(Brick *b) {
    SDL_Color base = color_palette[b->color_index % 10]; SDL_SetRenderDrawColor(renderer, base.r, base.g, base.b, 255); draw_rectf(renderer, &b->rect);
    // inner shine
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 110); RectF shine = { b->rect.x + 6, b->rect.y + 4, b->rect.w * 0.5f, b->rect.h * 0.35f }; draw_rectf(renderer, &shine);
    // shadow
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 40); RectF shadow = { b->rect.x + 4, b->rect.y + b->rect.h - 6, b->rect.w - 6, 6 }; draw_rectf(renderer, &shadow);
}
/* --------------------- PIXEL-FONT HUD (no SDL_ttf) --------------------- */
/* 5x7 font for digits and basic letters used by HUD (SCORE, LEVEL).
   Each glyph is 5 columns x 7 rows, stored as 7 bytes (LSB = top pixel). */

static const unsigned char PIXEL_FONT[][7] = {
    // 0..9
    {0x7E,0x81,0x81,0x81,0x7E,0x00,0x00}, // '0'
    {0x00,0x82,0xFF,0x80,0x00,0x00,0x00}, // '1' (vertical centered)
    {0xE2,0x91,0x91,0x91,0x8E,0x00,0x00}, // '2'
    {0x42,0x81,0x89,0x89,0x76,0x00,0x00}, // '3'
    {0x18,0x14,0x12,0x11,0xFF,0x10,0x00}, // '4'
    {0x4F,0x89,0x89,0x89,0x71,0x00,0x00}, // '5'
    {0x7E,0x89,0x89,0x89,0x72,0x00,0x00}, // '6'
    {0x01,0x01,0xF1,0x09,0x07,0x00,0x00}, // '7'
    {0x76,0x89,0x89,0x89,0x76,0x00,0x00}, // '8'
    {0x46,0x89,0x89,0x89,0x7E,0x00,0x00}, // '9'
    // Letters we need: 'S' and 'C','O','R','E','L' (we'll supply simple variants)
    // 'A' (index 10)
    {0x7C,0x12,0x11,0x12,0x7C,0x00,0x00}, // 'A'
    // 'C' (11)
    {0x7E,0x81,0x81,0x81,0x42,0x00,0x00}, // 'C'
    // 'E' (12)
    {0xFF,0x89,0x89,0x89,0x81,0x00,0x00}, // 'E'
    // 'L' (13)
    {0xFF,0x80,0x80,0x80,0x80,0x00,0x00}, // 'L'
    // 'O' (14) - reuse 0 (index 0). We'll point to it.
    // 'R' (15)
    {0xFF,0x11,0x19,0x15,0xE2,0x00,0x00}, // 'R'
    // 'S' (16)
    {0x46,0x89,0x89,0x89,0x72,0x00,0x00}, // 'S' (similar to 5)
};

static int char_index_for_hud(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch == 'A') return 10;
    if (ch == 'C') return 11;
    if (ch == 'E') return 12;
    if (ch == 'L') return 13;
    if (ch == 'R') return 15;
    if (ch == 'S') return 16;
    if (ch == 'O') return 0; // O ~ 0
    return -1;
}

/* Draw one glyph at (x,y) using cell size 'scale' (scale>=1).
   color is SDL_Color with RGBA. */
void draw_glyph(char ch, int x, int y, int scale, SDL_Color color) {
    int idx = char_index_for_hud(ch);
    if (idx < 0) return;
    const unsigned char *g = PIXEL_FONT[idx];
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    for (int row = 0; row < 7; row++) {
        unsigned char bits = g[row];
        // bits hold 8 bits but we use lower 7 bits as columns (we stored as bytes with columns)
        for (int col = 0; col < 7; col++) {
            int bit = (bits >> (6 - col)) & 1;
            if (bit) {
                SDL_Rect r = { x + col * scale, y + row * scale, scale, scale };
                SDL_RenderFillRect(renderer, &r);
            }
        }
    }
}

/* Draw a short ASCII string (letters supported above) */
void draw_text_pixel(const char *s, int x, int y, int scale, SDL_Color col) {
    int cx = x;
    for (size_t i = 0; i < strlen(s); ++i) {
        char c = s[i];
        if (c == ' ') { cx += (6 * scale); continue; }
        draw_glyph(c, cx, y, scale, col);
        cx += (6 * scale); // 5 px glyph + 1 px spacing
    }
}

/* Draw an integer (non-negative) right-aligned ending at (rx, y) */
void draw_number_right(int rx, int y, int scale, int value, SDL_Color col) {
    char buf[32];
    if (value < 0) value = 0;
    snprintf(buf, sizeof(buf), "%d", value);
    int len = (int)strlen(buf);
    int total_w = len * (6 * scale);
    int start = rx - total_w + scale;
    for (int i = 0; i < len; ++i) {
        draw_glyph(buf[i], start + i * (6 * scale), y, scale, col);
    }
}


/* --------------------- BACKGROUND (animated) --------------------- */
void draw_space_background(SDL_Renderer *ren, float tsec) {
    // vertical gradient base
    for (int y=0;y<WINDOW_HEIGHT;y+=2) {
        float ty = (float)y / (float)WINDOW_HEIGHT;
        Uint8 r = (Uint8)(8 + ty*10); Uint8 g = (Uint8)(10 + ty*20); Uint8 b = (Uint8)(28 + ty*50);
        SDL_SetRenderDrawColor(ren, r, g, b, 255); SDL_Rect line = {0,y,WINDOW_WIDTH,2}; SDL_RenderFillRect(ren,&line);
    }
    // nebula band drifts with time
    float offset = sinf(tsec * 0.12f) * 60.0f;
    for (int i=0;i<100;i++) {
        float py = WINDOW_HEIGHT * 0.25f + sinf(i*0.12f + offset*0.01f) * 16.0f + offset*0.05f;
        float width = WINDOW_WIDTH * (0.5f + 0.12f * sinf(i*0.3f + offset*0.02f));
        Uint8 alpha = (Uint8)(20 + (i%4)*6);
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND); SDL_SetRenderDrawColor(ren, 120,40,200, alpha);
        SDL_Rect band = { (int)(WINDOW_WIDTH/2 - width/2), (int)(py + i*1.0f), (int)width, 6 };
        SDL_RenderFillRect(ren, &band);
    }
    // stars
    for (int i=0;i<NUM_STARS;i++) {
        Star *s = &stars[i]; int br = (int)fminf(255, 180 + 40*(1.0f/(s->layer+1))); SDL_SetRenderDrawColor(ren, br, br, 255, 255);
        SDL_Rect r = {(int)s->x, (int)s->y, (int)s->size, (int)s->size}; SDL_RenderFillRect(ren,&r);
    }
    // planet glow subtle
    int px = WINDOW_WIDTH - 160; int py = 110; for (int rad=60;rad>0;rad-=8) { Uint8 a = (Uint8)(16*(rad/60.0f)); SDL_SetRenderDrawColor(ren, 180,120,255,a); SDL_Rect c = {px - rad/2, py - rad/2, rad, rad}; SDL_RenderFillRect(ren,&c); }
}

/* --------------------- RENDER SCENE --------------------- */
void render_scene() {
       
    float tsec = (float)(SDL_GetTicks() / 1000.0f);

    // background (animated)
    draw_space_background(renderer, tsec);

    // bricks (textured)
    for (int r = 0; r < BRICK_ROWS; r++) {
        for (int c = 0; c < BRICK_COLUMNS; c++) {
            Brick *b = &bricks[brick_index(r,c)];
            if (b->is_alive) draw_textured_brick(b);
        }
    }

    // particles (simple 3x3 rects)
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (!particles[i].alive) continue;
        float life_t = particles[i].life / particles[i].max_life;
        Uint8 a = (Uint8)(255 * (1.0f - life_t));
        SDL_SetRenderDrawColor(renderer, particles[i].col.r, particles[i].col.g, particles[i].col.b, a);
        SDL_Rect pr = { (int)particles[i].x, (int)particles[i].y, 3, 3 };
        SDL_RenderFillRect(renderer, &pr);
    }

    // paddle
    draw_paddle(&paddle);

    // ball with glow
    draw_ball_with_glow(&ball);

              // ---------- SIMPLE HORIZONTAL HUD (readable, top-center) ----------
    // background strip
    SDL_Rect hudStrip = { 0, 0, WINDOW_WIDTH, 44 };
    SDL_SetRenderDrawColor(renderer, 6, 8, 20, 220);
    SDL_RenderFillRect(renderer, &hudStrip);

    // colors / sizes
    SDL_Color fg = { 235, 235, 255, 255 };
    int labelScale = 2;   // small label pixel font (works), digits will be larger
    int digitScale = 4;   // bigger digits for readability

    // left area: SCORE label + value
    int sx = 18;
    int sy = 8;
    // label box for contrast
    SDL_SetRenderDrawColor(renderer, 40, 48, 80, 220);
    SDL_Rect labScoreBox = { sx - 6, sy - 4, 120, 32 };
    SDL_RenderFillRect(renderer, &labScoreBox);
    // label text (pixel font) - small
    draw_text_pixel("SCORE", sx, sy + 2, labelScale, fg);
    // numeric value (right of label)
    char tmp[32];
    snprintf(tmp, sizeof(tmp), "%d", game_state.score);
    draw_number_right(sx + 110, sy + 4, digitScale, game_state.score, fg);

    // middle: LEVEL
    int mx = WINDOW_WIDTH/2 - 80;
    SDL_SetRenderDrawColor(renderer, 40, 48, 80, 220);
    SDL_Rect labLevelBox = { mx - 6, sy - 4, 160, 32 };
    SDL_RenderFillRect(renderer, &labLevelBox);
    draw_text_pixel("LEVEL", mx, sy + 2, labelScale, fg);
    draw_number_right(mx + 130, sy + 4, digitScale, game_state.level, fg);

    // right: LIVES (boxes)
    int rx = WINDOW_WIDTH - 20;
    int life_w = 26, life_h = 16, gap = 8;
    for (int i = game_state.lives - 1; i >= 0; --i) {
        SDL_Rect lifeRect = { rx - life_w, sy + 6, life_w, life_h };
        SDL_SetRenderDrawColor(renderer, 220, 80, 140, 255);
        SDL_RenderFillRect(renderer, &lifeRect);
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 80);
        SDL_RenderDrawRect(renderer, &lifeRect);
        rx -= (life_w + gap);
    }
    // -------------------------------------------------------------------


    // menu overlay
    if (game_state.show_menu) {
        SDL_Rect panel = { WINDOW_WIDTH/2 - 260, WINDOW_HEIGHT/2 - 120, 520, 240 };
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 160);
        SDL_RenderFillRect(renderer, &panel);
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 200);
        SDL_RenderDrawRect(renderer, &panel);
    } else if (game_state.is_paused) {
        SDL_Rect p = { WINDOW_WIDTH/2 - 180, WINDOW_HEIGHT/2 - 40, 360, 80 };
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 160);
        SDL_RenderFillRect(renderer, &p);
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 200);
        SDL_RenderDrawRect(renderer, &p);
    }
}



/* --------------------- INPUT & INIT --------------------- */
void handle_input(SDL_Event *ev) {
    if (ev->type == SDL_QUIT) { game_state.is_running = 0; }
    else if (ev->type == SDL_KEYDOWN) {
        SDL_Keycode k = ev->key.keysym.sym;
        if (k == SDLK_ESCAPE) { if (game_state.show_menu) game_state.is_running = 0; else game_state.show_menu = 1; }
        else if (k == SDLK_SPACE) {
            if (game_state.show_menu) { game_state.show_menu = 0; game_state.is_running = 1; reset_level(game_state.level); if (music_bgm) Mix_PlayMusic(music_bgm, -1); }
            else if (game_state.is_paused) game_state.is_paused = 0;
            else if (ball.is_held) { float ang = ((rand()%120)-60)*(M_PI/180.0f); ball.vx = sinf(ang); ball.vy = -fabsf(cosf(ang)); float m = sqrtf(ball.vx*ball.vx+ball.vy*ball.vy); ball.vx/=m; ball.vy/=m; ball.is_held = 0; }
            else game_state.is_paused = !game_state.is_paused;
        }
        else if (k == SDLK_r) reset_game();
        else if (k == SDLK_m) { if (Mix_PlayingMusic()) Mix_PausedMusic() ? Mix_ResumeMusic() : Mix_PauseMusic(); else if (music_bgm) Mix_PlayMusic(music_bgm, -1); }
    }
    else if (ev->type == SDL_MOUSEMOTION) { int mx = ev->motion.x; paddle.rect.x = mx - paddle.rect.w/2; clamp_paddle_position(); }
}

int init_color_palette() { color_palette[0]=(SDL_Color){255,120,120,255}; color_palette[1]=(SDL_Color){255,200,80,255}; color_palette[2]=(SDL_Color){110,255,170,255}; color_palette[3]=(SDL_Color){90,160,255,255}; color_palette[4]=(SDL_Color){210,90,200,255}; color_palette[5]=(SDL_Color){120,200,255,255}; color_palette[6]=(SDL_Color){255,150,60,255}; color_palette[7]=(SDL_Color){170,120,255,255}; color_palette[8]=(SDL_Color){160,255,200,255}; color_palette[9]=(SDL_Color){255,100,180,255}; return 1; }

void spawn_stars() { for (int i=0;i<NUM_STARS;i++) { stars[i].x = (float)(rand()% (WINDOW_WIDTH+200) - 100); stars[i].y = (float)(rand()% (WINDOW_HEIGHT+200) - 100); stars[i].layer = rand()%STAR_LAYERS; stars[i].size = 1.0f + (float)(rand()%3) + (STAR_LAYERS - stars[i].layer); // give small drift velocity based on layer
    stars[i].vx = (stars[i].layer+1) * ( (rand()%20 - 10) / 100.0f ); stars[i].vy = (stars[i].layer+1) * ( (rand()%20 - 10) / 100.0f ); } }

int load_audio_assets() { sfx_bounce = Mix_LoadWAV("bounce.wav"); sfx_break = Mix_LoadWAV("break.wav"); music_bgm = Mix_LoadMUS("bgm.mp3"); return 1; }

int initialize_all() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 0;
    }

    window = SDL_CreateWindow("Arkanoid - Space (Upgraded)",
                              SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_SHOWN);
    if (!window) {
        fprintf(stderr, "Window create fail: %s\n", SDL_GetError());
        return 0;
    }

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        fprintf(stderr, "Renderer fail: %s\n", SDL_GetError());
        return 0;
    }

    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 1024) < 0) {
        fprintf(stderr, "Mix_OpenAudio fail: %s\n", Mix_GetError());
        // continue without audio if necessary
    }

    init_color_palette();

    // default paddle and ball sizes
    paddle.rect.w = PADDLE_WIDTH; paddle.rect.h = PADDLE_HEIGHT;
    paddle.velocity_x = 0;
    ball.rect.w = BALL_SIZE; ball.rect.h = BALL_SIZE;

    srand((unsigned)time(NULL));
    spawn_stars();
    load_audio_assets();

    // init particles
    for (int i = 0; i < MAX_PARTICLES; i++) particles[i].alive = 0;

    return 1;
}


void cleanup_all() { if (sfx_bounce) Mix_FreeChunk(sfx_bounce); if (sfx_break) Mix_FreeChunk(sfx_break); if (music_bgm) Mix_FreeMusic(music_bgm); Mix_CloseAudio(); if (renderer) SDL_DestroyRenderer(renderer); if (window) SDL_DestroyWindow(window); SDL_Quit(); }

/* --------------------- MAIN LOOP --------------------- */
int main(int argc, char *argv[]) {
    (void)argc; (void)argv; if (!initialize_all()) return 1; paddle.rect.w = PADDLE_WIDTH; paddle.rect.h = PADDLE_HEIGHT; paddle.velocity_x = 0; ball.rect.w = BALL_SIZE; ball.rect.h = BALL_SIZE; reset_game(); Uint64 now = SDL_GetPerformanceCounter(); Uint64 last = 0; double delta_time = 0; SDL_Event ev; while (game_state.is_running) {
        last = now; now = SDL_GetPerformanceCounter(); delta_time = (double)((now - last) / (double)SDL_GetPerformanceFrequency()); if (delta_time > 0.05) delta_time = 0.05;
        while (SDL_PollEvent(&ev)) handle_input(&ev);
        const Uint8 *ks = SDL_GetKeyboardState(NULL); float vx = 0.0f; if (ks[SDL_SCANCODE_LEFT] || ks[SDL_SCANCODE_A]) vx = -PADDLE_SPEED; if (ks[SDL_SCANCODE_RIGHT] || ks[SDL_SCANCODE_D]) vx = PADDLE_SPEED; paddle.rect.x += vx * (float)delta_time; clamp_paddle_position(); update_engine((float)delta_time); SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND); render_scene(); SDL_RenderPresent(renderer); SDL_Delay(1);
    }
    cleanup_all(); return 0; }
