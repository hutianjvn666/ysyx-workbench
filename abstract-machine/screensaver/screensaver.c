#include <am.h>
#include <klib-macros.h>
#include <stdlib.h>

// 讲义指定的窗口分辨率
#define SCREEN_WIDTH  400
#define SCREEN_HEIGHT 300

// 渐变参数（可自行调整）
#define GRADIENT_STEPS 100    // 每轮渐变的步数
#define NORMAL_DELAY  10000  // 正常每步间隔（微秒）
#define FAST_DELAY    2000   // 按键加速时的间隔（微秒）

// 讲义指定的目标颜色列表
const uint32_t color_palette[] = {
  0x000000,   // 黑色
  0xff0000,   // 红色
  0x00ff00,   // 绿色
  0x0000ff,   // 蓝色
  0xffff00,   // 黄色
  0xff00ff,   // 品红
  0x00ffff,   // 青色
  0xffffff    // 白色
};
const int palette_size = sizeof(color_palette) / sizeof(color_palette[0]);

// 全局帧缓冲区（400x300 32位像素）
static uint32_t framebuffer[SCREEN_WIDTH * SCREEN_HEIGHT];

// 步骤1：实现单种颜色全屏填充
void draw(uint32_t color) {
  // 填充整个帧缓冲区
  for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) {
    framebuffer[i] = color;
  }
  // 绘制到屏幕（x=0, y=0, 全屏, 同步刷新）
  io_write(AM_GPU_FBDRAW, 0, 0, framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, true);
}

// 从32位颜色值提取RGB分量
static void extract_rgb(uint32_t color, uint8_t *r, uint8_t *g, uint8_t *b) {
  *r = (color >> 16) & 0xff;
  *g = (color >> 8) & 0xff;
  *b = color & 0xff;
}

// 合并RGB分量为32位颜色值
static uint32_t combine_rgb(uint8_t r, uint8_t g, uint8_t b) {
  return (r << 16) | (g << 8) | b;
}

// 获取随机颜色索引（避免与当前颜色相同）
static int get_random_color(int current_idx) {
  int next_idx;
  do {
    next_idx = rand() % palette_size;
  } while (next_idx == current_idx);
  return next_idx;
}

int main() {
  ioe_init(); // GUI和IO初始化

  // 初始化渐变状态
  int current_color_idx = 0;
  int next_color_idx = get_random_color(current_color_idx);
  int step = 0;
  bool is_fast = false; // 是否处于加速状态

  // 提取当前和目标颜色的RGB分量
  uint8_t r1, g1, b1;
  uint8_t r2, g2, b2;
  extract_rgb(color_palette[current_color_idx], &r1, &g1, &b1);
  extract_rgb(color_palette[next_color_idx], &r2, &g2, &b2);

  while (1) {
    // 步骤3：处理按键事件
    AM_INPUT_KEYBRD_T key = io_read(AM_INPUT_KEYBRD);
    if (key.keycode != AM_KEY_NONE) {
      if (key.keycode == AM_KEY_ESCAPE && key.keydown) {
        // 按下ESC键退出程序
        halt(0);
      } else {
        // 其他按键：按下加速，释放恢复
        is_fast = key.keydown;
      }
    }

    // 步骤2：计算当前渐变颜色（线性插值）
    uint8_t r = r1 + (r2 - r1) * step / GRADIENT_STEPS;
    uint8_t g = g1 + (g2 - g1) * step / GRADIENT_STEPS;
    uint8_t b = b1 + (b2 - b1) * step / GRADIENT_STEPS;
    uint32_t current_color = combine_rgb(r, g, b);

    // 绘制当前颜色
    draw(current_color);

    // 更新渐变步数
    step++;
    if (step > GRADIENT_STEPS) {
      // 一轮渐变结束，切换到下一个随机颜色
      step = 0;
      current_color_idx = next_color_idx;
      next_color_idx = get_random_color(current_color_idx);
      
      // 更新RGB分量
      extract_rgb(color_palette[current_color_idx], &r1, &g1, &b1);
      extract_rgb(color_palette[next_color_idx], &r2, &g2, &b2);
    }

    // 控制显示间隔（根据是否加速调整）
    uint64_t start_time = io_read(AM_TIMER_UPTIME).us;
    while (io_read(AM_TIMER_UPTIME).us - start_time < (is_fast ? FAST_DELAY : NORMAL_DELAY));
  }

  return 0;
}
