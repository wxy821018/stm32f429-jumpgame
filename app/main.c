#include "stm32f4xx_hal.h"
#include "stm32f429i_discovery.h"
#include "stm32f429i_discovery_lcd.h"
#include "stm32f429i_discovery_sdram.h"
#include <stdint.h>

#define SCR_W   320
#define SCR_H   240
#define FB_W    240
#define FB      ((volatile uint32_t *)SDRAM_DEVICE_ADDR)

#define COLOR_SKY         0xFF87CEEB
#define COLOR_MOUNT_FAR   0xFF6A8E5A
#define COLOR_MOUNT_MID   0xFF4F7A45
#define COLOR_GROUND      0xFF8B4513
#define COLOR_GROUND_HI   0xFFA0612C
#define COLOR_PLAYER      0xFFFFD700
#define COLOR_PLAYER_AIR  0xFFFFEC4D
#define COLOR_PLAYER_HI   0xFFFFFA00
#define COLOR_OBSTACLE    0xFFB22222
#define COLOR_OBSTACLE_HI 0xFFE03030
#define COLOR_FAIL        0xFFFF1010

#define GROUND_H         30
#define PLAYER_X         60
#define PLAYER_SIZE      22
#define GRAVITY          1
#define JUMP_VY          14
#define MAX_JUMPS        2          /* ground jump + 1 air jump */

#define MAX_OBS          5
#define OBS_W_MIN        14
#define OBS_W_MAX        30
#define OBS_H_MIN        18
#define OBS_H_MAX        80

#define BASE_OBS_SPEED   3
#define MAX_OBS_SPEED    9
#define BASE_SPAWN_MIN   55
#define BASE_SPAWN_MAX   110
#define MIN_SPAWN_GAP    24         /* hard floor so jumping stays possible */

typedef struct { int active, x, w, h; } Obstacle;

typedef struct {
    int player_y, player_vy;
    int jumps_used;             /* 0 when grounded, increments per jump */
    int bg_far_off, bg_mid_off;
    Obstacle obs[MAX_OBS];
    int next_spawn;
    int score, hi_score;
    int game_over;
    uint32_t frame;
    uint32_t rng;
} Game;

static Game g;

static void SystemClock_Config(void);

static uint32_t xorshift(void) {
    g.rng ^= g.rng << 13;
    g.rng ^= g.rng >> 17;
    g.rng ^= g.rng << 5;
    return g.rng;
}

static inline int grounded(void) { return g.player_y <= GROUND_H && g.player_vy <= 0; }

/* landscape (lx, ly) → fb[lx * FB_W + ly], 90° CCW */
static inline void px(int lx, int ly, uint32_t c) {
    if ((unsigned)lx >= SCR_W || (unsigned)ly >= SCR_H) return;
    FB[lx * FB_W + ly] = c;
}

static void fill_rect(int lx, int ly, int w, int h, uint32_t c) {
    int x0 = lx < 0      ? 0     : lx;
    int y0 = ly < 0      ? 0     : ly;
    int x1 = lx + w > SCR_W ? SCR_W : lx + w;
    int y1 = ly + h > SCR_H ? SCR_H : ly + h;
    for (int xi = x0; xi < x1; xi++) {
        volatile uint32_t *col = &FB[xi * FB_W];
        for (int yj = y0; yj < y1; yj++) col[yj] = c;
    }
}

static void fill_screen(uint32_t c) {
    volatile uint32_t *p = FB;
    for (int i = 0; i < SCR_W * SCR_H; i++) p[i] = c;
}

static void vline(int lx, int ly, int h, uint32_t c) {
    if ((unsigned)lx >= SCR_W) return;
    int y0 = ly < 0 ? 0 : ly;
    int y1 = ly + h > SCR_H ? SCR_H : ly + h;
    volatile uint32_t *col = &FB[lx * FB_W];
    for (int yj = y0; yj < y1; yj++) col[yj] = c;
}

static int abs_i(int v) { return v < 0 ? -v : v; }

static void draw_mountains(int off, int amplitude, int period, uint32_t color) {
    for (int x = 0; x < SCR_W; x++) {
        int wx = ((x + off) % period + period) % period;
        int line_h = amplitude - abs_i(wx - period / 2);
        if (line_h < 0) line_h = 0;
        vline(x, GROUND_H, line_h, color);
    }
}

static void draw_ground(void) {
    fill_rect(0, 0,         SCR_W, GROUND_H, COLOR_GROUND);
    fill_rect(0, GROUND_H - 3, SCR_W, 3,     COLOR_GROUND_HI);
}

static void draw_player(int ly) {
    uint32_t body = (g.jumps_used >= 2) ? COLOR_PLAYER_AIR : COLOR_PLAYER;
    fill_rect(PLAYER_X,     ly,     PLAYER_SIZE, PLAYER_SIZE, body);
    fill_rect(PLAYER_X + 2, ly + PLAYER_SIZE - 4, PLAYER_SIZE - 4, 2, COLOR_PLAYER_HI);
    fill_rect(PLAYER_X + 14, ly + 13, 3, 3, 0xFF202020); /* eye */
}

static void draw_obstacles(int dead) {
    uint32_t c   = dead ? COLOR_FAIL : COLOR_OBSTACLE;
    uint32_t chi = dead ? COLOR_FAIL : COLOR_OBSTACLE_HI;
    for (int i = 0; i < MAX_OBS; i++) {
        if (!g.obs[i].active) continue;
        fill_rect(g.obs[i].x, GROUND_H,                          g.obs[i].w, g.obs[i].h, c);
        fill_rect(g.obs[i].x, GROUND_H + g.obs[i].h - 3,         g.obs[i].w, 3,          chi);
    }
}

/* 3x5 digit glyphs (column-major; LSB top) */
static const uint8_t digits[10][3] = {
    {0x1F,0x11,0x1F}, {0x00,0x1F,0x00}, {0x1D,0x15,0x17},
    {0x15,0x15,0x1F}, {0x07,0x04,0x1F}, {0x17,0x15,0x1D},
    {0x1F,0x15,0x1D}, {0x01,0x01,0x1F}, {0x1F,0x15,0x1F},
    {0x17,0x15,0x1F},
};

static void draw_digit(int d, int lx, int ly, int scale, uint32_t color) {
    if (d < 0 || d > 9) return;
    for (int col = 0; col < 3; col++) {
        uint8_t bits = digits[d][col];
        for (int row = 0; row < 5; row++) {
            if (bits & (1u << row)) {
                fill_rect(lx + col * scale, ly + (4 - row) * scale, scale, scale, color);
            }
        }
    }
}

static void draw_score(int score, int lx, int ly, int scale, uint32_t color) {
    if (score == 0) { draw_digit(0, lx, ly, scale, color); return; }
    int digit_w = scale * 4;
    int n = 0, tmp = score;
    while (tmp) { n++; tmp /= 10; }
    int x = lx + (n - 1) * digit_w;
    while (score) {
        draw_digit(score % 10, x, ly, scale, color);
        score /= 10;
        x -= digit_w;
    }
}

/* difficulty derived from score */
static int cur_obs_speed(void) {
    int s = BASE_OBS_SPEED + g.score / 6;
    return s > MAX_OBS_SPEED ? MAX_OBS_SPEED : s;
}
static int cur_spawn_min(void) {
    int s = BASE_SPAWN_MIN - g.score;
    return s < MIN_SPAWN_GAP ? MIN_SPAWN_GAP : s;
}
static int cur_spawn_max(void) {
    int mn = cur_spawn_min();
    int mx = BASE_SPAWN_MAX - g.score * 2;
    return mx < mn + 20 ? mn + 20 : mx;
}

static void game_reset(void) {
    for (int i = 0; i < MAX_OBS; i++) g.obs[i].active = 0;
    g.player_y    = GROUND_H;
    g.player_vy   = 0;
    g.jumps_used  = 0;
    g.bg_far_off  = 0;
    g.bg_mid_off  = 0;
    g.next_spawn  = 50;
    g.score       = 0;
    g.game_over   = 0;
    g.frame       = 0;
}

static int collide(void) {
    int px0 = PLAYER_X, py0 = g.player_y;
    int px1 = px0 + PLAYER_SIZE, py1 = py0 + PLAYER_SIZE;
    for (int i = 0; i < MAX_OBS; i++) {
        if (!g.obs[i].active) continue;
        int ox0 = g.obs[i].x, oy0 = GROUND_H;
        int ox1 = ox0 + g.obs[i].w, oy1 = oy0 + g.obs[i].h;
        if (px0 < ox1 && px1 > ox0 && py0 < oy1 && py1 > oy0) return 1;
    }
    return 0;
}

static void game_tick(int btn_edge) {
    g.frame++;

    if (g.game_over) {
        if (btn_edge) {
            if (g.score > g.hi_score) g.hi_score = g.score;
            game_reset();
        }
        return;
    }

    /* jump: any press uses one of MAX_JUMPS, replacing current vy */
    if (btn_edge && g.jumps_used < MAX_JUMPS) {
        g.player_vy   = JUMP_VY;
        g.jumps_used += 1;
    }

    /* physics: always integrate (even on ground frame), then clamp */
    g.player_y += g.player_vy;
    g.player_vy -= GRAVITY;
    if (g.player_y <= GROUND_H) {
        g.player_y    = GROUND_H;
        g.player_vy   = 0;
        g.jumps_used  = 0;
    }

    /* scrolling — slightly accelerates with score */
    g.bg_far_off += 1 + g.score / 30;
    g.bg_mid_off += 2 + g.score / 18;

    int sp = cur_obs_speed();
    for (int i = 0; i < MAX_OBS; i++) {
        if (!g.obs[i].active) continue;
        g.obs[i].x -= sp;
        if (g.obs[i].x + g.obs[i].w < 0) {
            g.obs[i].active = 0;
            g.score++;
        }
    }

    if (--g.next_spawn <= 0) {
        for (int i = 0; i < MAX_OBS; i++) {
            if (!g.obs[i].active) {
                g.obs[i].active = 1;
                g.obs[i].x = SCR_W + 8;
                g.obs[i].w = OBS_W_MIN + (xorshift() % (OBS_W_MAX - OBS_W_MIN + 1));
                g.obs[i].h = OBS_H_MIN + (xorshift() % (OBS_H_MAX - OBS_H_MIN + 1));
                break;
            }
        }
        int mn = cur_spawn_min();
        int mx = cur_spawn_max();
        g.next_spawn = mn + (xorshift() % (mx - mn + 1));
    }

    if (collide()) g.game_over = 1;
}

static void render(void) {
    fill_screen(COLOR_SKY);
    draw_mountains(g.bg_far_off, 70, 130, COLOR_MOUNT_FAR);
    draw_mountains(g.bg_mid_off, 45,  90, COLOR_MOUNT_MID);
    draw_ground();
    draw_obstacles(g.game_over);
    draw_player(g.player_y);

    draw_score(g.score,    8,         SCR_H - 16, 2, 0xFFFFFFFF);
    draw_score(g.hi_score, SCR_W - 70, SCR_H - 16, 2, 0xFFFFFF80);

    if (g.game_over && ((g.frame / 15) & 1)) {
        fill_rect(SCR_W / 2 - 60, SCR_H / 2 - 6, 120, 12, COLOR_FAIL);
    }
}

int main(void) {
    HAL_Init();
    SystemClock_Config();

    BSP_LED_Init(LED3);
    BSP_LED_Init(LED4);

    if (BSP_SDRAM_Init() != SDRAM_OK) {
        BSP_LED_On(LED4);
        while (1) {}
    }

    BSP_LCD_Init();
    BSP_LCD_LayerDefaultInit(LCD_BACKGROUND_LAYER, SDRAM_DEVICE_ADDR);
    BSP_LCD_SelectLayer(LCD_BACKGROUND_LAYER);
    BSP_LCD_Clear(LCD_COLOR_BLACK);

    BSP_PB_Init(BUTTON_KEY, BUTTON_MODE_GPIO);

    g.rng = HAL_GetTick() ^ 0xC0FFEE42u;
    g.hi_score = 0;
    game_reset();

    int prev_btn = 0;
    while (1) {
        int btn = BSP_PB_GetState(BUTTON_KEY) ? 1 : 0;
        int edge = btn && !prev_btn;
        prev_btn = btn;

        game_tick(edge);
        render();

        if (g.game_over) {
            BSP_LED_On(LED4);
            BSP_LED_Off(LED3);
        } else {
            BSP_LED_Off(LED4);
            if ((g.frame & 0xF) == 0) BSP_LED_Toggle(LED3);
        }

        HAL_Delay(16);
    }
}

static void SystemClock_Config(void) {
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState = RCC_HSE_ON;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLM = 8;
    osc.PLL.PLLN = 360;
    osc.PLL.PLLP = RCC_PLLP_DIV2;
    osc.PLL.PLLQ = 7;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) { while (1) {} }

    if (HAL_PWREx_EnableOverDrive() != HAL_OK) { while (1) {} }

    clk.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK
                  | RCC_CLOCKTYPE_PCLK1  | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV4;
    clk.APB2CLKDivider = RCC_HCLK_DIV2;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_5) != HAL_OK) { while (1) {} }
}
