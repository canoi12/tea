#include "src/tea.h"

#define GRAVITY 100

#include <math.h>
#define MATH_PI 3.14159

int main(int argc, char ** argv) {
  te_Config conf;
  tea_config_init(&conf, NULL, 640, 380);
  conf.window_flags |= SDL_WINDOW_RESIZABLE;

  tea_init(&conf);

  // te_Texture *tex = tea_texture_load("goblin.png");
  te_Image img = tea_image_load("goblin.png");

  te_Canvas canvas = tea_canvas(160, 95);

  te_Texture *tex = tea_image_texture(img);

  float x, y;
  x = y = 32;
  int frame = 0;

  while (!tea_should_close()) {
    tea_begin_render();

    // tea_draw_texture(tex, &tea_rect(0, 0, 16*4, 16*4), &tea_rect(0, 0, 16, 16));
    //if (tea_keyboard_was_pressed(TEA_KEY_A)) x -= 16;
    //else if (tea_keyboard_was_pressed(TEA_KEY_D)) x += 16;
    if (tea_keyboard_was_pressed(TEA_KEY_A)) frame = frame - 1 < 0 ? 5 : frame-1;
    else if (tea_keyboard_was_pressed(TEA_KEY_D)) frame = frame + 1 > 5 ? 0 : frame + 1;
    
    te_Rect r = tea_rect(frame*16, 0, 16, 16);

    tea_set_canvas(canvas);
    tea_render_clear(BLACK);
    tea_draw_color(WHITE);
    tea_draw_image(img, NULL, tea_point(0, 0));
    tea_draw_rect(tea_rect(frame*16, 0, 16, 16));
    tea_draw_image(img, &r, tea_point(frame*16, 32));
    
    
    
    // tea_draw_circle(tea_point(x, 0), 8);
    tea_set_canvas(0);

    if (tea_keyboard_was_pressed(TEA_KEY_ESCAPE)) break;


    te_Texture *ct = tea_canvas_texture(canvas);
    tea_draw_texture_ex(ct, &tea_rect(0, 0, 160*4, 95*4), NULL, 0, tea_point(0, 0), TEA_FLIP_NONE);


    tea_end_render();
  }

  tea_terminate();

  return 0;
}
