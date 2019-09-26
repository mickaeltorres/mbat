#ifndef MBAT_H
#define MBAT_H

#define WIN_HEIGHT 16

#define BAT_STR_MAX 128

typedef struct
{
  xcb_connection_t *x;
  xcb_screen_t *scrn;
  xcb_window_t win;
  xcb_gcontext_t red;
  xcb_gcontext_t green;
  xcb_gcontext_t blue;
  xcb_gcontext_t black;
  xcb_gcontext_t txt;
  xcb_pixmap_t pix;
  int def_scr;
} mbat_t;

#endif
