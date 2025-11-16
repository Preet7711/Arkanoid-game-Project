/* =====================================================================
   ARKANOID GAME - COMPONENT BREAKDOWN
   =====================================================================
   
   COMPONENT 1: GAME ENGINE & LOGIC CORE (Member 1 & 2)
   - Ball movement logic, paddle control, brick collisions
   - Score update mechanism, game state management
   - Functions: update_engine(), reset_game(), reset_level()
   
   COMPONENT 2: GRAPHICS & RENDERING (Member 3 & 4)
   - Frame buffer drawing, rendering bricks/ball/paddle
   - Visual effects (particles, glow, background)
   - Functions: render_scene(), draw_*() functions, spawn_stars()
   
   COMPONENT 3: INPUT HANDLING (Member 5 & 6)
   - Keyboard/mouse input for paddle movement and game control
   - Key mapping and event handling
   - Functions: handle_input(), SDL event processing in main loop
   
   COMPONENT 4: LEVELS, SCORING & PROGRESSION (Member 7 & 8)
   - Level design from files, score/lives tracking
   - Difficulty progression, leaderboard management
   - Functions: load_level_from_file(), load/save_highscore(), leaderboard functions
   
   COMPONENT 5: SOUND, UI & MENU SYSTEM (Member 9 & 10)
   - Start menu, pause, game-over screens
   - Background music and sound effects
   - Functions: draw_text_pixel(), menu rendering in render_scene(), load_audio_assets()
   
   ===================================================================== */

#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

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

#define NUM_STARS 220
#define STAR_LAYERS 3

#define MAX_PARTICLES 512
#define MAX_COLLECTIBLES 8

#define LEADERBOARD_N 5

/* --------------------- TYPES --------------------- */
typedef struct { float x, y, w, h; } RectF;
typedef struct { RectF rect; int is_alive; int color_index; int special; } Brick;
typedef struct { RectF rect; float velocity_x; } Paddle;
typedef struct { RectF rect; float vx, vy; float speed; int is_held; } Ball;
typedef struct { int score; int lives; int level; int bricks_remaining; int is_paused; int is_running; int show_menu; } GameState;
typedef struct { float x, y; float size; int layer; float vx, vy; } Star;
typedef struct { float x, y; float vx, vy; float life; float max_life; SDL_Color col; int alive; } Particle;
typedef struct { RectF rect; float vx, vy; int alive; int type; } Collectible;

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
Collectible collectibles[MAX_COLLECTIBLES];

RectF menu_play_rect;
int high_score = 0;
int leaderboard[LEADERBOARD_N];
const char *HIGH_SCORE_FILE = "highscore.dat";
const char *LEADERBOARD_FILE = "leaderboard.dat";

/* ========================================================================
   START: COMPONENT 1 - GAME ENGINE & LOGIC CORE (Member 1 & 2)
   ======================================================================== */
static inline int brick_index(int row, int col) { 
    return row * BRICK_COLUMNS + col; 
}

void clamp_paddle_position() { 
    if (paddle.rect.x < 0) paddle.rect.x = 0; 
    if (paddle.rect.x + paddle.rect.w > WINDOW_WIDTH) 
        paddle.rect.x = WINDOW_WIDTH - paddle.rect.w; 
}

int rect_overlap(RectF *a, RectF *b) { 
    return !(a->x + a->w <= b->x || b->x + b->w <= a->x || 
             a->y + a->h <= b->y || b->y + b->h <= a->y); 
}
/* ======================================================================== 
   END: COMPONENT 1 - GAME ENGINE & LOGIC CORE
   ======================================================================== */

/* ========================================================================
   START: COMPONENT 4 - LEVELS, SCORING & PROGRESSION (Member 7 & 8)
   ======================================================================== */
static void load_highscore(void) {
    FILE *f = fopen(HIGH_SCORE_FILE, "rb");
    if (!f) { high_score = 0; return; }
    int sc = 0;
    if (fread(&sc, sizeof(int), 1, f) == 1) high_score = sc;
    else high_score = 0;
    fclose(f);
}

static void save_highscore(void) {
    FILE *f = fopen(HIGH_SCORE_FILE, "wb");
    if (!f) return;
    fwrite(&high_score, sizeof(int), 1, f);
    fclose(f);
}

static void load_leaderboard(void) {
    FILE *f = fopen(LEADERBOARD_FILE, "rb");
    if (!f) { for (int i=0;i<LEADERBOARD_N;i++) leaderboard[i]=0; return; }
    size_t r = fread(leaderboard, sizeof(int), LEADERBOARD_N, f);
    if (r < (size_t)LEADERBOARD_N) for (int i=r;i<LEADERBOARD_N;i++) leaderboard[i]=0;
    fclose(f);
}

static void save_leaderboard(void) {
    FILE *f = fopen(LEADERBOARD_FILE, "wb");
    if (!f) return;
    fwrite(leaderboard, sizeof(int), LEADERBOARD_N, f);
    fclose(f);
}

static void add_to_leaderboard(int score) {
    if (score <= 0) return;
    int tmp[LEADERBOARD_N+1];
    int i,j,k=0;
    for (i=0;i<LEADERBOARD_N;i++) tmp[i]=leaderboard[i];
    tmp[LEADERBOARD_N]=score;
    for (i=0;i<LEADERBOARD_N+1;i++) {
        for (j=i+1;j<LEADERBOARD_N+1;j++) 
            if (tmp[j] > tmp[i]) { 
                int t=tmp[i]; tmp[i]=tmp[j]; tmp[j]=t; 
            }
    }
    for (i=0;i<LEADERBOARD_N;i++) leaderboard[i]=tmp[i];
    save_leaderboard();
}

int load_level_from_file(int level) {
    char name[128]; 
    snprintf(name, sizeof(name), "level%d.txt", level);
    FILE *f = fopen(name, "r");
    if (!f) return 0;
    char line[256];
    for (int r = 0; r < BRICK_ROWS; r++) {
        if (!fgets(line, sizeof(line), f)) {
            for (int c = 0; c < BRICK_COLUMNS; c++) {
                Brick *b = &bricks[brick_index(r,c)];
                b->rect.w = BRICK_WIDTH - BRICK_PADDING; 
                b->rect.h = BRICK_HEIGHT - BRICK_PADDING;
                b->rect.x = c * BRICK_WIDTH + BRICK_PADDING/2; 
                b->rect.y = 80 + r * (BRICK_HEIGHT + BRICK_PADDING);
                b->is_alive = 0; b->special = 0; 
                b->color_index = (r + c + level) % 10;
            }
            continue;
        }
        for (int c = 0; c < BRICK_COLUMNS; c++) {
            Brick *b = &bricks[brick_index(r,c)];
            b->rect.w = BRICK_WIDTH - BRICK_PADDING; 
            b->rect.h = BRICK_HEIGHT - BRICK_PADDING;
            b->rect.x = c * BRICK_WIDTH + BRICK_PADDING/2; 
            b->rect.y = 80 + r * (BRICK_HEIGHT + BRICK_PADDING);
            char ch = (c < (int)strlen(line)) ? line[c] : '.';
            if (ch == '#') { b->is_alive = 1; b->special = 0; }
            else if (ch == 'A') { b->is_alive = 1; b->special = 1; }
            else { b->is_alive = 0; b->special = 0; }
            b->color_index = (r + c + level) % 10;
        }
    }
    fclose(f);
    int alive = 0; 
    for (int i=0;i<BRICK_ROWS*BRICK_COLUMNS;i++) 
        if (bricks[i].is_alive) alive++;
    game_state.bricks_remaining = alive;
    return 1;
}

void reset_level(int level) {
    if (load_level_from_file(level)) {
        // loaded from file
    } else {
        int alive_count = 0;
        for (int r = 0; r < BRICK_ROWS; r++) {
            for (int c = 0; c < BRICK_COLUMNS; c++) {
                Brick *b = &bricks[brick_index(r,c)];
                b->rect.w = BRICK_WIDTH - BRICK_PADDING; 
                b->rect.h = BRICK_HEIGHT - BRICK_PADDING;
                b->rect.x = c * BRICK_WIDTH + BRICK_PADDING/2; 
                b->rect.y = 80 + r * (BRICK_HEIGHT + BRICK_PADDING);
                if ((level <= 1) || ((r + c + level) % (1 + level / 2) != 0)) {
                    b->is_alive = 1; alive_count++; 
                    b->special = (rand()%18==0) ? 1 : 0;
                } else { 
                    b->is_alive = 0; b->special = 0; 
                }
                b->color_index = (r + c + level) % 10;
            }
        }
        game_state.bricks_remaining = alive_count;
    }
    paddle.rect.x = (WINDOW_WIDTH - paddle.rect.w) / 2.0f; 
    paddle.rect.y = WINDOW_HEIGHT - PADDLE_Y_OFFSET;
    ball.rect.x = paddle.rect.x + (paddle.rect.w - ball.rect.w) / 2.0f; 
    ball.rect.y = paddle.rect.y - ball.rect.h - 2; 
    ball.vx = 0; ball.vy = -1; 
    ball.speed = BALL_SPEED_INITIAL; 
    ball.is_held = 1;
    for (int ci=0; ci<MAX_COLLECTIBLES; ci++) 
        collectibles[ci].alive = 0;
}

void reset_game() { 
    game_state.score = 0; 
    game_state.lives = STARTING_LIVES; 
    game_state.level = 1; 
    game_state.is_paused = 0; 
    game_state.is_running = 1; 
    game_state.show_menu = 1; 
    reset_level(game_state.level); 
}
/* ======================================================================== 
   END: COMPONENT 4 - LEVELS, SCORING & PROGRESSION
   ======================================================================== */

/* ========================================================================
   START: COMPONENT 2 - GRAPHICS & RENDERING (Member 3 & 4)
   ======================================================================== */
void spawn_particles(float x, float y, SDL_Color col, int count) {
    for (int i = 0; i < MAX_PARTICLES && count > 0; i++) {
        if (!particles[i].alive) {
            particles[i].alive = 1; 
            particles[i].x = x; 
            particles[i].y = y;
            float ang = ((rand()%360) * (M_PI/180.0f));
            float sp = 60 + (rand()%120);
            particles[i].vx = cosf(ang)*sp; 
            particles[i].vy = sinf(ang)*sp;
            particles[i].life = 0.0f; 
            particles[i].max_life = 0.5f + ((rand()%100)/200.0f);
            particles[i].col = col; 
            count--;
        }
    }
}

void update_particles(float dt) {
    for (int i=0;i<MAX_PARTICLES;i++) {
        if (!particles[i].alive) continue;
        particles[i].x += particles[i].vx * dt; 
        particles[i].y += particles[i].vy * dt; 
        particles[i].vy += 200.0f * dt;
        particles[i].life += dt; 
        if (particles[i].life >= particles[i].max_life) 
            particles[i].alive = 0;
    }
}

void update_collectibles(float dt) {
    for (int i=0;i<MAX_COLLECTIBLES;i++) {
        if (!collectibles[i].alive) continue;
        collectibles[i].rect.x += collectibles[i].vx * dt;
        collectibles[i].rect.y += collectibles[i].vy * dt;
        if (collectibles[i].rect.y > WINDOW_HEIGHT) 
            collectibles[i].alive = 0;
        RectF pr = collectibles[i].rect;
        if (rect_overlap(&pr, &paddle.rect)) {
            if (collectibles[i].type == 0) {
                paddle.rect.w += 40; 
                if (paddle.rect.w > WINDOW_WIDTH/2) 
                    paddle.rect.w = WINDOW_WIDTH/2; 
                clamp_paddle_position();
            }
            collectibles[i].alive = 0;
        }
    }
}
/* ======================================================================== 
   END: COMPONENT 2 - GRAPHICS & RENDERING
   ======================================================================== */

/* ========================================================================
   START: COMPONENT 1 - GAME ENGINE & LOGIC CORE (Member 1 & 2)
   ======================================================================== */
void add_score_for_brick(int row, int col) { 
    (void)row; (void)col; 
    game_state.score += 10; 
}

void update_engine(float dt) {
    if (!game_state.is_running || game_state.is_paused || game_state.show_menu) 
        return;
    
    if (!ball.is_held) { 
        ball.rect.x += ball.vx * ball.speed * dt; 
        ball.rect.y += ball.vy * ball.speed * dt; 
    } else { 
        ball.rect.x = paddle.rect.x + (paddle.rect.w - ball.rect.w)/2.0f; 
        ball.rect.y = paddle.rect.y - ball.rect.h - 2; 
    }

    if (!ball.is_held) {
        if (ball.rect.x <= 0) { 
            ball.rect.x = 0; 
            ball.vx = fabsf(ball.vx); 
            if (sfx_bounce) Mix_PlayChannel(-1, sfx_bounce, 0); 
        }
        if (ball.rect.x + ball.rect.w >= WINDOW_WIDTH) { 
            ball.rect.x = WINDOW_WIDTH - ball.rect.w; 
            ball.vx = -fabsf(ball.vx); 
            if (sfx_bounce) Mix_PlayChannel(-1, sfx_bounce, 0); 
        }
        if (ball.rect.y <= 0) { 
            ball.rect.y = 0; 
            ball.vy = fabsf(ball.vy); 
            if (sfx_bounce) Mix_PlayChannel(-1, sfx_bounce, 0); 
        }
    }

    if (!ball.is_held && ball.vy > 0 && rect_overlap(&ball.rect, &paddle.rect)) {
        float impact = ((ball.rect.x + ball.rect.w/2.0f) - (paddle.rect.x + paddle.rect.w/2.0f)) / (paddle.rect.w/2.0f);
        if (impact < -1) impact = -1; 
        if (impact > 1) impact = 1; 
        float angle = impact * (75.0f * (M_PI/180.0f));
        ball.vx = sinf(angle); 
        ball.vy = -cosf(angle); 
        ball.speed *= BALL_SPEED_GROWTH; 
        ball.rect.y = paddle.rect.y - ball.rect.h - 1; 
        if (sfx_bounce) Mix_PlayChannel(-1, sfx_bounce, 0);
    }

    if (!ball.is_held) {
        for (int r=0;r<BRICK_ROWS;r++) {
            for (int c=0;c<BRICK_COLUMNS;c++) {
                Brick *b = &bricks[brick_index(r,c)]; 
                if (!b->is_alive) continue; 
                RectF br = b->rect;
                if (rect_overlap(&ball.rect, &br)) {
                    float ol = (ball.rect.x + ball.rect.w) - br.x; 
                    float or = (br.x + br.w) - ball.rect.x; 
                    float ot = (ball.rect.y + ball.rect.h) - br.y; 
                    float ob = (br.y + br.h) - ball.rect.y; 
                    float m = ol; 
                    m = fminf(m, or); 
                    m = fminf(m, ot); 
                    m = fminf(m, ob);
                    
                    if (m == ol) { 
                        ball.rect.x -= ol; 
                        ball.vx = -fabsf(ball.vx); 
                    }
                    else if (m == or) { 
                        ball.rect.x += or; 
                        ball.vx = fabsf(ball.vx); 
                    }
                    else if (m == ot) { 
                        ball.rect.y -= ot; 
                        ball.vy = -fabsf(ball.vy); 
                    }
                    else { 
                        ball.rect.y += ob; 
                        ball.vy = fabsf(ball.vy); 
                    }

                    b->is_alive = 0; 
                    game_state.bricks_remaining--;
                    
                    if (b->special) {
                        float cx = b->rect.x + b->rect.w/2.0f; 
                        float cy = b->rect.y + b->rect.h/2.0f;
                        for (int ci=0; ci<MAX_COLLECTIBLES; ci++) {
                            if (!collectibles[ci].alive) {
                                collectibles[ci].alive = 1; 
                                collectibles[ci].rect.x = cx - 10; 
                                collectibles[ci].rect.y = cy - 10; 
                                collectibles[ci].rect.w = 20; 
                                collectibles[ci].rect.h = 20; 
                                collectibles[ci].vx = 0; 
                                collectibles[ci].vy = 60.0f; 
                                collectibles[ci].type = 0; 
                                break;
                            }
                        }
                        b->special = 0;
                    }

                    add_score_for_brick(r,c);
                    if (sfx_break) Mix_PlayChannel(-1, sfx_break, 0);
                    SDL_Color pc = color_palette[b->color_index % 10]; 
                    spawn_particles(ball.rect.x + ball.rect.w/2, ball.rect.y + ball.rect.h/2, pc, 18);
                    ball.speed *= 1.015f; 
                    goto after_brick;
                }
            }
        }
    }
after_brick: ;

    if (!ball.is_held && ball.rect.y > WINDOW_HEIGHT) {
        game_state.lives--; 
        if (sfx_bounce) Mix_PlayChannel(-1, sfx_bounce, 0);
        if (game_state.lives <= 0) {
            if (game_state.score > high_score) { 
                high_score = game_state.score; 
                save_highscore(); 
            }
            add_to_leaderboard(game_state.score);
            game_state.show_menu = 1; 
            game_state.is_running = 0;
        } else {
            ball.is_held = 1; 
            ball.speed = BALL_SPEED_INITIAL; 
            ball.vx = 0; 
            ball.vy = -1; 
            paddle.rect.x = (WINDOW_WIDTH - paddle.rect.w)/2.0f;
        }
    }

    if (game_state.bricks_remaining <= 0) {
        game_state.level++;
        if (game_state.level > MAX_LEVELS) {
            if (game_state.score > high_score) { 
                high_score = game_state.score; 
                save_highscore(); 
            }
            add_to_leaderboard(game_state.score);
            game_state.show_menu = 1; 
            game_state.is_running = 0;
        } else {
            reset_level(game_state.level);
        }
    }

    update_particles(dt); 
    update_collectibles(dt);

    for (int i=0;i<NUM_STARS;i++) {
        stars[i].x += stars[i].vx * dt; 
        stars[i].y += stars[i].vy * dt;
        if (stars[i].x < -20) stars[i].x = WINDOW_WIDTH + 20; 
        if (stars[i].x > WINDOW_WIDTH+20) stars[i].x = -20;
        if (stars[i].y < -20) stars[i].y = WINDOW_HEIGHT + 20; 
        if (stars[i].y > WINDOW_HEIGHT+20) stars[i].y = -20;
    }
}
/* ======================================================================== 
   END: COMPONENT 1 - GAME ENGINE & LOGIC CORE
   ======================================================================== */

/* ========================================================================
   START: COMPONENT 2 - GRAPHICS & RENDERING (Member 3 & 4)
   ======================================================================== */
void draw_rectf(SDL_Renderer *ren, RectF *f) { 
    SDL_Rect rr = { (int)f->x, (int)f->y, (int)f->w, (int)f->h }; 
    SDL_RenderFillRect(ren, &rr); 
}

void draw_ball_with_glow(Ball *b) {
    int rings = 6;
    for (int i = rings; i >= 1; i--) {
        float t = (float)i / (float)rings;
        Uint8 a = (Uint8)(40 * t);
        SDL_SetRenderDrawColor(renderer, 255, 240, 180, a);
        RectF gr = { b->rect.x - (rings-i)*2.0f, b->rect.y - (rings-i)*2.0f, 
                     b->rect.w + (rings-i)*4.0f, b->rect.h + (rings-i)*4.0f };
        draw_rectf(renderer, &gr);
    }
    SDL_SetRenderDrawColor(renderer, 255, 240, 180, 255); 
    draw_rectf(renderer, &b->rect);
}

void draw_paddle(Paddle *p) {
    SDL_SetRenderDrawColor(renderer, 30, 90, 140, 255); 
    draw_rectf(renderer, &p->rect);
    SDL_SetRenderDrawColor(renderer, 220, 240, 255, 255);
    RectF top = { p->rect.x + 4, p->rect.y + 2, p->rect.w - 8, p->rect.h/2 - 2 }; 
    draw_rectf(renderer, &top);
}

void draw_textured_brick(Brick *b) {
    SDL_Color base = color_palette[b->color_index % 10]; 
    SDL_SetRenderDrawColor(renderer, base.r, base.g, base.b, 255); 
    draw_rectf(renderer, &b->rect);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 110); 
    RectF shine = { b->rect.x + 6, b->rect.y + 4, b->rect.w * 0.5f, b->rect.h * 0.35f }; 
    draw_rectf(renderer, &shine);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 40); 
    RectF shadow = { b->rect.x + 4, b->rect.y + b->rect.h - 6, b->rect.w - 6, 6 }; 
    draw_rectf(renderer, &shadow);
}
/* ======================================================================== 
   END: COMPONENT 2 - GRAPHICS & RENDERING
   ======================================================================== */

/* ========================================================================
   START: COMPONENT 5 - SOUND, UI & MENU SYSTEM (Member 9 & 10)
   ======================================================================== */
static const unsigned char FONT_5x7[][5] = {
    {0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},{0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},
    {0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},
    {0x7C,0x12,0x11,0x12,0x7C},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},{0x7F,0x41,0x41,0x22,0x1C},
    {0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},{0x3E,0x41,0x49,0x49,0x7A},{0x7F,0x08,0x08,0x08,0x7F},
    {0x00,0x41,0x7F,0x41,0x00},{0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},{0x7F,0x40,0x40,0x40,0x40},
    {0x7F,0x02,0x04,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},{0x7F,0x09,0x09,0x09,0x06},
    {0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7F,0x01,0x01},
    {0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},{0x63,0x14,0x08,0x14,0x63},
    {0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43}
};

static int char_index_for_hud(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'A' && ch <= 'Z') return 10 + (ch - 'A');
    return -1;
}

void draw_glyph(char ch, int x, int y, int scale, SDL_Color color) {
    int idx = char_index_for_hud(ch);
    if (idx < 0) return;
    const unsigned char *cols = FONT_5x7[idx];
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    for (int col = 0; col < 5; col++) {
        unsigned char colbits = cols[col];
        for (int row = 0; row < 7; row++) {
            int bit = (colbits >> row) & 1;
            if (bit) {
                SDL_Rect r = { x + col * scale, y + row * scale, scale, scale };
                SDL_RenderFillRect(renderer, &r);
            }
        }
    }
}

void draw_text_pixel(const char *s, int x, int y, int scale, SDL_Color col) {
    int cx = x;
    for (size_t i = 0; i < strlen(s); ++i) {
        char c = s[i];
        if (c == ' ') { cx += (6 * scale); continue; }
        draw_glyph(c, cx, y, scale, col);
        cx += (6 * scale);
    }
}

void draw_number_left(int x, int y, int scale, int value, SDL_Color col) {
    char buf[32];
    if (value < 0) value = 0;
    snprintf(buf, sizeof(buf), "%d", value);
    int len = (int)strlen(buf);
    for (int i = 0; i < len; ++i) 
        draw_glyph(buf[i], x + i * (6 * scale), y, scale, col);
}

void draw_number_right(int rx, int y, int scale, int value, SDL_Color col) {
    char buf[32];
    if (value < 0) value = 0;
    snprintf(buf, sizeof(buf), "%d", value);
    int len = (int)strlen(buf);
    int total_w = len * (6 * scale);
    int start = rx - total_w + 1;
    for (int i = 0; i < len; ++i) 
        draw_glyph(buf[i], start + i * (6 * scale), y, scale, col);
}
/* ======================================================================== 
   END: COMPONENT 5 - SOUND, UI & MENU SYSTEM
   ======================================================================== */

/* ========================================================================
   START: COMPONENT 2 - GRAPHICS & RENDERING (Member 3 & 4)
   ======================================================================== */
void spawn_stars() { 
    for (int i=0;i<NUM_STARS;i++) { 
        stars[i].x = (float)(rand()% (WINDOW_WIDTH+200) - 100); 
        stars[i].y = (float)(rand()% (WINDOW_HEIGHT+200) - 100); 
        stars[i].layer = rand()%STAR_LAYERS; 
        stars[i].size = 1.0f + (float)(rand()%3) + (STAR_LAYERS - stars[i].layer); 
        stars[i].vx = (stars[i].layer+1) * ( (rand()%20 - 10) / 100.0f ); 
        stars[i].vy = (stars[i].layer+1) * ( (rand()%20 - 10) / 100.0f ); 
    } 
}

void draw_space_background(SDL_Renderer *ren, float tsec) {
    for (int y = 0; y < WINDOW_HEIGHT; y += 2) {
        float ty = (float)y / (float)WINDOW_HEIGHT;
        Uint8 r = (Uint8)(8 + ty * 10);
        Uint8 g = (Uint8)(10 + ty * 20);
        Uint8 b = (Uint8)(28 + ty * 50);
        SDL_SetRenderDrawColor(ren, r, g, b, 255);
        SDL_Rect line = {0, y, WINDOW_WIDTH, 2};
        SDL_RenderFillRect(ren, &line);
    }

    float offset = sinf(tsec * 0.12f) * 60.0f;
    for (int i = 0; i < 80; i++) {
        float py = WINDOW_HEIGHT * 0.25f + sinf(i * 0.12f + offset * 0.01f) * 16.0f + offset * 0.05f;
        float width = WINDOW_WIDTH * (0.5f + 0.12f * sinf(i * 0.3f + offset * 0.02f));
        Uint8 alpha = (Uint8)(20 + (i % 4) * 6);
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(ren, 120, 40, 200, alpha);
        SDL_Rect band = { (int)(WINDOW_WIDTH / 2 - width / 2), (int)(py + i * 1.0f), (int)width, 6 };
        SDL_RenderFillRect(ren, &band);
    }

    for (int i = 0; i < NUM_STARS; i++) {
        Star *s = &stars[i];
        int br = (int)fminf(255.0f, 180.0f + 40.0f * (1.0f / (s->layer + 1)));
        if (br < 0) br = 0;
        if (br > 255) br = 255;
        SDL_SetRenderDrawColor(ren, (Uint8)br, (Uint8)br, (Uint8)br, 255);
        SDL_Rect sr = { (int)s->x, (int)s->y, (int)fmaxf(1.0f, s->size), (int)fmaxf(1.0f, s->size) };
        SDL_RenderFillRect(ren, &sr);
    }
}

void render_scene() {
    float tsec = (float)(SDL_GetTicks() / 1000.0f);
    draw_space_background(renderer, tsec);

    for (int r = 0; r < BRICK_ROWS; r++) {
        for (int c = 0; c < BRICK_COLUMNS; c++) { 
            Brick *b = &bricks[brick_index(r,c)]; 
            if (b->is_alive) draw_textured_brick(b); 
        }
    }

    for (int i = 0; i < MAX_PARTICLES; i++) { 
        if (!particles[i].alive) continue; 
        float life_t = particles[i].life / particles[i].max_life; 
        Uint8 a = (Uint8)(255 * (1.0f - life_t)); 
        SDL_SetRenderDrawColor(renderer, particles[i].col.r, particles[i].col.g, particles[i].col.b, a); 
        SDL_Rect pr = { (int)particles[i].x, (int)particles[i].y, 3, 3 }; 
        SDL_RenderFillRect(renderer, &pr); 
    }

    for (int ci = 0; ci < MAX_COLLECTIBLES; ci++) { 
        if (!collectibles[ci].alive) continue; 
        SDL_SetRenderDrawColor(renderer, 255, 200, 80, 255); 
        SDL_Rect cr = { (int)collectibles[ci].rect.x, (int)collectibles[ci].rect.y, 
                        (int)collectibles[ci].rect.w, (int)collectibles[ci].rect.h }; 
        SDL_RenderFillRect(renderer, &cr); 
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 100); 
        SDL_RenderDrawRect(renderer, &cr); 
    }

    draw_paddle(&paddle);
    draw_ball_with_glow(&ball);

    SDL_Rect hudStrip = { 0, 0, WINDOW_WIDTH, 44 };
    SDL_SetRenderDrawColor(renderer, 6, 8, 20, 220);
    SDL_RenderFillRect(renderer, &hudStrip);

    SDL_Color fg = { 235, 235, 255, 255 };
    int labelScale = 2;
    int digitScale = 4;

    int sx = 18;
    int sy = 8;
    SDL_SetRenderDrawColor(renderer, 40, 48, 80, 220);
    SDL_Rect labScoreBox = { sx - 6, sy - 4, 160, 32 };
    SDL_RenderFillRect(renderer, &labScoreBox);
    draw_text_pixel("SCORE", sx, sy + 2, labelScale, fg);
    int score_x_right = sx + 150;
    draw_number_right(score_x_right, sy + 4, digitScale, game_state.score, fg);

    int mx = WINDOW_WIDTH/2 - 80;
    SDL_SetRenderDrawColor(renderer, 40, 48, 80, 220);
    SDL_Rect labLevelBox = { mx - 6, sy - 4, 160, 32 };
    SDL_RenderFillRect(renderer, &labLevelBox);
    draw_text_pixel("LEVEL", mx, sy + 2, labelScale, fg);
    draw_number_right(mx + 130, sy + 4, digitScale, game_state.level, fg);

    int rx = WINDOW_WIDTH - 20;
    int heart_w = 20, heart_h = 18, gap = 10;
    for (int i = 0; i < game_state.lives; i++) {
        int hx = rx - heart_w;
        int hy = sy + 6;
        SDL_SetRenderDrawColor(renderer, 255, 80, 120, 255);
        SDL_Rect left = { hx, hy, heart_w/2, heart_h/2 };
        SDL_Rect right = { hx + heart_w/2, hy, heart_w/2, heart_h/2 };
        SDL_Rect bottom = { hx + heart_w/4, hy + heart_h/4, heart_w/2, heart_h*3/4 };
        SDL_RenderFillRect(renderer, &left);
        SDL_RenderFillRect(renderer, &right);
        SDL_RenderFillRect(renderer, &bottom);
        rx -= (heart_w + gap);
    }

    if (game_state.show_menu) {
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 200);
        SDL_Rect full = {0,0,WINDOW_WIDTH,WINDOW_HEIGHT};
        SDL_RenderFillRect(renderer,&full);

        SDL_Color titleCol = {255,140,70,255};
        int titleScale = 10;
        const char *title = "ARKANOID";
        int tw = (int)strlen(title) * ((5 * titleScale) + titleScale);
        int tx = (WINDOW_WIDTH - tw) / 2;
        int ty = 80;
        draw_text_pixel(title, tx, ty, titleScale, titleCol);

        SDL_Color scoreCol = {255,80,80,255};
        int hs_x = WINDOW_WIDTH/2 - 60;
        draw_text_pixel("HIGH SCORE", hs_x, 18, 2, scoreCol);
        int label_w = (int)strlen("HIGH SCORE") * (6 * 2);
        int num_x = hs_x + label_w + 8;
        draw_number_left(num_x, 18 + 6, 3, high_score, (SDL_Color){255,255,255,255});

        int pw = 220, ph = 72;
        menu_play_rect.x = (WINDOW_WIDTH - pw)/2; 
        menu_play_rect.y = ty + 180; 
        menu_play_rect.w = pw; 
        menu_play_rect.h = ph;
        SDL_Rect pr = {(int)menu_play_rect.x, (int)menu_play_rect.y, 
                       (int)menu_play_rect.w, (int)menu_play_rect.h};
        SDL_SetRenderDrawColor(renderer, 40,20,90,220);
        SDL_RenderFillRect(renderer,&pr);
        draw_text_pixel("PLAY", (int)menu_play_rect.x + 56, (int)menu_play_rect.y + 12, 6, 
                       (SDL_Color){255,180,200,255});
        draw_text_pixel("TAP TO START", WINDOW_WIDTH/2 - 70, (int)menu_play_rect.y + ph + 18, 2, 
                       (SDL_Color){200,200,220,200});

        int lb_x = WINDOW_WIDTH/2 - 140;
        int lb_y = (int)menu_play_rect.y + ph + 60;
        draw_text_pixel("LEADERBOARD", lb_x, lb_y, 2, (SDL_Color){200,180,240,255});
        for (int i=0;i<LEADERBOARD_N;i++) {
            char rankbuf[32];
            snprintf(rankbuf, sizeof(rankbuf), "%d.", i+1);
            draw_text_pixel(rankbuf, lb_x, lb_y + 26 + i*22, 2, (SDL_Color){220,220,220,230});
            int sxpos = lb_x + (int)strlen(rankbuf) * (6*2) + 6;
            draw_number_left(sxpos, lb_y + 26 + i*22, 2, leaderboard[i], (SDL_Color){255,255,255,255});
        }
    }
}
/* ======================================================================== 
   END: COMPONENT 2 - GRAPHICS & RENDERING
   ======================================================================== */

/* ========================================================================
   START: COMPONENT 3 - INPUT HANDLING (Member 5 & 6)
   ======================================================================== */
void handle_input(SDL_Event *ev) {
    if (ev->type == SDL_QUIT) { 
        game_state.is_running = 0; 
    }
    else if (ev->type == SDL_KEYDOWN) {
        SDL_Keycode k = ev->key.keysym.sym;
        if (k == SDLK_ESCAPE) { 
            if (game_state.show_menu) 
                game_state.is_running = 0; 
            else 
                game_state.show_menu = 1; 
        }
        else if (k == SDLK_SPACE) {
            if (game_state.show_menu) { 
                game_state.show_menu = 0; 
                game_state.is_running = 1; 
                reset_level(game_state.level); 
                if (music_bgm) Mix_PlayMusic(music_bgm, -1); 
            }
            else if (game_state.is_paused) 
                game_state.is_paused = 0;
            else if (ball.is_held) { 
                float ang = ((rand()%120)-60)*(M_PI/180.0f); 
                ball.vx = sinf(ang); 
                ball.vy = -fabsf(cosf(ang)); 
                float m = sqrtf(ball.vx*ball.vx+ball.vy*ball.vy); 
                ball.vx/=m; 
                ball.vy/=m; 
                ball.is_held = 0; 
            }
            else 
                game_state.is_paused = !game_state.is_paused;
        }
        else if (k == SDLK_r) 
            reset_game();
        else if (k == SDLK_m) { 
            if (Mix_PlayingMusic()) 
                Mix_PausedMusic() ? Mix_ResumeMusic() : Mix_PauseMusic(); 
            else if (music_bgm) 
                Mix_PlayMusic(music_bgm, -1); 
        }
    }
    else if (ev->type == SDL_MOUSEMOTION) { 
        int mx = ev->motion.x; 
        paddle.rect.x = mx - paddle.rect.w/2; 
        clamp_paddle_position(); 
    }
    else if (ev->type == SDL_MOUSEBUTTONDOWN) {
        int mx = ev->button.x, my = ev->button.y;
        if (game_state.show_menu) {
            if (mx >= (int)menu_play_rect.x && mx <= (int)(menu_play_rect.x + menu_play_rect.w) && 
                my >= (int)menu_play_rect.y && my <= (int)(menu_play_rect.y + menu_play_rect.h)) {
                game_state.show_menu = 0; 
                game_state.is_running = 1; 
                reset_level(game_state.level); 
                if (music_bgm) Mix_PlayMusic(music_bgm, -1);
            }
        }
    }
}
/* ======================================================================== 
   END: COMPONENT 3 - INPUT HANDLING
   ======================================================================== */

/* ========================================================================
   START: COMPONENT 2 - GRAPHICS & RENDERING (Member 3 & 4)
   ======================================================================== */
int init_color_palette() { 
    color_palette[0]=(SDL_Color){255,120,120,255}; 
    color_palette[1]=(SDL_Color){255,200,80,255}; 
    color_palette[2]=(SDL_Color){110,255,170,255}; 
    color_palette[3]=(SDL_Color){90,160,255,255}; 
    color_palette[4]=(SDL_Color){210,90,200,255}; 
    color_palette[5]=(SDL_Color){120,200,255,255}; 
    color_palette[6]=(SDL_Color){255,150,60,255}; 
    color_palette[7]=(SDL_Color){170,120,255,255}; 
    color_palette[8]=(SDL_Color){160,255,200,255}; 
    color_palette[9]=(SDL_Color){255,100,180,255}; 
    return 1; 
}
/* ======================================================================== 
   END: COMPONENT 2 - GRAPHICS & RENDERING
   ======================================================================== */

/* ========================================================================
   START: COMPONENT 5 - SOUND, UI & MENU SYSTEM (Member 9 & 10)
   ======================================================================== */
int load_audio_assets() { 
    sfx_bounce = Mix_LoadWAV("bounce_real.wav"); 
    sfx_break = Mix_LoadWAV("break_real.wav"); 
    music_bgm = Mix_LoadMUS("bgm_arcade.wav"); 
    return 1; 
}

int initialize_all() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) { 
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError()); 
        return 0; 
    }
    window = SDL_CreateWindow("Arkanoid - Final", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 
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
    }
    init_color_palette();
    paddle.rect.w = PADDLE_WIDTH; 
    paddle.rect.h = PADDLE_HEIGHT; 
    paddle.velocity_x = 0; 
    ball.rect.w = BALL_SIZE; 
    ball.rect.h = BALL_SIZE;
    srand((unsigned)time(NULL)); 
    spawn_stars(); 
    load_audio_assets();
    load_highscore(); 
    load_leaderboard();
    for (int i = 0; i < MAX_PARTICLES; i++) 
        particles[i].alive = 0;
    for (int i = 0; i < MAX_COLLECTIBLES; i++) 
        collectibles[i].alive = 0;
    return 1;
}

void cleanup_all() {
    if (game_state.score > high_score) { 
        high_score = game_state.score; 
        save_highscore(); 
    }
    if (sfx_bounce) Mix_FreeChunk(sfx_bounce); 
    if (sfx_break) Mix_FreeChunk(sfx_break); 
    if (music_bgm) Mix_FreeMusic(music_bgm);
    Mix_CloseAudio();
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
}
/* ======================================================================== 
   END: COMPONENT 5 - SOUND, UI & MENU SYSTEM
   ======================================================================== */

/* ========================================================================
   MAIN LOOP - ALL COMPONENTS INTEGRATED
   - COMPONENT 3: Input polling (handle_input, keyboard state)
   - COMPONENT 1: Game engine updates (update_engine)
   - COMPONENT 2: Rendering (render_scene)
   ======================================================================== */
int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    if (!initialize_all()) return 1;
    paddle.rect.w = PADDLE_WIDTH; 
    paddle.rect.h = PADDLE_HEIGHT; 
    paddle.velocity_x = 0; 
    ball.rect.w = BALL_SIZE; 
    ball.rect.h = BALL_SIZE; 
    reset_game();
    Uint64 now = SDL_GetPerformanceCounter(); 
    Uint64 last = 0; 
    double delta_time = 0; 
    SDL_Event ev;
    while (game_state.is_running) {
        last = now; 
        now = SDL_GetPerformanceCounter(); 
        delta_time = (double)((now - last) / (double)SDL_GetPerformanceFrequency()); 
        if (delta_time > 0.05) delta_time = 0.05;
        
        while (SDL_PollEvent(&ev)) handle_input(&ev);
        
        const Uint8 *ks = SDL_GetKeyboardState(NULL); 
        float vx = 0.0f; 
        if (ks[SDL_SCANCODE_LEFT] || ks[SDL_SCANCODE_A]) vx = -PADDLE_SPEED; 
        if (ks[SDL_SCANCODE_RIGHT] || ks[SDL_SCANCODE_D]) vx = PADDLE_SPEED; 
        paddle.rect.x += vx * (float)delta_time; 
        clamp_paddle_position(); 
        
        update_engine((float)delta_time); 
        
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND); 
        render_scene(); 
        SDL_RenderPresent(renderer); 
        SDL_Delay(1);
    }
    cleanup_all();
    return 0;
}
