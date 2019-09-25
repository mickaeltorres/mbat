#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

#include <machine/apmvar.h>

#include <xcb/xcb.h>

#include "mbat.h"

xcb_pixmap_t pix;

xcb_window_t create_win(xcb_connection_t *x, xcb_screen_t **scrn, int def_scr)
{
  xcb_intern_atom_reply_t *wm_rep;
  xcb_screen_iterator_t xsi;
  const xcb_setup_t *setup;
  xcb_window_t win;
  uint32_t val[5];
  int mask;
  int i;

  setup = xcb_get_setup(x);
  xsi = xcb_setup_roots_iterator(setup);

  for (i = 0; i < def_scr; i++)
    xcb_screen_next(&xsi);

  *scrn = xsi.data;

  win = xcb_generate_id(x);
  mask = XCB_CW_EVENT_MASK;
  i = XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_ENTER_WINDOW |
    XCB_EVENT_MASK_LEAVE_WINDOW;
  xcb_create_window(x, XCB_COPY_FROM_PARENT, win, (*scrn)->root, 0, -WIN_HEIGHT + 2,
		    (*scrn)->width_in_pixels, WIN_HEIGHT, 0,
		    XCB_WINDOW_CLASS_INPUT_OUTPUT,
		    (*scrn)->root_visual, mask, &i);

  i = 1;
  xcb_change_window_attributes(x, win, XCB_CW_OVERRIDE_REDIRECT, &i);

  wm_rep = xcb_intern_atom_reply(x,
				 xcb_intern_atom(x, 0, 15, "_MOTIF_WM_HINTS"),
				 NULL);
  val[0] = 2; // flags
  val[2] = 0; // decorations
  xcb_change_property(x, XCB_PROP_MODE_REPLACE, win, wm_rep->atom,
		      wm_rep->atom, 32, 5, val);
  free(wm_rep);

  pix = xcb_generate_id(x);
  xcb_create_pixmap(x, (*scrn)->root_depth, pix, (*scrn)->root, (*scrn)->width_in_pixels, WIN_HEIGHT);

  return win;
}

xcb_gcontext_t create_color(xcb_connection_t *x, xcb_screen_t *scrn, xcb_window_t win, uint16_t r, uint16_t g, uint16_t b)
{
  xcb_alloc_color_reply_t *crep;
  xcb_gcontext_t gc;
  int32_t val[3];

  crep = xcb_alloc_color_reply(x, xcb_alloc_color(x, scrn->default_colormap, r, g, b), NULL);
  if (crep == NULL)
  {
    printf("cannot allocate color\n");
    return EXIT_FAILURE;
  }

  gc = xcb_generate_id(x);
  val[0] = crep->pixel;
  val[1] = scrn->black_pixel;
  val[2] = 0;
  xcb_create_gc(x, gc, win, XCB_GC_FOREGROUND|XCB_GC_BACKGROUND|XCB_GC_GRAPHICS_EXPOSURES, val);

  return gc;
}

void win_move(xcb_connection_t *x, xcb_window_t win, int32_t X, int32_t Y)
{
  int32_t val[2];

  val[0] = X;
  val[1] = Y;
  xcb_configure_window(x, win, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y,
		       val);
  xcb_flush(x);
}

void win_event(xcb_connection_t *x, xcb_screen_t *scrn,  xcb_window_t win, xcb_gcontext_t green)
{
  xcb_generic_event_t *ev;

  ev = xcb_wait_for_event(x);
  switch (ev->response_type & ~0x80)
  {
  case XCB_EXPOSE:
    xcb_copy_area(x, pix, win, green, 0, 0, 0, 0, scrn->width_in_pixels, WIN_HEIGHT);
    xcb_flush(x);
    break;
  case XCB_ENTER_NOTIFY:
    win_move(x, win, 0, 0);
    break;
  case XCB_LEAVE_NOTIFY:
    win_move(x, win, 0, -WIN_HEIGHT + 2);
    break;
  }
}

void pixmap_update(xcb_connection_t *x, xcb_screen_t *scrn, xcb_window_t win,
		   xcb_gcontext_t red, xcb_gcontext_t green, xcb_gcontext_t blue,
		   xcb_gcontext_t black, xcb_gcontext_t txt,
		   uint32_t pct, uint32_t ac, char *str)
{
  xcb_rectangle_t bar;
  xcb_gcontext_t gc;

  bar.x = 0;
  bar.y = 0;
  bar.width = scrn->width_in_pixels;
  bar.height = WIN_HEIGHT;
  xcb_poly_fill_rectangle(x, pix, black, 1, &bar);

  if (ac == 1)
    gc = blue;
  else
  {
    if (pct > 10)
      gc = green;
    else
      gc = red;
  }

  bar.x = 0;
  bar.y = 0;
  bar.width = (scrn->width_in_pixels * pct) / 99;
  bar.height = WIN_HEIGHT;
  xcb_poly_fill_rectangle(x, pix, gc, 1, &bar);

  xcb_image_text_8(x, strlen(str), pix, txt, 10, 10, str);

  xcb_copy_area(x, pix, win, green, 0, 0, 0, 0, scrn->width_in_pixels, WIN_HEIGHT);

  xcb_flush(x);
}

void bat_get(int fd, bat_t *bat,
	     xcb_connection_t *x, xcb_screen_t *scrn, xcb_window_t win,
	     xcb_gcontext_t red, xcb_gcontext_t green, xcb_gcontext_t blue,
	     xcb_gcontext_t black, xcb_gcontext_t txt)
{
  struct apm_power_info api;
  char *str;

  bzero(bat, sizeof(*bat));
  if (ioctl(fd, APM_IOC_GETPOWER, &api) == -1)
  {
    printf("APM_IOC_GETPOWER error\n");
    exit(EXIT_FAILURE);
  }

  if (api.ac_state == 1)
    asprintf(&str, " %d%%, charging ", api.battery_life);
  else
    asprintf(&str, " %d%%, %d minutes left ", api.battery_life, api.minutes_left);
  pixmap_update(x, scrn, win, red, green, blue, black, txt, api.battery_life, api.ac_state == 1, str);
  free(str);
}

void apm_event(int fd, struct kevent *kev, bat_t *bat,
	       xcb_connection_t *x, xcb_screen_t *scrn, xcb_window_t win,
	       xcb_gcontext_t red, xcb_gcontext_t green, xcb_gcontext_t blue,
	       xcb_gcontext_t black, xcb_gcontext_t txt)
{
  switch (APM_EVENT_TYPE(kev->data))
  {
  case APM_UPDATE_TIME:
  case APM_BATTERY_LOW:
  case APM_POWER_CHANGE:
    bat_get(fd, bat, x, scrn, win, red, green, blue, black, txt);
    break;
  }
}

int main(int ac, char **av)
{
  xcb_gcontext_t black;
  xcb_gcontext_t green;
  xcb_gcontext_t blue;
  xcb_gcontext_t red;
  xcb_gcontext_t txt;
  xcb_connection_t *x;
  xcb_screen_t *scrn;
  struct kevent kev;
  xcb_window_t win;
  int def_scr;
  bat_t bat;
  int fdx;
  int fd;
  int kq;

  x = xcb_connect(NULL, &def_scr);
  if (x == NULL)
  {
    printf("cannot open DISPLAY\n");
    return EXIT_FAILURE;
  }

  fd = open("/dev/apm", O_RDONLY);
  if (fd == -1)
  {
    printf("cannot open /dev/apm\n");
    return EXIT_FAILURE;
  }

  switch (fork())
  {
  case -1:
    printf("cannot fork()\n");
    return EXIT_FAILURE;
  case 0:
    break;
  default:
    return EXIT_SUCCESS;
  }

  win = create_win(x, &scrn, def_scr);
  black = create_color(x, scrn, win, 0, 0, 0);
  red = create_color(x, scrn, win, 65535, 0, 0);
  green = create_color(x, scrn, win, 0, 65535, 0);
  blue = create_color(x, scrn, win, 0, 0, 65535);
  txt = create_color(x, scrn, win, 65535, 65535, 65535);
  bat_get(fd, &bat, x, scrn, win, red, green, blue, black, txt);
  xcb_map_window(x, win);
  xcb_flush(x);
  fdx = xcb_get_file_descriptor(x);

  kq = kqueue();
  if (kq == -1)
  {
    printf("cannot create kqueue\n");
    return EXIT_FAILURE;
  }

  EV_SET(&kev, fdx, EVFILT_READ, EV_ADD, 0, 0, 0);
  if (kevent(kq, &kev, 1, NULL, 0, NULL) == -1)
  {
    printf("cannot add X fd to kqueue\n");
    return EXIT_FAILURE;
  }
  EV_SET(&kev, fd, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, 0);
  if (kevent(kq, &kev, 1, NULL, 0, NULL) == -1)
  {
    printf("cannot add apm fd to kqueue\n");
    return EXIT_FAILURE;
  }

  for (;;)
  {
    if (kevent(kq, NULL, 0, &kev, 1, NULL) == 1)
    {
      if (kev.ident == fdx)
	win_event(x, scrn, win, green);
      else if (kev.ident == fd)
	apm_event(fd, &kev, &bat, x, scrn, win, red, green, blue, black, txt);
    }
  }

  xcb_disconnect(x);

  return EXIT_SUCCESS;
}
