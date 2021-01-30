#include "src/tea.h"

#include <math.h>
#define MATH_PI 3.14159

int main(int argc, char ** argv) {
  te_Config conf;
  tea_config_init(&conf, NULL, 640, 380);
  conf.window_flags |= SDL_WINDOW_RESIZABLE;

  tea_init(&conf);

  float x = 0;
  float spd = 5;

  te_Texture tex = tea_texture_load("goblin.png");
  te_Font *font = tea_font_load("extrude.ttf", 32);
  te_Canvas canvas = tea_canvas(256, 64);
  float t = 0;
  int frame = 0;

  float xx = 0;

  while (!tea_should_close()) {
    tea_begin_render();
    float delta = tea_get_delta();

    x += spd * delta;
    if (x >= MATH_PI*2) x = 0;
    float s = sinf(x)+1;

    // int triang_sz = 32;

    for (int i = 0; i < 24; i++) {
      float ss = sinf(x + i)+1;
      float sss = sinf(x + 1 + i)+1;
      float x = (i+ss) * 16 - 32;
      float y = 48 + ss * 16;

      float xx = (i+1+sss) * 16 - 32;
      float yy = 48 + sss * 16;
      te_Point v = {x, y};
      te_Point vv = {xx, yy};
      te_Color cc = tea_color(20+ss*107, 50+ss*32, 40+ss*64);
      tea_draw_mode(TEA_FILL);
      tea_draw_color(cc);
			// tea_draw_circle(v, (ss+2)*8);
      int triang_sz = (ss+2)*12;
      // v.x += triang_sz;
      // v.y += triang_sz;
      tea_draw_triangle(tea_point(v.x-triang_sz, v.y), tea_point(v.x, v.y-triang_sz), tea_point(v.x+triang_sz, v.y));

      v.y += 64;
      vv.y += 64;
      tea_draw_line(v, vv);

      v.y += 96;
      tea_draw_mode(TEA_FILL);
      tea_draw_circle(v, (ss+2)*8);

      // tico_graphics_draw_circle((i+ss) * 16 - 32, 256 + ss*16, (ss+2)*8, tico_color(20+ss*107, 20+ss*32, 40+ss*64));
    }
    // tea_render_draw_color(tea_color(255, 0, 255));

    // tea_draw_texture(tex, NULL, tea_point(32, 32));
    s *= 16; 
    tea_draw_mode(TEA_LINE);
    tea_draw_color(WHITE);
    tea_draw_triangle(tea_point(s, 80), tea_point(s + 256, 16), tea_point(s+256, 144));

    tea_draw_mode(TEA_LINE);
    tea_draw_rect(0, 0, 32, 32);
    tea_draw_circle((te_Point){320, 190}, 32);

    
    // Draw texture

    t += 8*delta;
    if (t >= 1) {
      t = 0;
      if (frame++ >= 4) frame = 0;
    }
    
    te_Rect r = tea_rect(16*frame, 0, 16, 16);
    tea_draw_texture(tex, &r, tea_point(xx+s+272, 64));
    
    char text[32];
    int frame = tea_get_framerate();
    sprintf(text, "fps: %d", tea_get_framerate());
    tea_draw_text(font, text, tea_point(s, 16));

    // tea_clear_color(WHITE);
    tea_set_canvas(canvas);
    
    // tea_draw_color(WHITE);
    tea_draw_color(tea_color(255, 0, 0));
    tea_draw_rect(32, 32, 32, 64);
    
    // tea_clear_color(BLACK);
    tea_set_canvas(0);
    
    
    /*te_Rect r = tea_rect(16*frame, 0, 16, 16);
    tea_push_canvas(canvas);
    tea_draw_texture(tex, &r, tea_point(s+272, 64));
    float angle = s+15;
    te_Point scale = tea_point(s, s);
    te_Point origin = tea_point(s-8, (sin(s/4)*2)+8);
    tea_draw_texture_ex(tex, &r, tea_point(320, 190), angle, scale, origin);
    tea_draw_text(font, "hihihihih", tea_point(s, 16));
    tea_pop_canvas();*/

    // tea_draw_texture(canvas, NULL, tea_point(128, 128));

    if (tea_key_is_down(TEA_KEY_RIGHT)) xx += 150 * tea_get_delta();
    if (tea_key_is_down(TEA_KEY_LEFT)) xx -= 150 * tea_get_delta();
    if (tea_key_is_down(TEA_KEY_ESCAPE)) break;
    
    // tea_draw_canvas(canvas, NULL, tea_point(xx, 150));
    tea_end_render();
    // printf("teste\n");
  }

  // SDL_Delay(1000);

  tea_terminate();

  return 0;
}
