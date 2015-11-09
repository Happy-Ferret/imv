/* Copyright (c) 2015 Harry Jeffery

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE. */

#include <stdio.h>
#include <SDL2/SDL.h>
#include <FreeImage.h>

SDL_Window *g_window = NULL;
SDL_Renderer *g_renderer = NULL;

struct loop_item_s {
  struct loop_item_s *prev;
  struct loop_item_s *next;
  const char *path;
};

struct {
  double scale;
  int x, y;
  int fullscreen;
  int redraw;
} g_view = {1,0,0,0,1};

struct {
  struct loop_item_s *first, *last, *cur;
  int changed;
  int dir;
} g_path = {NULL,NULL,NULL,1,1};

struct {
  FIMULTIBITMAP *mbmp;
  FIBITMAP *frame;
  SDL_Texture *tex;
  int width, height;
  int cur_frame, next_frame, num_frames, playing;
  double frame_time;
} g_img = {NULL,NULL,NULL,0,0,0,0,0,0,0};

void toggle_fullscreen()
{
  if(g_view.fullscreen) {
    SDL_SetWindowFullscreen(g_window, 0);
    g_view.fullscreen = 0;
  } else {
    SDL_SetWindowFullscreen(g_window, SDL_WINDOW_FULLSCREEN_DESKTOP);
    g_view.fullscreen = 1;
  }
}

void reset_view()
{
  g_view.scale = 1;
  g_view.x = 0;
  g_view.y = 0;
  g_view.redraw = 1;
}

void move_view(int x, int y)
{
  g_view.x += x;
  g_view.y += y;
  g_view.redraw = 1;
}

void zoom_view(int amount)
{
  g_view.scale += amount * 0.1;
  if(g_view.scale > 10)
    g_view.scale = 10;
  else if (g_view.scale < 0.1)
    g_view.scale = 0.1;
  g_view.redraw = 1;
}

void add_path(const char* path)
{
  struct loop_item_s *new_path =
    (struct loop_item_s*)malloc(sizeof(struct loop_item_s));
  new_path->path = path;
  if(!g_path.first && !g_path.last) {
    g_path.first = new_path;
    g_path.last = new_path;
    new_path->next = new_path;
    new_path->prev = new_path;
    g_path.cur = new_path;
  } else {
    g_path.last->next = new_path;
    new_path->prev = g_path.last;
    g_path.first->prev = new_path;
    new_path->next = g_path.first;
    g_path.last = new_path;
  }
}

void remove_current_path()
{
  if(g_path.cur->next == g_path.cur) {
    fprintf(stderr, "All input files closed. Exiting\n");
    exit(0);
  }

  struct loop_item_s* cur = g_path.cur;
  cur->next->prev = cur->prev;
  cur->prev->next = cur->next;
  if(g_path.dir > 0) {
    g_path.cur = cur->prev;
  } else {
    g_path.cur = cur->next;
  }
  g_path.changed = 1;
  free(cur);
}

void next_path()
{
  g_path.cur = g_path.cur->prev;
  g_path.changed = 1;
  g_path.dir = 1;
}

void prev_path()
{
  g_path.cur = g_path.cur->next;
  g_path.changed = 1;
  g_path.dir = -1;
}

void render_image(FIBITMAP *image)
{
  if(g_img.frame) {
    FreeImage_Unload(g_img.frame);
  }
  g_img.frame = FreeImage_ConvertTo32Bits(image);
  g_img.width = FreeImage_GetWidth(g_img.frame);
  g_img.height = FreeImage_GetHeight(g_img.frame);

  char* pixels = (char*)FreeImage_GetBits(g_img.frame);

  if(g_img.tex) {
    SDL_DestroyTexture(g_img.tex);
  }
  g_img.tex = SDL_CreateTexture(g_renderer,
        SDL_PIXELFORMAT_RGB888,
        SDL_TEXTUREACCESS_STATIC,
        g_img.width, g_img.height);
  SDL_Rect area = {0,0,g_img.width,g_img.height};
  SDL_UpdateTexture(g_img.tex, &area, pixels, 4 * g_img.width);
  g_view.redraw = 1;
}

void next_frame()
{
  if(g_img.num_frames < 2) {
    return;
  }
  FITAG *tag = NULL;
  char disposal_method = 0;
  int frame_time = 0;

  g_img.cur_frame = g_img.next_frame;
  g_img.next_frame = (g_img.cur_frame + 1) % g_img.num_frames;
  FIBITMAP *frame = FreeImage_LockPage(g_img.mbmp, g_img.cur_frame);
  FIBITMAP *frame32 = FreeImage_ConvertTo32Bits(frame);
  FreeImage_FlipVertical(frame32);

  //First frame is always going to use the raw frame
  if(g_img.cur_frame > 0) {
    FreeImage_GetMetadata(FIMD_ANIMATION, frame, "DisposalMethod", &tag);
    if(FreeImage_GetTagValue(tag)) {
      disposal_method = *(char*)FreeImage_GetTagValue(tag);
    }
  }

  FreeImage_GetMetadata(FIMD_ANIMATION, frame, "FrameTime", &tag);
  if(FreeImage_GetTagValue(tag)) {
    frame_time = *(int*)FreeImage_GetTagValue(tag);
  }

  g_img.frame_time += frame_time * 0.001;

  FreeImage_UnlockPage(g_img.mbmp, frame, 0);

  switch(disposal_method) {
    case 0: /*nothing specified, just use the raw frame*/
      render_image(frame32);
      break;
    case 1: /*composite over previous frame*/
      if(g_img.frame && g_img.cur_frame > 0) {
        FIBITMAP *bg_frame = FreeImage_ConvertTo24Bits(g_img.frame);
        FIBITMAP *comp = FreeImage_Composite(frame32, 1, NULL, bg_frame);
        FreeImage_Unload(bg_frame);
        render_image(comp);
        FreeImage_Unload(comp);
      } else {
        //No previous frame, just render directly
        render_image(frame32);
      }
      break;
    case 2: /*set to background, composite over that*/
      fprintf(stdout, "tried to set to bg\n");
      break;
    case 3: /*restore to previous content*/
      fprintf(stdout, "restore to content\n");
      break;
  }
  FreeImage_Unload(frame32);
}

void load_gif(const char* path)
{
  FIMULTIBITMAP *gif =
    FreeImage_OpenMultiBitmap(FIF_GIF, path,
        /* don't create */ 0,
        /* read only */ 1,
        /* keep in memory */ 1,
        /* flags */ GIF_LOAD256);

  if(!gif) {
    return;
  }

  g_img.mbmp = gif;
  g_img.num_frames = FreeImage_GetPageCount(gif);
  g_img.cur_frame = 0;
  g_img.next_frame = 0;
  g_img.frame_time = 0;
  g_img.playing = 1;

  next_frame();
}

void load_image(const char* path)
{
  if(g_img.mbmp) {
    FreeImage_CloseMultiBitmap(g_img.mbmp, 0);
    g_img.mbmp = NULL;
  }

  FREE_IMAGE_FORMAT fmt = FreeImage_GetFileType(path,0);
  if(fmt == FIF_GIF) {
    load_gif(path);
    return;
  }

  g_img.num_frames = 0;
  g_img.cur_frame = 0;
  g_img.next_frame = 0;
  g_img.frame_time = 0;
  g_img.playing = 0;

  FIBITMAP *image = FreeImage_Load(fmt, path, 0);
  FIBITMAP *frame = FreeImage_ConvertTo32Bits(image);
  FreeImage_FlipVertical(frame);
  render_image(frame);
  FreeImage_Unload(image);
}

int main(int argc, char** argv)
{
  if(argc < 2) {
    fprintf(stderr, "Usage: %s <image_file> <...>\n", argv[0]);
    exit(1);
  }

  if(argc == 2 && strcmp(argv[1], "-") == 0) {
    char buf[512];
    while(fgets(buf, sizeof(buf), stdin)) {
      size_t len = strlen(buf);
      if(buf[len-1] == '\n') {
        buf[--len] = 0;
      }
      if(len > 0) {
        char *str = (char*)malloc(len + 1);
        memcpy(str, buf, len + 1);
        add_path(str);
      }
    }
  } else {
    for(int i = 1; i < argc; ++i) {
      add_path(argv[i]);
    }
  }

  if(SDL_Init(SDL_INIT_VIDEO) != 0) {
    fprintf(stderr, "SDL Failed to Init: %s\n", SDL_GetError());
    exit(1);
  }

  const int width = 1280;
  const int height = 720;

  g_window = SDL_CreateWindow(
        "imv",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        width, height,
        SDL_WINDOW_RESIZABLE);

  g_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_ACCELERATED);

  //Use linear sampling for scaling
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");

  double lastTime = SDL_GetTicks() / 1000.0;

  int quit = 0;
  while(!quit) {
    double curTime = SDL_GetTicks() / 1000.0;
    double dt = curTime - lastTime;
    lastTime = curTime;

    SDL_Event e;
    while(!quit && SDL_PollEvent(&e)) {
      switch(e.type) {
        case SDL_QUIT:
          quit = 1;
          break;
        case SDL_KEYDOWN:
          switch (e.key.keysym.sym) {
            case SDLK_q:     quit = 1;              break;
            case SDLK_LEFTBRACKET:
            case SDLK_LEFT:  prev_path();           break;
            case SDLK_RIGHTBRACKET:
            case SDLK_RIGHT: next_path();           break;
            case SDLK_EQUALS:
            case SDLK_i:
            case SDLK_UP:    zoom_view(1);          break;
            case SDLK_MINUS:
            case SDLK_o:
            case SDLK_DOWN:  zoom_view(-1);         break;
            case SDLK_r:     reset_view();          break;
            case SDLK_j:     move_view(0, -50);     break;
            case SDLK_k:     move_view(0, 50);      break;
            case SDLK_h:     move_view(50, 0);      break;
            case SDLK_l:     move_view(-50, 0);     break;
            case SDLK_x:     remove_current_path(); break;
            case SDLK_f:     toggle_fullscreen();   break;
            case SDLK_PERIOD: next_frame();         break;
          }
          break;
        case SDL_MOUSEWHEEL:
          zoom_view(e.wheel.y);
          break;
        case SDL_MOUSEMOTION:
          if(e.motion.state & SDL_BUTTON_LMASK) {
            move_view(e.motion.xrel, e.motion.yrel);
          }
          break;
        case SDL_WINDOWEVENT:
          g_view.redraw = 1;
          break;
      }
    }

    if(quit) {
      break;
    }

    while(g_path.changed) {
      load_image(g_path.cur->path);
      if(g_img.tex == NULL) {
        fprintf(stderr, "Ignoring unsupported file: %s\n", g_path.cur->path);
        remove_current_path();
      } else {
        g_path.changed = 0;
        char title[128];
        snprintf(&title[0], sizeof(title), "imv - %s", g_path.cur->path);
        SDL_SetWindowTitle(g_window, (const char*)&title);
        reset_view();
      }
    }

    if(g_img.playing) {
      g_img.frame_time -= dt;

      while(g_img.frame_time < 0) {
        next_frame();
      }
    }

    if(g_view.redraw) {
      SDL_RenderClear(g_renderer);

      if(g_img.tex) {
        int img_w, img_h, img_access;
        unsigned int img_format;
        SDL_QueryTexture(g_img.tex, &img_format, &img_access, &img_w, &img_h);
        SDL_Rect g_view_area = {
          g_view.x,
          g_view.y,
          img_w * g_view.scale,
          img_h * g_view.scale
        };
        SDL_RenderCopy(g_renderer, g_img.tex, NULL, &g_view_area);
      }

      SDL_RenderPresent(g_renderer);
      g_view.redraw = 0;
    }
    SDL_Delay(10);
  }

  if(g_img.mbmp) {
    FreeImage_CloseMultiBitmap(g_img.mbmp, 0);
  }
  if(g_img.frame) {
    FreeImage_Unload(g_img.frame);
  }
  if(g_img.tex) {
    SDL_DestroyTexture(g_img.tex);
  }
  SDL_DestroyRenderer(g_renderer);
  SDL_DestroyWindow(g_window);
  SDL_Quit();

  return 0;
}
