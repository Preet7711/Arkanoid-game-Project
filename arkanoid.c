// ============================================================================
// ARKANOID GAME - TEAM PROJECT (Windows Console Version)
// ============================================================================
// Compile: gcc arkanoid.c -o arkanoid.exe -lgdi32
// Run: arkanoid.exe
// ============================================================================

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <stdbool.h>
#include <time.h>

// ============================================================================
// CONSTANTS AND CONFIGURATIONS
// ============================================================================
#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 600
#define FPS 60
#define FRAME_DELAY (1000 / FPS)

// Paddle settings
#define PADDLE_WIDTH 100
#define PADDLE_HEIGHT 20
#define PADDLE_SPEED 12
#define PADDLE_Y (WINDOW_HEIGHT - 80)

// Ball settings
#define BALL_SIZE 15
#define BALL_SPEED 6

// Brick settings
#define BRICK_ROWS 4
#define BRICK_COLS 14
#define BRICK_WIDTH 50
#define BRICK_HEIGHT 20
#define BRICK_PADDING 5
#define BRICK_OFFSET_X 15
#define BRICK_OFFSET_Y 50

// Game settings
#define MAX_LIVES 3
#define MAX_LEVELS 3

// Global variables for Windows
HWND hwnd;
HDC hdc;
bool keys[256] = {false};
bool running = true;

// ============================================================================
// COMPONENT 1: GAME ENGINE & LOGIC CORE (Member 1 & 2)
// Handles: Ball movement, paddle control, collision detection, physics
// ============================================================================

// Ball structure
typedef struct {
    float x, y;
    float dx, dy;
    int radius;
    bool active;
} Ball;

// Paddle structure
typedef struct {
    int x, y;
    int width, height;
} Paddle;

// Brick structure
typedef struct {
    int x, y;
    int width, height;
    bool active;
    int color_type;
} Brick;

// Game state structure
typedef struct {
    int score;
    int lives;
    int level;
    bool paused;
    bool game_over;
    bool victory;
} GameState;

// Initialize ball
void init_ball(Ball *ball) {
    ball->x = WINDOW_WIDTH / 2;
    ball->y = WINDOW_HEIGHT / 2;
    ball->radius = BALL_SIZE / 2;
    float angle = ((rand() % 90) - 45) * 3.14159 / 180.0;
    ball->dx = BALL_SPEED * sin(angle);
    ball->dy = BALL_SPEED * cos(angle);
    ball->active = true;
}

// Initialize paddle
void init_paddle(Paddle *paddle) {
    paddle->x = (WINDOW_WIDTH - PADDLE_WIDTH) / 2;
    paddle->y = PADDLE_Y;
    paddle->width = PADDLE_WIDTH;
    paddle->height = PADDLE_HEIGHT;
}

// Initialize bricks
void init_bricks(Brick bricks[][BRICK_COLS]) {
    for (int row = 0; row < BRICK_ROWS; row++) {
        for (int col = 0; col < BRICK_COLS; col++) {
            bricks[row][col].x = BRICK_OFFSET_X + col * (BRICK_WIDTH + BRICK_PADDING);
            bricks[row][col].y = BRICK_OFFSET_Y + row * (BRICK_HEIGHT + BRICK_PADDING);
            bricks[row][col].width = BRICK_WIDTH;
            bricks[row][col].height = BRICK_HEIGHT;
            bricks[row][col].active = true;
            bricks[row][col].color_type = row;
        }
    }
}

// Update ball position and physics
void update_ball(Ball *ball, Paddle *paddle, Brick bricks[][BRICK_COLS], GameState *state) {
    if (state->paused || state->game_over || state->victory) return;

    ball->x += ball->dx;
    ball->y += ball->dy;

    // Wall collision
    if (ball->x - ball->radius <= 0 || ball->x + ball->radius >= WINDOW_WIDTH) {
        ball->dx = -ball->dx;
    }

    // Ceiling collision
    if (ball->y - ball->radius <= 0) {
        ball->dy = -ball->dy;
    }

    // Floor collision (lose life)
    if (ball->y - ball->radius >= WINDOW_HEIGHT) {
        state->lives--;
        if (state->lives <= 0) {
            state->game_over = true;
        } else {
            init_ball(ball);
        }
        return;
    }

    // Paddle collision
    if (ball->y + ball->radius >= paddle->y && 
        ball->y - ball->radius <= paddle->y + paddle->height &&
        ball->x >= paddle->x && ball->x <= paddle->x + paddle->width) {
        
        float paddle_center = paddle->x + paddle->width / 2.0;
        float hit_pos = (ball->x - paddle_center) / (paddle->width / 2.0);
        float bounce_angle = hit_pos * 60 * 3.14159 / 180.0;
        
        float speed = sqrt(ball->dx * ball->dx + ball->dy * ball->dy);
        ball->dx = speed * sin(bounce_angle);
        ball->dy = -fabs(speed * cos(bounce_angle));
        ball->y = paddle->y - ball->radius;
    }

    // Brick collision
    for (int row = 0; row < BRICK_ROWS; row++) {
        for (int col = 0; col < BRICK_COLS; col++) {
            if (!bricks[row][col].active) continue;

            Brick *brick = &bricks[row][col];
            
            if (ball->x + ball->radius >= brick->x && 
                ball->x - ball->radius <= brick->x + brick->width &&
                ball->y + ball->radius >= brick->y && 
                ball->y - ball->radius <= brick->y + brick->height) {
                
                brick->active = false;
                state->score += (BRICK_ROWS - row) * 10;
                
                float brick_center_x = brick->x + brick->width / 2.0;
                float brick_center_y = brick->y + brick->height / 2.0;
                float dx_collision = ball->x - brick_center_x;
                float dy_collision = ball->y - brick_center_y;
                
                if (fabs(dx_collision / brick->width) > fabs(dy_collision / brick->height)) {
                    ball->dx = -ball->dx;
                } else {
                    ball->dy = -ball->dy;
                }
            }
        }
    }

    // Check victory
    bool all_destroyed = true;
    for (int row = 0; row < BRICK_ROWS; row++) {
        for (int col = 0; col < BRICK_COLS; col++) {
            if (bricks[row][col].active) {
                all_destroyed = false;
                break;
            }
        }
        if (!all_destroyed) break;
    }
    
    if (all_destroyed) {
        state->level++;
        if (state->level > MAX_LEVELS) {
            state->victory = true;
        } else {
            init_bricks(bricks);
            init_ball(ball);
        }
    }
}

// Move paddle
void move_paddle_left(Paddle *paddle) {
    paddle->x -= PADDLE_SPEED;
    if (paddle->x < 0) paddle->x = 0;
}

void move_paddle_right(Paddle *paddle) {
    paddle->x += PADDLE_SPEED;
    if (paddle->x + paddle->width > WINDOW_WIDTH) {
        paddle->x = WINDOW_WIDTH - paddle->width;
    }
}

// ============================================================================
// COMPONENT 2: GRAPHICS & RENDERING (Member 3 & 4)
// Handles: Drawing all visual elements using Windows GDI
// ============================================================================

// Draw filled rectangle
void draw_rect(HDC hdc, int x, int y, int w, int h, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    HBRUSH oldBrush = SelectObject(hdc, brush);
    Rectangle(hdc, x, y, x + w, y + h);
    SelectObject(hdc, oldBrush);
    DeleteObject(brush);
}

// Draw filled circle
void draw_circle(HDC hdc, int cx, int cy, int radius, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    HBRUSH oldBrush = SelectObject(hdc, brush);
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HPEN oldPen = SelectObject(hdc, pen);
    
    Ellipse(hdc, cx - radius, cy - radius, cx + radius, cy + radius);
    
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

// Render paddle
void render_paddle(HDC hdc, Paddle *paddle) {
    draw_rect(hdc, paddle->x, paddle->y, paddle->width, paddle->height, RGB(30, 144, 255));
}

// Render ball
void render_ball(HDC hdc, Ball *ball) {
    draw_circle(hdc, (int)ball->x, (int)ball->y, ball->radius, RGB(255, 255, 255));
}

// Render bricks
void render_bricks(HDC hdc, Brick bricks[][BRICK_COLS]) {
    for (int row = 0; row < BRICK_ROWS; row++) {
        for (int col = 0; col < BRICK_COLS; col++) {
            if (!bricks[row][col].active) continue;

            Brick *brick = &bricks[row][col];
            COLORREF color;
            
            switch (brick->color_type) {
                case 0: color = RGB(0, 255, 0); break;      // Green
                case 1: color = RGB(255, 0, 255); break;    // Magenta
                case 2: color = RGB(255, 255, 0); break;    // Yellow
                case 3: color = RGB(255, 0, 0); break;      // Red
                default: color = RGB(255, 255, 255); break;
            }
            
            draw_rect(hdc, brick->x, brick->y, brick->width, brick->height, color);
        }
    }
}

// Render text
void render_text(HDC hdc, const char *text, int x, int y, COLORREF color) {
    SetTextColor(hdc, color);
    SetBkMode(hdc, TRANSPARENT);
    TextOut(hdc, x, y, text, strlen(text));
}

// ============================================================================
// COMPONENT 3: INPUT HANDLING (Member 5 & 6)
// Handles: Keyboard input for paddle movement and game control
// ============================================================================

// Update paddle based on input
void handle_paddle_input(Paddle *paddle, GameState *state) {
    if (state->paused || state->game_over || state->victory) return;
    
    if (keys[VK_LEFT] || keys['A']) {
        move_paddle_left(paddle);
    }
    if (keys[VK_RIGHT] || keys['D']) {
        move_paddle_right(paddle);
    }
}

// ============================================================================
// COMPONENT 4: LEVELS, SCORING & PROGRESSION (Member 7 & 8)
// Handles: Score tracking, lives management, level progression
// ============================================================================

// Initialize game state
void init_game_state(GameState *state) {
    state->score = 0;
    state->lives = MAX_LIVES;
    state->level = 1;
    state->paused = false;
    state->game_over = false;
    state->victory = false;
}

// Save score to file
void save_score(int score) {
    FILE *file = fopen("highscore.txt", "a");
    if (file) {
        fprintf(file, "%d\n", score);
        fclose(file);
    }
}

// Load high score
int load_high_score() {
    FILE *file = fopen("highscore.txt", "r");
    int high_score = 0;
    if (file) {
        int score;
        while (fscanf(file, "%d", &score) != EOF) {
            if (score > high_score) high_score = score;
        }
        fclose(file);
    }
    return high_score;
}

// Render UI
void render_ui(HDC hdc, GameState *state) {
    char buffer[100];
    
    // Score
    sprintf(buffer, "Score: %d", state->score);
    render_text(hdc, buffer, 10, 10, RGB(255, 255, 255));
    
    // Lives (hearts)
    sprintf(buffer, "Lives:");
    render_text(hdc, buffer, WINDOW_WIDTH - 120, 10, RGB(255, 255, 255));
    for (int i = 0; i < state->lives; i++) {
        draw_circle(hdc, WINDOW_WIDTH - 30 - (i * 25), 20, 8, RGB(255, 0, 0));
    }
    
    // Level
    sprintf(buffer, "Level: %d", state->level);
    render_text(hdc, buffer, WINDOW_WIDTH / 2 - 30, 10, RGB(255, 255, 255));
}

// ============================================================================
// COMPONENT 5: SOUND, UI & MENU SYSTEM (Member 9 & 10)
// Handles: Menus, game-over screen, UI elements
// ============================================================================

// Render pause screen
void render_pause(HDC hdc) {
    draw_rect(hdc, WINDOW_WIDTH/2 - 100, WINDOW_HEIGHT/2 - 30, 200, 60, RGB(255, 255, 0));
    render_text(hdc, "PAUSED", WINDOW_WIDTH/2 - 35, WINDOW_HEIGHT/2 - 10, RGB(0, 0, 0));
    render_text(hdc, "Press P to continue", WINDOW_WIDTH/2 - 70, WINDOW_HEIGHT/2 + 15, RGB(0, 0, 0));
}

// Render game over
void render_game_over(HDC hdc, GameState *state) {
    draw_rect(hdc, WINDOW_WIDTH/2 - 150, WINDOW_HEIGHT/2 - 50, 300, 100, RGB(255, 0, 0));
    
    char buffer[100];
    sprintf(buffer, "GAME OVER");
    render_text(hdc, buffer, WINDOW_WIDTH/2 - 50, WINDOW_HEIGHT/2 - 30, RGB(255, 255, 255));
    
    sprintf(buffer, "Score: %d", state->score);
    render_text(hdc, buffer, WINDOW_WIDTH/2 - 40, WINDOW_HEIGHT/2, RGB(255, 255, 255));
    
    render_text(hdc, "Press R to restart", WINDOW_WIDTH/2 - 70, WINDOW_HEIGHT/2 + 30, RGB(255, 255, 255));
}

// Render victory
void render_victory(HDC hdc, GameState *state) {
    draw_rect(hdc, WINDOW_WIDTH/2 - 150, WINDOW_HEIGHT/2 - 50, 300, 100, RGB(0, 255, 0));
    
    char buffer[100];
    sprintf(buffer, "VICTORY!");
    render_text(hdc, buffer, WINDOW_WIDTH/2 - 40, WINDOW_HEIGHT/2 - 30, RGB(0, 0, 0));
    
    sprintf(buffer, "Score: %d", state->score);
    render_text(hdc, buffer, WINDOW_WIDTH/2 - 40, WINDOW_HEIGHT/2, RGB(0, 0, 0));
    
    render_text(hdc, "Press R to restart", WINDOW_WIDTH/2 - 70, WINDOW_HEIGHT/2 + 30, RGB(0, 0, 0));
}

// ============================================================================
// WINDOWS MESSAGE HANDLING
// ============================================================================

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_DESTROY:
            running = false;
            PostQuitMessage(0);
            return 0;
            
        case WM_KEYDOWN:
            if (wParam < 256) {
                keys[wParam] = true;
            }
            return 0;
            
        case WM_KEYUP:
            if (wParam < 256) {
                keys[wParam] = false;
            }
            return 0;
            
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            EndPaint(hwnd, &ps);
            return 0;
        }
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

// ============================================================================
// MAIN FUNCTION
// ============================================================================

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // Register window class
    const char CLASS_NAME[] = "ArkanoidGameClass";
    
    WNDCLASS wc = {0};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    
    RegisterClass(&wc);
    
    // Create window
    hwnd = CreateWindowEx(
        0, CLASS_NAME, "Arkanoid Game - Team Project",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, WINDOW_WIDTH, WINDOW_HEIGHT,
        NULL, NULL, hInstance, NULL
    );
    
    if (!hwnd) {
        MessageBox(NULL, "Window creation failed!", "Error", MB_OK | MB_ICONERROR);
        return 0;
    }
    
    ShowWindow(hwnd, nCmdShow);
    
    // Initialize random
    srand(time(NULL));
    
    // Initialize game objects
    Ball ball;
    Paddle paddle;
    Brick bricks[BRICK_ROWS][BRICK_COLS];
    GameState state;
    
    init_ball(&ball);
    init_paddle(&paddle);
    init_bricks(bricks);
    init_game_state(&state);
    
    // Print controls
    printf("\n=== ARKANOID GAME ===\n");
    printf("Controls:\n");
    printf("  Arrow Keys / A,D - Move paddle\n");
    printf("  P - Pause\n");
    printf("  R - Restart (when game over)\n");
    printf("  ESC - Quit\n\n");
    
    // Game loop
    MSG msg = {0};
    DWORD last_time = GetTickCount();
    
    while (running) {
        // Handle Windows messages
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                running = false;
            }
            
            // Handle key presses
            if (msg.message == WM_KEYDOWN) {
                if (msg.wParam == VK_ESCAPE) {
                    running = false;
                }
                if (msg.wParam == 'P' || msg.wParam == VK_SPACE) {
                    if (!state.game_over && !state.victory) {
                        state.paused = !state.paused;
                    }
                }
                if (msg.wParam == 'R') {
                    if (state.game_over || state.victory) {
                        init_ball(&ball);
                        init_paddle(&paddle);
                        init_bricks(bricks);
                        init_game_state(&state);
                    }
                }
            }
            
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        
        // Handle input
        handle_paddle_input(&paddle, &state);
        
        // Update game logic
        update_ball(&ball, &paddle, bricks, &state);
        
        // Render
        hdc = GetDC(hwnd);
        
        // Clear background
        RECT rect;
        GetClientRect(hwnd, &rect);
        HBRUSH bgBrush = CreateSolidBrush(RGB(0, 0, 0));
        FillRect(hdc, &rect, bgBrush);
        DeleteObject(bgBrush);
        
        // Draw game elements
        render_bricks(hdc, bricks);
        render_paddle(hdc, &paddle);
        render_ball(hdc, &ball);
        render_ui(hdc, &state);
        
        // Draw overlays
        if (state.paused) render_pause(hdc);
        if (state.game_over) {
            render_game_over(hdc, &state);
            save_score(state.score);
        }
        if (state.victory) {
            render_victory(hdc, &state);
            save_score(state.score);
        }
        
        ReleaseDC(hwnd, hdc);
        
        // Frame rate control
        DWORD current_time = GetTickCount();
        DWORD elapsed = current_time - last_time;
        if (elapsed < FRAME_DELAY) {
            Sleep(FRAME_DELAY - elapsed);
        }
        last_time = GetTickCount();
    }
    
    printf("\nFinal Score: %d\n", state.score);
    printf("Thanks for playing!\n\n");
    
    return 0;
}

// ============================================================================
// END OF ARKANOID GAME
// ============================================================================
