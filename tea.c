#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tea.h"

#include "stb_image.h"
#include "stb_truetype.h"

#include "font8x8_basic.h"

#include "SDL_render.h"

#include <SDL.h>

#if defined(TEA_GL)
#include <SDL_opengl.h>
// #include "GL/gl3w.h"
#endif

#define tea() (&_tea_ctx)
#define render() (&(tea()->render))
#define window() ((tea()->window))
#define event() (&(tea()->event))

#define MAX_FONT_CHAR 256
#define MAX(a, b) ((a) < (b) ? (b) : (a))
#define MIN(a, b) ((a) > (b) ? (b) : (a))

#define pixel_format(id) (tea()->mode.pixel_format[id])

typedef struct te_render_t te_render_t;
typedef SDL_Window te_window_t;

struct color_t { unsigned char r, g, b, a; };

struct te_texture_t {
    int usage;
#if !defined(TEA_GL)
    void *handle;
#else
    int handle;
#endif
    unsigned int width, height;
    unsigned int filter[2], wrap[2];
    int channels;
};

struct te_font_t {
    te_texture_t *tex;
    struct {
        int ax; // advance.x
        int ay; // advance.y

        int bw; // bitmap.width
        int bh; // bitmap.height

        int bl; // bitmap.left
        int bt; // bitmap.top

        float tx; // x offset of glyph in texture coordinates
    } c[MAX_FONT_CHAR];

    unsigned char size;

    stbtt_fontinfo info;
    float ptsize;
    float scale;
    int baseline;
    void *data;
};

struct te_rendermode_t {
    int pixel_format[TEA_PIXELFORMAT_COUNT];
};

struct te_renderstat_t {
    te_color_t draw_color;
    te_color_t clear_color;
    int draw_mode;

    te_texture_t *tex;
    te_transform_t transform;
};

struct te_window_s {
    int flags;
    void *handle;
    unsigned int width, height;
};

struct te_render_t {
    int flags;
    struct te_renderstat_t stat;
    void *handle;
};

struct te_input_t {
    struct { const Uint8 *state; Uint8 old_state[TEA_KEY_COUNT]; } key;
    struct {
        Uint8 state[TEA_MOUSE_COUNT];
        Uint8 old_state[TEA_MOUSE_COUNT];
        TEA_TNUM x, y;
        TEA_TNUM scrollx, scrolly;
    } mouse;
};

struct te_timer_t {
    unsigned int prev_time, current_time;
    float delta;
    unsigned int prev_fps_time, frame, fps;
};

struct Tea {
    int running;
    struct te_rendermode_t mode;

    te_render_t render;
    te_window_t *window;
    te_event_t event;

    char error_buf[256];
    struct te_input_t input;
    struct te_timer_t timer;

    te_font_t *default_font;
    struct {
        teaKeyboardEv key;
        teaControllerEv ctrl;
        struct {
            teaWindowMoveEv move;
            teaWindowResizeEv resize;
            teaWindowVisibleEv visible;
            teaWindowFocusEv focus;
            teaWindowMouseEv mouse;
            teaWindowCloseEv close;
        } window;
        teaDropEv drop;
        teaDropFileEv drop_file;
        teaDropTextEv drop_text;
        teaTextInputEv text_input;
        teaTextEditEv text_edit;
    } callback;
};

Tea _tea_ctx;

// Make sure that r is not NULL
static SDL_Rect _to_sdl_rect(te_rect_t *r) {
    return (SDL_Rect){r->x, r->y, r->w, r->h};
}

te_config_t tea_config_init(const char *title, int w, int h) {
    te_config_t c = (te_config_t){{0}};
    title = title ? title : ("tea "TEA_VER);

    strcpy((char*)c.title, title);
    c.width = w;
    c.height = h;

    c.flags = SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_TIMER | SDL_INIT_EVENTS;
    c.render_flags = SDL_RENDERER_ACCELERATED;
    c.window_flags = SDL_WINDOW_SHOWN;

    return c;
}

int tea_render_mode() {
    tea()->mode.pixel_format[TEA_PIXELFORMAT_UNKNOWN] = 0;
#if !defined(TEA_GL)
    tea()->mode.pixel_format[TEA_RGB] = SDL_PIXELFORMAT_RGB888;

    tea()->mode.pixel_format[TEA_RGBA] = SDL_PIXELFORMAT_RGBA32;
    tea()->mode.pixel_format[TEA_ARGB] = SDL_PIXELFORMAT_ARGB32;
    tea()->mode.pixel_format[TEA_BGRA] = SDL_PIXELFORMAT_BGRA32;
    tea()->mode.pixel_format[TEA_ABGR] = SDL_PIXELFORMAT_ABGR32;
#else
    tea()->mode.pixel_format[TEA_RGB] = GL_RGB;
    tea()->mode.pixel_format[TEA_RGBA] = GL_RGBA;
#endif

    return 1;
}

int tea_init(te_config_t *c) {
    if (c == NULL) {
        tea_error("config cannot be NULL");
        return 0;
    }
    memset(tea(), 0, sizeof(struct Tea));
    tea_render_mode();

    if (SDL_Init(c->flags)) {
        tea_error("failed to init SDL");
        return 0;
    }
    int window_flags = c->window_flags;

#if defined(TEA_GL)
    window_flags |= SDL_WINDOW_OPENGL;

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
#endif

    window() = SDL_CreateWindow((const char*)c->title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, c->width, c->height, window_flags);
    te_window_t *window = window();


#if !defined(TEA_GL)
    int render_flags = c->render_flags;
    render()->handle = SDL_CreateRenderer(window, -1, render_flags);
    if (!render()->handle) {
        tea_error("failed to create SDL_Renderer: %s", SDL_GetError());
        return 0;
    }
#else
    SDL_GLContext ctx = SDL_GL_CreateContext(window);
    // if (gl3wInit()) {
    //     tea_error("failed to init GL lib");
    //     return 0;
    // }
#endif

    te_render_t *r = render();

    r->stat.clear_color = 0;
    r->stat.draw_color = TEA_WHITE;
    r->stat.draw_mode = TEA_LINE;
    r->stat.tex = NULL;
    r->stat.transform.scale = TEA_POINT(1, 1);

    tea()->input.key.state = SDL_GetKeyboardState(NULL);
    tea()->default_font = NULL;
    tea()->default_font = tea_default_font(NULL);

    return 1;
}

int tea_deinit() {
#if !defined(TEA_GL)
    SDL_DestroyRenderer(render()->handle);
#endif
    SDL_DestroyWindow(window());
    SDL_Quit();
    return 1;
}

int tea_begin() {
    tea_poll_event(event());
    tea()->timer.current_time = SDL_GetTicks();
    tea()->timer.delta = tea()->timer.current_time - tea()->timer.prev_time;
    tea()->timer.prev_time = tea()->timer.current_time;

    tea()->timer.frame++;

    SDL_Delay(TEA_FPS);
    SDL_SetRenderDrawBlendMode(render()->handle, SDL_BLENDMODE_BLEND);
    te_point_t size;
    tea_window_size(&size, 0, 0);
#if defined(TEA_GL)
    glViewport(0, 0, size.x, size.y);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.f, size.x, size.y, 0.f, 0.f, 1.f);
#endif
    return 1;
}

int tea_end() { 
#if !defined(TEA_GL)
    SDL_RenderPresent(render()->handle);
#else
    SDL_GL_SwapWindow(window());
#endif
    return 1; 
}

int tea_should_close() {
    return event()->type == SDL_QUIT;
}

float tea_delta() {
    return tea()->timer.delta / 1000.f;
}

int tea_fps() {
    return tea()->timer.fps;
}

/*********************************
 * Render
 *********************************/

static int _to_gl_color(te_color_t color, float *out) {
    if (!out) return 0;
    for (int i = 0; i < 4; i++) out[i] = (float)((color >> (8*i)) & 0xff) / 255.f;
    return 1;
}

int tea_clear(te_color_t col) {
#if !defined(TEA_GL)
    int draw_color = tea_color(-1);

    tea_color(col);
    SDL_RenderClear(render()->handle);
    tea_color(draw_color);
#else
    float c[4];
    _to_gl_color(col, c);
    glClearColor(c[0], c[1], c[2], c[3]);
    glClear(GL_COLOR_BUFFER_BIT);
#endif


    return 1;
}

int tea_mode(int mode) {
    if (mode >= 0) render()->stat.draw_mode = mode;
    return render()->stat.draw_mode;
}

te_color_t tea_color(te_color_t col) {
    if (col >= 0) render()->stat.draw_color = col;
    else col = render()->stat.draw_color;
#if !defined(TEA_GL)
    unsigned char c[4];
#else
    float c[4];
#endif
    te_color_t clear_color = col;
    for (int i = 0; i < 4; i++) {
        c[i] = clear_color & 0xff;
#if defined(TEA_GL)
        c[i] /= 255.f;
#endif
        clear_color >>= 8;
    }
#if !defined(TEA_GL)
    SDL_SetRenderDrawColor(render()->handle, c[0], c[1], c[2], c[3]);
#endif

    return render()->stat.draw_color;
}

int tea_point(TEA_TNUM x, TEA_TNUM y) {
#if !defined(TEA_GL)
    SDL_RenderDrawPoint(render()->handle, x, y);
#else
    float c[4];
    _to_gl_color(render()->stat.draw_color, c);
    glBegin(GL_POINTS);
    glVertex2f(x, y);
    glColor4f(c[0], c[1], c[2], c[3]);
    glEnd();
#endif
    return 1;
}

int tea_line(TEA_TNUM x0, TEA_TNUM y0, TEA_TNUM x1, TEA_TNUM y1) {
#if !defined(TEA_GL)
    SDL_RenderDrawLine(render()->handle, x0, y0, x1, y1);
#else
    float c[4];
    _to_gl_color(render()->stat.draw_color, c);
    glBegin(GL_LINES);
    glColor4f(c[0], c[1], c[2], c[3]);
    glVertex2f(x0, y0);
    glColor4f(c[0], c[1], c[2], c[3]);
    glVertex2f(x1, y1);
    glEnd();
#endif
    return 1;
}

static int _draw_fill_rect(TEA_TNUM x, TEA_TNUM y, TEA_TNUM w, TEA_TNUM h) {
    te_transform_t *t = &(render()->stat.transform);
    SDL_Rect r;
    r.x = x + t->translate.x;
    r.y = y + t->translate.y;
    r.w = w;
    r.h = h;

#if !defined(TEA_GL)
    SDL_RenderFillRect(render()->handle, &r);
#else
    float c[4];
    _to_gl_color(render()->stat.draw_color, c);
    glBegin(GL_TRIANGLES);
    glColor4f(c[0], c[1], c[2], c[3]);
    glVertex2f(x, y);
    glColor4f(c[0], c[1], c[2], c[3]);
    glVertex2f(x+w, y);
    glColor4f(c[0], c[1], c[2], c[3]);
    glVertex2f(x+w, y+h);

    glColor4f(c[0], c[1], c[2], c[3]);
    glVertex2f(x+w, y+h);
    glColor4f(c[0], c[1], c[2], c[3]);
    glVertex2f(x, y+h);
    glColor4f(c[0], c[1], c[2], c[3]);
    glVertex2f(x, y);
    glEnd();
#endif
    return 1;
}

static int _draw_line_rect(TEA_TNUM x, TEA_TNUM y, TEA_TNUM w, TEA_TNUM h) {
    te_transform_t *t = &(render()->stat.transform);
    SDL_Rect r;
    r.x = x + t->translate.x;
    r.y = y + t->translate.y;
    r.w = w;
    r.h = h;
#if !defined(TEA_GL)
    SDL_RenderDrawRect(render()->handle, &r);
#else
    float c[4];
    _to_gl_color(render()->stat.draw_color, c);
    glBegin(GL_LINES);
    glColor4f(c[0], c[1], c[2], c[3]);
    glVertex2f(x, y);
    glColor4f(c[0], c[1], c[2], c[3]);
    glVertex2f(x+w, y);
    glColor4f(c[0], c[1], c[2], c[3]);
    glVertex2f(x+w, y+h);
    glColor4f(c[0], c[1], c[2], c[3]);
    glVertex2f(x, y+h);
    glEnd();
#endif
    return 1;
}

typedef int(*DrawRect)(TEA_TNUM, TEA_TNUM, TEA_TNUM, TEA_TNUM);
static DrawRect rect_fn[2] = {_draw_line_rect, _draw_fill_rect};

int tea_rect(TEA_TNUM x, TEA_TNUM y, TEA_TNUM w, TEA_TNUM h) {
    int mode = render()->stat.draw_mode;
    return rect_fn[mode](x, y, w, h);
}

static int _draw_line_circle(TEA_TNUM x, TEA_TNUM y, TEA_TNUM radius) {
    int xx = -radius;
    int yy = 0;
    int r = radius;
    int err = 2 - 2*r;
    do {
        tea_point(x-xx, y+yy);
        tea_point(x-yy, y-xx);
        tea_point(x+xx, y-yy);
        tea_point(x+yy, y+xx);
        r = err;
        if (r <= yy) err += ++yy*2+1;
        if (r > xx || err > yy) err += ++xx*2+1;
    } while (xx < 0);
    return 1;
}

static int _draw_fill_circle(TEA_TNUM x, TEA_TNUM y, TEA_TNUM radius) {
    int xx = 0;
    int yy = radius;

    int P = 1 - radius;

    if (radius > 0) tea_line(x+radius, y, x-radius, y);

    while (xx <= y) {
        if (P < 0) P += 2*xx + 3;
        else {
            P += (2*(xx-yy))+5;
            yy--;
        }
        xx++;
        if (xx > yy) break;

        tea_line(x-xx, y+yy, x+xx, y+yy);
        tea_line(x+xx, y-yy, x-xx, y-yy);
        if (xx != yy) {
            tea_line(x-yy, y+xx, x+yy, y+xx);
            tea_line(x+yy, y-xx, x-yy, y-xx);
        }
    }
    return 1;
}

typedef int(*DrawCircle)(TEA_TNUM, TEA_TNUM, TEA_TNUM);
static DrawCircle circ_fn[2] = {_draw_line_circle, _draw_fill_circle};

int tea_circle(TEA_TNUM x, TEA_TNUM y, TEA_TNUM radius) {
    int mode = render()->stat.draw_mode;
    return circ_fn[mode](x, y, radius);
}

static int _draw_line_triang(TEA_TNUM x0, TEA_TNUM y0, TEA_TNUM x1, TEA_TNUM y1, TEA_TNUM x2, TEA_TNUM y2) {
    tea_line(x0, y0, x1, y1);
    tea_line(x1, y1, x2, y2);
    tea_line(x2, y2, x0, y0);
    return 1;
}

static int fill_bottom_flat_triangle(TEA_TNUM x0, TEA_TNUM y0, TEA_TNUM x1, TEA_TNUM y1, TEA_TNUM x2, TEA_TNUM y2) {
    int dy = (y2 - y0);
    float invslope0 = (float)(x1 - x0) / dy;
    float invslope1 = (float)(x2 - x0) / dy;

    float curx1 = x0;
    float curx2 = x0;

    int scanline_y;
    for (scanline_y = y0; scanline_y <= y1; scanline_y++) {
        tea_line(curx1, scanline_y, curx2, scanline_y);
        curx1 += invslope0;
        curx2 += invslope1;
    }
    return 1;
}

static int fill_top_flat_triangle(TEA_TNUM x0, TEA_TNUM y0, TEA_TNUM x1, TEA_TNUM y1, TEA_TNUM x2, TEA_TNUM y2) {
    int dy = (y2 - y0);
    float invslope0 = (float)(x2 - x0) / dy;
    float invslope1 = (float)(x2 - x1) / dy;

    float curx1 = x2;
    float curx2 = x2;

    int scanline_y;
    for (scanline_y = y2; scanline_y > y1; scanline_y--) {
        tea_line(curx1, scanline_y, curx2, scanline_y);
        curx1 -= invslope0;
        curx2 -= invslope1;
    }
    return 1;
}

static void points_ord_y(te_point_t *points, int len) {
    for (int i = 0; i < len; i++) {
        for (int j = 0; j < len-1; j++) {
            if (points[j].y < points[j+1].y) continue;
            te_point_t aux = points[j];
            points[j] = points[j+1];
            points[j+1] = aux;
        }
    }
}

static int _draw_fill_triang(TEA_TNUM x0, TEA_TNUM y0, TEA_TNUM x1, TEA_TNUM y1, TEA_TNUM x2, TEA_TNUM y2) {
    te_point_t points[3];
    points[0] = TEA_POINT(x0, y0);
    points[1] = TEA_POINT(x1, y1);
    points[2] = TEA_POINT(x2, y2);

    points_ord_y(points, 3);

    if (points[1].y == points[2].y) fill_bottom_flat_triangle(points[0].x, points[0].y, points[1].x, points[1].y, points[2].x, points[2].y);
    else if (points[0].y == points[1].y) fill_bottom_flat_triangle(points[0].x, points[0].y, points[1].x, points[1].y, points[2].x, points[2].y);
    else {
        te_point_t p = TEA_POINT(
                (points[0].x + ((points[1].y - points[0].y) / (points[2].y - points[0].y)) * (points[2].x - points[0].x)),
                points[1].y
                );

        fill_bottom_flat_triangle(points[0].x, points[0].y, points[1].x, points[1].y, p.x, p.y);
        fill_top_flat_triangle(points[1].x, points[1].y, p.x, p.y, points[2].x, points[2].y);
    }
    return 1;
}

typedef int(*DrawTriang)(TEA_TNUM, TEA_TNUM, TEA_TNUM, TEA_TNUM, TEA_TNUM, TEA_TNUM);
static DrawTriang tria_fn[2] = {_draw_line_triang, _draw_fill_triang};

int tea_triangle(TEA_TNUM x0, TEA_TNUM y0, TEA_TNUM x1, TEA_TNUM y1, TEA_TNUM x2, TEA_TNUM y2) {
    int mode = render()->stat.draw_mode;
    return tria_fn[mode](x0, y0, x1, y1, x2, y2);
}

int tea_print(const char *text, TEA_TNUM x, TEA_TNUM y) {
    // tea_texture_draw(tea()->default_font->tex, NULL, NULL);
    return tea_font_print(tea()->default_font, text, x, y);
}

int tea_set_target(te_texture_t *target) {
#if !defined(TEA_GL)
    SDL_Texture *tex = NULL;
    if (target) {
        TEA_ASSERT(target->usage & TEA_TEXTURE_TARGET, "Texture is not target");
        tex = (SDL_Texture*)target->handle;
    }

    SDL_SetRenderTarget(render()->handle, tex);
#endif

    return 1;
}

int tea_set_shader(te_shader_t *shader) { return 1; }
int tea_set_font(te_font_t *font) { return 1; }
int tea_set_transform(te_transform_t *transform) { return 1; }

int tea_set_blendmode(int mode) {
    SDL_SetRenderDrawBlendMode(render()->handle, mode);
    return 0;
}

int tea_clip(te_rect_t *clip) { 
    if (!clip) return SDL_RenderSetClipRect(render()->handle, NULL);
    SDL_Rect r = (SDL_Rect){clip->x, clip->y, clip->w, clip->h};
    return SDL_RenderSetClipRect(render()->handle, &r);
}
int tea_translate(TEA_TNUM x, TEA_TNUM y) { return 1; }
int tea_rotate(TEA_TNUM angle) { return 1; }
int tea_scale(TEA_TNUM x, TEA_TNUM y) { return 1; }
int tea_origin(TEA_TNUM x, TEA_TNUM y) { return 1; }

/*********************************
 * Texture
 *********************************/

int tea_texture_info(te_texture_t *tex, te_texinfo_t *out) {
    if (!tex) return 0;
    if (!out) return 0;

    out->size.w = tex->width;
    out->size.h = tex->height;
    out->format = tex->channels;
    out->usage = tex->usage;
    return 1;
}

te_texture_t* tea_texture(void *data, int w, int h, int format, int usage) {
    if (format < 0 || format > TEA_PIXELFORMAT_COUNT) {
        tea_error("Invalid pixel format");    
        return NULL;
    }

    te_texture_t *tex = (te_texture_t*)malloc(sizeof(*tex)); 
    TEA_ASSERT(tex != NULL, "Failed to alloc mem for texture");
    memset(tex, 0, sizeof(*tex));

    tex->channels = format;
    tex->width = w;
    tex->height = h;
    tex->usage = usage;

#if 0
    if (!data) {
        tex->handle = SDL_CreateTexture(render()->handle, pixel_format(format), usage, w, h);
        TEA_ASSERT(tex->handle != NULL, "Failed to create SDL_Texture: %s", SDL_GetError());
        return tex;
    }
#endif

#if !defined(TEA_GL)
    tex->handle = SDL_CreateTexture(render()->handle, pixel_format(format), usage, w, h);
    SDL_SetTextureBlendMode(tex->handle, SDL_BLENDMODE_BLEND);

    if (data) tea_texture_update(tex, NULL, data);
#else
    glGenTextures(1, &tex->handle);
    glBindTexture(GL_TEXTURE_2D, tex->handle);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    if (data) glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tex->width, tex->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glBindTexture(GL_TEXTURE_2D, 0);
#endif

#if 0
    SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormatFrom(data, w, h, depth, pitch, _format);
    if (!surf) {
        tea_error("Failed to create SDL_Surface: %s", SDL_GetError());
        return 0;
    }
    tex->handle = SDL_CreateTextureFromSurface(render()->handle, surf);
    SDL_FreeSurface(surf);
    if (!tex->handle) {
        tea_error("Failed to create SDL_Texture: %s", SDL_GetError());
        return 0;
    }
#endif

    return tex;
}

te_texture_t* tea_texture_load(const char *path, int usage) {
    te_texture_t *tex = NULL;
    if (!path) {
        tea_error("invalid path");
        return tex;
    }

    int req_format = TEA_RGBA;
    int w, h, format;

    unsigned char *pixels = stbi_load(path, &w, &h, &format, req_format);
    if (!pixels) {
        tea_error("Failed to load image: %s", path);
        return NULL;
    }

    tex = tea_texture(pixels, w, h, format, usage);

    return tex;
}

te_texture_t* tea_texture_from_memory(void *data, int data_size, int usage) {
    te_texture_t *tex = NULL;
    if (!data) {
        tea_error("invalid path");
        return tex;
    }

    int req_format = TEA_RGBA;
    int w, h, format;
    unsigned char *pixels = stbi_load_from_memory(data, data_size, &w, &h, &format, req_format);
    if (!pixels) {
        tea_error("Invalid image format");
        return tex;
    }
    tex = tea_texture(pixels, w, h, format, usage);


    return tex;
}

int tea_texture_update(te_texture_t *tex, te_rect_t *rect, void *data) {
    TEA_ASSERT(tex != NULL, "Texture cannot be null");
#if !defined(TEA_GL)
    int pitch = tex->channels * tex->width;
    return SDL_UpdateTexture(tex->handle, NULL, data, pitch);
#else
    return 0;
#endif
}

int tea_texture_width(te_texture_t *tex) {
    TEA_ASSERT(tex != NULL, "Texture cannot be NULL");
    return tex->width;
}

int tea_texture_height(te_texture_t *tex) {
    TEA_ASSERT(tex != NULL, "Texture cannot be NULL");
    return tex->height;
}

int tex_texture_size(te_texture_t *tex, int *w_out, int *h_out) {
    TEA_ASSERT(tex != NULL, "Texture cannot be NULL");
    if (w_out) *w_out = tex->width;
    if (h_out) *h_out = tex->height;
    return 1;
}

int tea_texture_draw(te_texture_t *tex, te_rect_t *dest, te_rect_t *src) {
    TEA_ASSERT(tex != NULL, "Texture cannot be NULL");
#if !defined(TEA_GL)
    SDL_Rect d, s;
    d = (SDL_Rect){0, 0, tex->width, tex->height};
    s = (SDL_Rect){0, 0, tex->width, tex->height};
    if (dest) d = _to_sdl_rect(dest);
    if (src) s = _to_sdl_rect(src);

    SDL_RenderCopy(render()->handle, tex->handle, &s, &d);
#else
    te_rect_t d, s;
    d = TEA_RECT(0, 0, tex->width, tex->height);
    s = TEA_RECT(0, 0, tex->width, tex->height);
    if (dest) memcpy(&d, dest, sizeof(*dest));
    if (src) memcpy(&s, src, sizeof(*src));

    float srcf[4];
    srcf[0] = (float)(s.x / tex->width);
    srcf[1] = (float)(s.y / tex->height);
    srcf[2] = srcf[0] + (float)(s.w / tex->width);
    srcf[3] = srcf[1] + (float)(s.h / tex->height);

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, tex->handle);
    glBegin(GL_QUADS);
    glTexCoord2f(srcf[0], srcf[1]); glVertex2f(d.x, d.y);
    glTexCoord2f(srcf[2], srcf[1]); glVertex2f(d.x+d.w, d.y);
    glTexCoord2f(srcf[2], srcf[3]); glVertex2f(d.x+d.w, d.y+d.h);
    glTexCoord2f(srcf[0], srcf[3]); glVertex2f(d.x, d.y+d.h);
    glEnd();
    glDisable(GL_TEXTURE_2D);
#endif

    return 1;
}

int tea_texture_draw_ex(te_texture_t *tex, te_rect_t *dest, te_rect_t *src, TEA_TNUM angle, te_point_t *origin, int flip) {
    TEA_ASSERT(tex != NULL, "Texture cannot be NULL");
#if !defined(TEA_GL)
    SDL_Rect d, s;
    d = (SDL_Rect){0, 0, tex->width, tex->height};
    s = (SDL_Rect){0, 0, tex->width, tex->height};

    if (dest) d = _to_sdl_rect(dest);
    if (src) s = _to_sdl_rect(src);

    SDL_Point o = {0, 0};
    if (origin) o = (SDL_Point){origin->x, origin->y};
    SDL_RenderCopyEx(render()->handle, tex->handle, &s, &d, (int)angle, &o, flip);
#endif

    return 1;
}

/*********************************
 * Font
 *********************************/

te_font_t* tea_default_font(te_font_t *def) {
    if (def) tea()->default_font = def;
    if (tea()->default_font) return tea()->default_font;
    te_font_t *font = NULL;

    te_texture_t *tex = NULL; 
    int texw, texh;
    texw = 128*8;
    texh = 8;
    Uint32 pixels[texw*texh];
    memset(pixels, 0, texw*texh*4);

    int i;
    for (i = 0; i < 128; i++) {
        char *letter = font8x8_basic[i];
        for (int yy = 0; yy < 8; yy++) {
            char line = letter[yy];    
            int offset = (i*8)+(yy*texw);
            for (int xx = 0; xx < 8; xx++) {
                char a = ((line >> xx) & 0x1) * 255;
                int o = offset+xx;

                pixels[o] = (a << 24) | 0xffffff;
            }
        }
    }

    tex = tea_texture(pixels, texw, texh, TEA_RGBA, TEA_TEXTURE_STATIC);
    if (!tex) {
        fprintf(stderr, "Failed to create default font texture: %s\n", tea_geterror());
        exit(0);
    }

    font = tea_font_bitmap(tex, 8, 1, 0);
    return font;
}

te_font_t* tea_font(void *data, int size, int font_size) {
    TEA_ASSERT(font_size > 0, "Invalid font size");
    TEA_ASSERT(data != NULL, "Font data cannot be null");

    te_font_t *font = NULL;
    stbtt_fontinfo info;
    if (!stbtt_InitFont(&info, (const unsigned char*)data, 0)) {
        tea_error("Invalid font data");
        return NULL;
    }

    font = (te_font_t*)malloc(sizeof(*font));
    font->data = data;
    memcpy(&font->info, &info, sizeof(stbtt_fontinfo));

    int ascent, descent, line_gap;
    font->size = font_size;
    float fsize = font_size;

    font->scale = stbtt_ScaleForMappingEmToPixels(&font->info, fsize);
    stbtt_GetFontVMetrics(&font->info, &ascent, &descent, &line_gap);
    font->baseline = ascent * font->scale;

    int tw, th;
    tw = th = 0;

    int i;
    for (i = 0; i < MAX_FONT_CHAR; i++) {
        int ax, bl;
        int x0, y0, x1, y1;
        int w, h;

        stbtt_GetCodepointHMetrics(&font->info, i, &ax, &bl);
        stbtt_GetCodepointBitmapBox(&font->info, i, font->scale, font->scale, &x0, &y0, &x1, &y1);
        w = x1 - x0;
        h = y1 - y0;


        font->c[i].ax = ax * font->scale;
        font->c[i].ay = 0;
        font->c[i].bl = bl * font->scale;
        font->c[i].bw = w;
        font->c[i].bh = h;
        font->c[i].bt = font->baseline + y0;

        tw += w;
        th = MAX(th, h);
    }

    font->tex = tea_texture(NULL, tw, th, TEA_RGBA, TEA_TEXTURE_STREAM);

#if !defined(TEA_GL)
    SDL_SetTextureBlendMode(font->tex->handle, SDL_BLENDMODE_BLEND);
#endif

    int x = 0;
    for (i = 0; i < MAX_FONT_CHAR; i++) {
        int ww = font->c[i].bw;
        int hh = font->c[i].bh;
        int ssize = ww * hh;
        int ox, oy;

        unsigned char *bitmap = stbtt_GetCodepointBitmap(&font->info, 0, font->scale, i, NULL, NULL, &ox, &oy);
#if !defined(TEA_GL)
        Uint32 *pixels = NULL;
#else
        te_color_t pixels[ww*hh];
#endif
        int pitch;

        SDL_Rect r;
        r.x = x;
        r.y = 0;
        r.w = ww;
        r.h = hh;


#if !defined(TEA_GL)
        if (SDL_LockTexture(font->tex->handle, &r, (void**)&pixels, &pitch) != 0) {
            fprintf(stderr, "Failed to lock SDL_Texture: %s\n", SDL_GetError());
            exit(0);
        }

        int yy = 0;
        for (int j = 0; j < ssize; j++) {
            int xx = j % ww;
            if (j != 0 && xx == 0) yy++;
            int index = xx + (yy * (pitch / 4));

            Uint32 pp = bitmap[j];
            pp <<= 24;
            pp |= 0xffffff;
            pixels[index] = pp;
        }
        SDL_UnlockTexture(font->tex->handle);
#else
    int yy = 0;
    for (int j = 0; j < ssize; j++) {
        int xx = j % ww;
        if (j != 0 && xx == 0) yy++;
        int index = xx + (yy * (pitch / 4));

        Uint32 pp = 0xffffff00 | bitmap[j];
        pixels[index] = pp;
    }

    glTexSubImage2D(GL_TEXTURE_2D, 0, r.x, r.y, r.w, r.h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
#endif
        font->c[i].tx = x;

        x += font->c[i].bw;
    }
    return font;
}

#if 0
te_font_t* tea_font_ttf(void *data, int size, int font_size) {
    TEA_ASSERT(font_size > 0, "Invalid font size");
    TEA_ASSERT(data != NULL, "Font data cannot be null");

    te_font_t *font = NULL;
    stbtt_fontinfo info;
    if (!stbtt_InitFont(&info, (const unsigned char*)data, 0)) {
        tea_error("Invalid font data");
        return NULL;
    }

    font = (te_font_t*)malloc(sizeof(*font));
    font->data = data;
    memcpy(&font->info, &info, sizeof(stbtt_fontinfo));

    int ascent, descent, line_gap;
    font->size = font_size;
    float fsize = font_size;

    font->scale = stbtt_ScaleForMappingEmToPixels(&font->info, fsize);
    stbtt_GetFontVMetrics(&font->info, &ascent, &descent, &line_gap);
    font->baseline = ascent * font->scale;

    int tw, th;
    tw = th = 0;

    int i;
    for (i = 0; i < MAX_FONT_CHAR; i++) {
        int ax, bl;
        int x0, y0, x1, y1;
        int w, h;

        stbtt_GetCodepointHMetrics(&font->info, i, &ax, &bl);
        stbtt_GetCodepointBitmapBox(&font->info, i, font->scale, font->scale, &x0, &y0, &x1, &y1);
        w = x1 - x0;
        h = y1 - y0;


        font->c[i].ax = ax * font->scale;
        font->c[i].ay = 0;
        font->c[i].bl = bl * font->scale;
        font->c[i].bw = w;
        font->c[i].bh = h;
        font->c[i].bt = font->baseline + y0;

        tw += w;
        th = MAX(th, h);
    }

    font->tex = tea_texture(NULL, tw, th, TEA_RGBA, TEA_TEXTURE_STREAM);

    SDL_SetTextureBlendMode(font->tex->handle, SDL_BLENDMODE_BLEND);

    int x = 0;
    for (i = 0; i < MAX_FONT_CHAR; i++) {
        int ww = font->c[i].bw;
        int hh = font->c[i].bh;
        int ssize = ww * hh;
        int ox, oy;

        unsigned char *bitmap = stbtt_GetCodepointBitmap(&font->info, 0, font->scale, i, NULL, NULL, &ox, &oy);
        Uint32 *pixels = NULL;
        int pitch;

        SDL_Rect r;
        r.x = x;
        r.y = 0;
        r.w = ww;
        r.h = hh;


        if (SDL_LockTexture(font->tex->handle, &r, (void**)&pixels, &pitch) != 0) {
            fprintf(stderr, "Failed to lock SDL_Texture: %s\n", SDL_GetError());
            exit(0);
        }

        int yy = 0;
        for (int j = 0; j < ssize; j++) {
            int xx = j % ww;
            if (j != 0 && xx == 0) yy++;
            int index = xx + (yy * (pitch / 4));

            Uint32 pp = bitmap[j];
            pp <<= 24;
            pp |= 0xffffff;
            pixels[index] = pp;
        }
        SDL_UnlockTexture(font->tex->handle);
        font->c[i].tx = x;

        x += font->c[i].bw;
    }
    return font;
}
#endif
te_font_t* tea_font_load(const char *filename, int size) {
    te_font_t *font = NULL; 
    TEA_ASSERT(filename != NULL, "Font filename cannot be null");
    TEA_ASSERT(size > 0, "Invalid font size");

    FILE *fp;
    fp = fopen(filename, "rb");
    TEA_ASSERT(fp != NULL, "Invalid filename");

    unsigned int sz;
    fseek(fp, 0, SEEK_END);
    sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    char data[sz];
    fread(data, 1, sz, fp);

    font = tea_font(data, sz, size);
    fclose(fp);

    return font;
}

te_font_t* tea_font_bitmap(te_texture_t *tex, int size, int top, int right) {
    if (!tex) {
        tea_error("Texture cannot be NULL");
        return NULL;
    }

    te_font_t *font = (te_font_t*)malloc(sizeof(*font));
    memset(font, 0, sizeof(*font));
    font->tex = tex;
#if !defined(TEA_GL)
    SDL_SetTextureBlendMode(font->tex->handle, SDL_BLENDMODE_BLEND);
#endif

    for (int i = 0; i < MAX_FONT_CHAR; i++) {
        font->c[i].ax = size;
        font->c[i].ay = 0;
        font->c[i].bh = size;
        font->c[i].bw = size;
        font->c[i].tx = i*size;
        font->c[i].bt = top;
    }

    return font;
}

int tea_font_char_rect(te_font_t *font, unsigned int c, te_rect_t *r) {
    if (!font) {
        fprintf(stderr, "Font cannot be null\n");
        return 0;
    }

    if (c == '\n' || c == '\t') return 1;
    if (c >= MAX_FONT_CHAR) return 0;

    if (r) *r = TEA_RECT(font->c[c].tx, 0, font->c[c].bw, font->c[c].bh);
    return 1;
}

int tea_font_print(te_font_t *font, const char *text, TEA_TNUM x, TEA_TNUM y) {
    TEA_ASSERT(font != NULL, "Font cannot be NULL");
    if (!text) return 0;
    char *p = (char*)text;
    te_color_t color = render()->stat.draw_color;
    unsigned char c[4];

    for (int i = 0; i < 4; i++) {
        c[i] = color & 0xff;
        color >>= 8;
    }

#if !defined(TEA_GL)
    SDL_SetTextureColorMod(font->tex->handle, c[0], c[1], c[2]);
    SDL_SetTextureAlphaMod(font->tex->handle, c[3]);
#endif

    while (*p) {
        te_rect_t r;
        tea_font_char_rect(font, *p, &r);
        int index = (int)*p;
        tea_texture_draw(font->tex, &TEA_RECT(x, y+font->c[index].bt, r.w, r.h), &r);
        x += font->c[index].ax;

        p++;
    }

    return 0;
}

/*******************************
 * Event
 *******************************/

typedef int(*teaCallback)(te_event_t*);

static int _keyboard_callback(te_event_t *ev);
static int _controller_callback(te_event_t *ev);

static int _window_callback(te_event_t *ev);
static int _window_move_callback(te_event_t *ev);
static int _window_resize_callback(te_event_t *ev);
static int _window_visible_callback(te_event_t *ev);
static int _window_mouse_callback(te_event_t *ev);
static int _window_close_callback(te_event_t *ev);

static int _drop_callback(te_event_t *ev);
static int _drop_file_callback(te_event_t *ev);
static int _drop_text_callback(te_event_t *ev);

static int _text_input_callback(te_event_t *ev);
static int _text_edit_callback(te_event_t *ev);


const teaCallback _callbacks[] = {
    [SDL_WINDOWEVENT] = _window_callback, 
    [SDL_CONTROLLERDEVICEADDED] = _controller_callback,
    [SDL_CONTROLLERDEVICEREMOVED] = _controller_callback,
    [SDL_CONTROLLERDEVICEREMAPPED] = _controller_callback,
    [SDL_KEYDOWN] = _keyboard_callback,
    [SDL_KEYUP] = _keyboard_callback,
    [SDL_DROPBEGIN] = _drop_callback,
    [SDL_DROPCOMPLETE] = _drop_callback,
    [SDL_DROPFILE] = _drop_file_callback,
    [SDL_DROPTEXT] = _drop_text_callback,
    [SDL_TEXTINPUT] = _text_input_callback,
    [SDL_TEXTEDITING] = _text_edit_callback,
    [SDL_LASTEVENT] = NULL
};

const teaCallback _window_callbacks[] = {
    [SDL_WINDOWEVENT_NONE] = NULL,
    [SDL_WINDOWEVENT_MOVED] = _window_move_callback,
    [SDL_WINDOWEVENT_RESIZED] = _window_resize_callback,
    [SDL_WINDOWEVENT_CLOSE] = _window_close_callback,
    [SDL_WINDOWEVENT_SHOWN] = _window_visible_callback,
    [SDL_WINDOWEVENT_HIDDEN] = _window_visible_callback,
    [SDL_WINDOWEVENT_ENTER] = _window_mouse_callback,
    [SDL_WINDOWEVENT_LEAVE] = _window_mouse_callback,
    [SDL_WINDOWEVENT_HIT_TEST] = NULL
};

int tea_poll_event(te_event_t *out) {
    memcpy(tea()->input.key.old_state, tea()->input.key.state, TEA_KEY_COUNT);
    int mx, my;
    int mouse_state = SDL_GetMouseState(&mx, &my);

    tea()->input.mouse.x = mx;
    tea()->input.mouse.y = my;

    for (int i = 0; i < TEA_MOUSE_COUNT; i++) {
        tea()->input.mouse.old_state[i] = tea()->input.mouse.state[i];
        tea()->input.mouse.state[i] = mouse_state & SDL_BUTTON(i+1);
    }

    while (SDL_PollEvent(out)) {
        teaCallback fn = _callbacks[out->type];
        if (fn) fn(out); 
    }

    return 1;
}

typedef void(*_SDLTextInput)(void);

int tea_textinput(int mode) {
    _SDLTextInput md[2] = { SDL_StopTextInput, SDL_StartTextInput };
    md[mode]();
    return 0;
}

int tea_event_key(teaKeyboardEv fn) {
    tea()->callback.key = fn;
    return 0;
}

int tea_event_controller(teaControllerEv fn) {
    tea()->callback.ctrl = fn;
    return 0;
}

int tea_event_window_move(teaWindowMoveEv fn) {
    tea()->callback.window.move = fn;
    return 0;
}

int tea_event_window_resize(teaWindowResizeEv fn) {
    tea()->callback.window.resize = fn;
    return 0;
}

int tea_event_window_visible(teaWindowVisibleEv fn) {
    tea()->callback.window.visible = fn;
    return 0;
}

int tea_event_window_focus(teaWindowFocusEv fn) {
    tea()->callback.window.focus = fn;
    return 0;
}

int tea_event_window_mouse(teaWindowMouseEv fn) {
    tea()->callback.window.mouse = fn;
    return 0;
}

int tea_event_window_close(teaWindowCloseEv fn) {
    tea()->callback.window.close = fn;
    return 0;
}

int tea_event_drop(teaDropEv fn) {
    tea()->callback.drop = fn;
    return 0;
}

int tea_event_drop_file(teaDropFileEv fn) {
    tea()->callback.drop_file = fn;  
    return 0;
}

int tea_event_drop_text(teaDropTextEv fn) {
    tea()->callback.drop_text = fn;
    return 0;
}

int tea_event_text_input(teaTextInputEv fn) {
    tea()->callback.text_input = fn;
    return 0;
}

int tea_event_text_edit(teaTextEditEv fn) {
    tea()->callback.text_edit = fn;
    return 0;
}

/******* Internal Callbacks ********/

int _keyboard_callback(te_event_t *ev) {
    teaKeyboardEv fn = tea()->callback.key;
    struct te_keysym_t sym = {0};
    SDL_KeyboardEvent kev = ev->key;
    sym.scancode = kev.keysym.scancode;
    sym.keycode = kev.keysym.sym;
    sym.mod = kev.keysym.mod;
    if (fn) fn(ev->key.windowID, (ev->key.type - 0x301)*-1, ev->key.repeat, sym);
    return 0;
}
int _controller_callback(te_event_t *ev) {
    teaControllerEv fn = tea()->callback.ctrl;
    if (fn) fn(ev->cdevice.type, ev->cdevice.which);
    return 0; }

    int _window_callback(te_event_t *ev) {
        teaCallback fn = _window_callbacks[ev->window.event];
        if (fn) return fn(ev);
        return 0;
    }

int _window_resize_callback(te_event_t *ev) {
    teaWindowResizeEv fn = tea()->callback.window.resize;
    if (fn) fn(ev->window.windowID, ev->window.data1, ev->window.data2);
    return 0;
}

int _window_move_callback(te_event_t *ev) {
    teaWindowMoveEv fn = tea()->callback.window.move;
    if (fn) fn(ev->window.windowID, ev->window.data1, ev->window.data2);
    return 0;
}

int _window_visible_callback(te_event_t *ev) {
    teaWindowVisibleEv fn = tea()->callback.window.visible;
    int visible = 1;
    switch (ev->window.event) {
        case SDL_WINDOWEVENT_SHOWN: visible = 1; break;
        case SDL_WINDOWEVENT_HIDDEN: visible = 0; break;
    }

    if (fn) fn(ev->window.windowID, visible);
    return 0;
}

int _window_focus_callback(te_event_t *ev) {
    teaWindowFocusEv fn = tea()->callback.window.focus;
    int focused = (ev->window.event - 13) * -1;
    if (fn) fn(ev->window.windowID, focused);
    return 0;
}

int _window_mouse_callback(te_event_t *ev) {
    teaWindowMouseEv fn = tea()->callback.window.mouse;
    int enter = 0;
    switch (ev->window.event) {
        case SDL_WINDOWEVENT_ENTER: enter = 1; break;
        case SDL_WINDOWEVENT_LEAVE: enter = 0; break;
    }
    if (fn) fn(ev->window.windowID, enter);
    return 0;
}

int _window_close_callback(te_event_t *ev) {
    teaWindowCloseEv fn = tea()->callback.window.close;
    if (fn) return fn(ev->window.windowID);
    return 0;
}

int _drop_callback(te_event_t *ev) {
    teaDropEv fn = tea()->callback.drop;
    if (fn) fn(ev->drop.windowID, ev->drop.type);
    return 0;
}

int _drop_file_callback(te_event_t *ev) {
    teaDropFileEv fn = tea()->callback.drop_file;
    if (fn) fn(ev->drop.windowID, ev->drop.file);
    return 0;
}
int _drop_text_callback(te_event_t *ev) {
    teaDropTextEv fn = tea()->callback.drop_text;
    if (fn) fn(ev->drop.windowID, ev->drop.file);
    return 0;
}

int _text_input_callback(te_event_t *ev) {
    teaTextInputEv fn = tea()->callback.text_input;
    if (fn) fn(ev->text.windowID, ev->text.text);
    return 0;
}

int _text_edit_callback(te_event_t *ev) {
    teaTextEditEv fn = tea()->callback.text_edit;
    if (fn) fn(ev->edit.windowID, ev->edit.text, ev->edit.start, ev->edit.length);
    return 0;
}

/*******************************
 * Window
 *******************************/

int tea_window_title(const char *title, char *out) {
    if (title) SDL_SetWindowTitle(window(), title);
    if (out) strcpy(out, SDL_GetWindowTitle(window()));

    return 1;
}

int tea_window_flags() {
    int flags = SDL_GetWindowFlags(window());
    return flags;
}

int tea_window_pos(te_point_t *out, int x, int y) {
    if (out) {
        int xx, yy;
        SDL_GetWindowPosition(window(), &xx, &yy);
        out->x = xx;
        out->y = yy;
    } else {
        SDL_SetWindowPosition(window(), x, y);
    }


    return 1;
}

int tea_window_resizable(int resizable) {
    SDL_SetWindowResizable(window(), resizable);
    return 1;
}

int tea_window_width(int *out, int width) {
    SDL_Window *w = window();
    int ww, hh;
    SDL_GetWindowSize(w, &ww, &hh);
    if (!out) SDL_SetWindowSize(w, width, hh);
    else *out = ww;

    return 1;
}

int tea_window_height(int *out, int height) {
    SDL_Window *w = window();
    int ww, hh;
    SDL_GetWindowSize(w, &ww, &hh);
    if (!out) SDL_SetWindowSize(w, ww, height);
    else *out = hh;

    return 1;
}

int tea_window_size(te_point_t *out, int width, int height) {
    if (out) {
        int xx, yy;
        SDL_GetWindowSize(window(), &xx, &yy);
        out->x = xx;
        out->y = yy;
    } else {
        SDL_SetWindowSize(window(), width, height);
    }

    return 1;
}

int tea_window_minsize(te_point_t *out, int width, int height) {
    if (out) {
        int ww, hh;
        SDL_GetWindowMinimumSize(window(), &ww, &hh);
        out->x = ww;
        out->y = hh;
    } else SDL_SetWindowMinimumSize(window(), width, height);
    return 1;
}

int tea_window_maxsize(te_point_t *out, int width, int height) {
    if (out) {
        int ww, hh;
        SDL_GetWindowMaximumSize(window(), &ww, &hh);
        out->x = ww;
        out->y = hh;
    } else SDL_SetWindowMaximumSize(window(), width, height);
    return 1;
}

int tea_window_minimize() {
    SDL_MinimizeWindow(window());
    return 1;
}

int tea_window_maximize() {
    SDL_MaximizeWindow(window());
    return 1;
}

int tea_window_restore() {
    SDL_RestoreWindow(window());
    return 1;
}

int tea_window_fullscreen(int mode) {
    SDL_SetWindowFullscreen(window(), mode);
    return 1;
}

int tea_window_bordered(int bordered) {
    SDL_SetWindowBordered(window(), bordered);
    return 1;
}


/*********************************
 * Input
 *********************************/


int tea_key_from_name(const char *name) {
    return SDL_GetScancodeFromName(name);
}

const char* tea_key_name(int key) {
    return SDL_GetScancodeName(key);
}

int tea_key_down(int key) {
    return tea()->input.key.state[key];
}

int tea_key_up(int key) {
    return !tea()->input.key.state[key];
}

int tea_key_pressed(int key) {
    int pressed = !tea()->input.key.old_state[key] && tea()->input.key.state[key];
    return pressed;
}
int tea_key_released(int key) {
    int released = tea()->input.key.old_state[key] && !tea()->input.key.state[key];
    return released;
}

/* Mouse */

int tea_mouse_pos(int *x, int *y) {
    if (x) *x = tea()->input.mouse.x;
    if (y) *y = tea()->input.mouse.y;
    return 1;
}

int tea_mouse_scroll(int *x, int *y) {
    if (x) *x = tea()->input.mouse.scrollx;
    if (y) *y = tea()->input.mouse.scrolly;
    return 1;
}

int tea_mouse_down(int button) {
    return tea()->input.mouse.state[button];
}
int tea_mouse_up(int button) {
    return !tea_mouse_down(button);
}

int tea_mouse_pressed(int button) {
    return !tea()->input.mouse.old_state[button] && tea_mouse_down(button);
}
int tea_mouse_released(int button) {
    return tea()->input.mouse.old_state[button] && tea_mouse_up(button);
}

float tea_jpad_axis(int jid, int axis) {
    return 1;
}
int tea_jpad_down(int jid, int button) {
    return 1;
}
int tea_jpad_up(int jid, int button) {
    return 1;
}
int tea_jpad_pressed(int jid, int button) {
    return 1;
}
int tea_jpad_released(int jid, int button) {
    return 1;
}

/* Joystick */

int tea_joystick_info(te_joystick_t *j, te_joyinfo_t *out) {
    if (!out) return -1;
    out->vendor_id = SDL_JoystickGetVendor(j);
    out->product_id = SDL_JoystickGetProduct(j);
    out->product_ver = SDL_JoystickGetProductVersion(j);
    return 0;
}
te_joystick_t *tea_joystick(int index) { return SDL_JoystickOpen(index); }
int tea_joystick_close(te_joystick_t *j) { SDL_JoystickClose(j); return 0; }

int tea_joystick_count(void) { return SDL_NumJoysticks(); }
int tea_joystick_isopen(te_joystick_t *j) { return SDL_JoystickGetAttached(j); }

int tea_joystick_powerlevel(te_joystick_t *j) { return SDL_JoystickCurrentPowerLevel(j); }
int tea_joystick_vibration(te_joystick_t *j, int lfreq, int hfreq, int ms) { return SDL_JoystickRumble(j, lfreq, hfreq, ms); }

const char* tea_joystick_name(te_joystick_t *j) { return SDL_JoystickName(j); }
int tea_joystick_connected_index(te_joystick_t *j) { return SDL_JoystickGetPlayerIndex(j); }
te_joyID_t tea_joystick_instance_id(te_joystick_t *j) { return SDL_JoystickInstanceID(j); }
te_joyGUID_t tea_joystick_GUID(te_joystick_t *j) {
    te_joyGUID_t guid;
    SDL_JoystickGUID sguid = SDL_JoystickGetGUID(j);
    memcpy(&guid, &sguid, sizeof(guid));
    return guid;
}
int tea_joystick_is_gamepad(te_joystick_t *j) {
    return tea_joystick_gamepad(j) != NULL;
}

void *tea_joystick_gamepad(te_joystick_t *j) {
    te_joyID_t id = tea_joystick_instance_id(j);
    return SDL_GameControllerFromInstanceID(id);
}

int tea_joystick_axis_count(te_joystick_t *j) { return SDL_JoystickNumAxes(j); }
int tea_joystick_button_count(te_joystick_t *j) { return SDL_JoystickNumButtons(j); }
int tea_joystick_hat_count(te_joystick_t *j) { return SDL_JoystickNumHats(j); }
int tea_joystick_ball_count(te_joystick_t *j) { return SDL_JoystickNumBalls(j); }

int tea_joystick_axis(te_joystick_t *j, int axis) { return SDL_JoystickGetAxis(j, axis); }
int tea_joystick_button(te_joystick_t *j, int button) { return SDL_JoystickGetButton(j, button); }
int tea_joystick_hat(te_joystick_t *j, int hat) { return SDL_JoystickGetHat(j, hat); }
int tea_joystick_ball(te_joystick_t *j, int ball, int *dx, int *dy) { return SDL_JoystickGetBall(j, ball, dx, dy); }

#if 0
int tea_joystick_virtua(int type, int axes, int buttons, int hats) {
    return SDL_JoystickAttachVirtual(type, axes, buttons, hats);
}
int tea_joystick_destroy_virtual(int index) { return SDL_JoystickDetachVirtual(index); }
int tea_joystick_is_virtual(int index) { return SDL_JoystickIsVirtual(index); }

int tea_joystick_virtual_axis(te_joystick_t *j, int axis, short value) { return SDL_JoystickVirtualAxis(j, axis, value); }
int tea_joystick_virtual_button(te_joystick_t *j, int button, int state) { return SDL_JoystickVirtualButton(j, button, state); }
int tea_joystick_virtual_hat(te_joystick_t *j, int hat, int state) { return SDL_JoystickVirtualHat(j, hat, state); }
#endif

/* Debug */

#include <stdarg.h>

const char* tea_geterror() {
    return tea()->error_buf;
}

int tea_error(const char *fmt, ...) {
    va_list args;

    va_start(args, fmt);
    sprintf(tea()->error_buf, fmt, args);
    va_end(args);

    return 1;
}

int tea_log(int line, const char *func, const char *fmt, ...) {
    char buf[512];
    va_list args;

    va_start(args, fmt);
    sprintf(buf, fmt, args);
    fprintf(stderr, "%d: %s - %s", line, func, buf);
    va_end(args);
    return 1;
}

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
