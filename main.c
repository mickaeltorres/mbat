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

mbat_t mbat;

void create_win(void)
{
  xcb_intern_atom_reply_t *wm_rep;
  xcb_screen_iterator_t xsi;
  const xcb_setup_t *setup;
  uint32_t val[5];
  int mask;
  int i;

  setup = xcb_get_setup(mbat.x);
  xsi = xcb_setup_roots_iterator(setup);

  for (i = 0; i < mbat.def_scr; i++)
    xcb_screen_next(&xsi);

  mbat.scrn = xsi.data;

  mbat.win = xcb_generate_id(mbat.x);
  mask = XCB_CW_EVENT_MASK;
  val[0] = XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_ENTER_WINDOW |
    XCB_EVENT_MASK_LEAVE_WINDOW;
  xcb_create_window(mbat.x, XCB_COPY_FROM_PARENT, mbat.win, mbat.scrn->root, 0,
		    -WIN_HEIGHT + 2,
		    mbat.scrn->width_in_pixels, WIN_HEIGHT, 0,
		    XCB_WINDOW_CLASS_INPUT_OUTPUT,
		    mbat.scrn->root_visual, mask, val);

  val[0] = 1;
  xcb_change_window_attributes(mbat.x, mbat.win, XCB_CW_OVERRIDE_REDIRECT, val);

  wm_rep = xcb_intern_atom_reply(mbat.x,
				 xcb_intern_atom(mbat.x, 0, 15,
						 "_MOTIF_WM_HINTS"),
				 NULL);
  val[0] = 2; // flags
  val[2] = 0; // decorations
  xcb_change_property(mbat.x, XCB_PROP_MODE_REPLACE, mbat.win, wm_rep->atom,
		      wm_rep->atom, 32, 5, val);
  free(wm_rep);

  mbat.pix = xcb_generate_id(mbat.x);
  xcb_create_pixmap(mbat.x, mbat.scrn->root_depth, mbat.pix, mbat.scrn->root,
		    mbat.scrn->width_in_pixels, WIN_HEIGHT);
}

xcb_gcontext_t create_color(uint16_t r, uint16_t g, uint16_t b)
{
  xcb_alloc_color_reply_t *crep;
  xcb_gcontext_t gc;
  int32_t val[3];

  crep = xcb_alloc_color_reply(mbat.x,
			       xcb_alloc_color(mbat.x,
					       mbat.scrn->default_colormap,
					       r, g, b),
			       NULL);
  if (crep == NULL)
  {
    printf("cannot allocate color\n");
    return EXIT_FAILURE;
  }

  gc = xcb_generate_id(mbat.x);
  val[0] = crep->pixel;
  val[1] = mbat.scrn->black_pixel;
  val[2] = 0;
  xcb_create_gc(mbat.x, gc, mbat.win,
		XCB_GC_FOREGROUND|XCB_GC_BACKGROUND|XCB_GC_GRAPHICS_EXPOSURES,
		val);

  return gc;
}

void win_move(int32_t X, int32_t Y)
{
  int32_t val[2];

  val[0] = X;
  val[1] = Y;
  xcb_configure_window(mbat.x, mbat.win,
		       XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y,
		       val);
  xcb_flush(mbat.x);
}

void win_event(void)
{
  xcb_generic_event_t *ev;

  ev = xcb_wait_for_event(mbat.x);
  switch (ev->response_type & ~0x80)
  {
  case XCB_EXPOSE:
    xcb_copy_area(mbat.x, mbat.pix, mbat.win, mbat.green, 0, 0, 0, 0,
		  mbat.scrn->width_in_pixels, WIN_HEIGHT);
    xcb_flush(mbat.x);
    break;
  case XCB_ENTER_NOTIFY:
    win_move(0, 0);
    break;
  case XCB_LEAVE_NOTIFY:
    win_move(0, -WIN_HEIGHT + 2);
    break;
  }
}

void pixmap_update(uint32_t pct, uint32_t ac, char *str)
{
  xcb_rectangle_t bar;
  xcb_gcontext_t gc;

  bar.x = 0;
  bar.y = 0;
  bar.width = mbat.scrn->width_in_pixels;
  bar.height = WIN_HEIGHT;
  xcb_poly_fill_rectangle(mbat.x, mbat.pix, mbat.black, 1, &bar);

  if (ac == 1)
    gc = mbat.blue;
  else
  {
    if (pct > 10)
      gc = mbat.green;
    else
      gc = mbat.red;
  }

  bar.x = 0;
  bar.y = 0;
  bar.width = (mbat.scrn->width_in_pixels * pct) / 99;
  bar.height = WIN_HEIGHT;
  xcb_poly_fill_rectangle(mbat.x, mbat.pix, gc, 1, &bar);

  xcb_image_text_8(mbat.x, strlen(str), mbat.pix, gc, 10, 9, str);

  xcb_copy_area(mbat.x, mbat.pix, mbat.win, mbat.green,
		0, 0, 0, 0, mbat.scrn->width_in_pixels, WIN_HEIGHT);

  xcb_flush(mbat.x);
}

void bat_get(int fd)
{
  struct apm_power_info api;
  struct tm *now;
  time_t epoch;
  char *str;

  if (ioctl(fd, APM_IOC_GETPOWER, &api) == -1)
  {
    printf("APM_IOC_GETPOWER error\n");
    exit(EXIT_FAILURE);
  }

  epoch = time(NULL);
  now = localtime(&epoch);
  if (api.ac_state == 1)
    asprintf(&str, " %02d:%02d | %d%%, charging ", now->tm_hour, now->tm_min, api.battery_life);
  else
    asprintf(&str, " %02d:%02d | %d%%, %d minutes left ", now->tm_hour, now->tm_min, api.battery_life, api.minutes_left);
  pixmap_update(api.battery_life, api.ac_state == 1, str);
  free(str);
}

void apm_event(int fd, struct kevent *kev)
{
  switch (APM_EVENT_TYPE(kev->data))
  {
  case APM_UPDATE_TIME:
  case APM_BATTERY_LOW:
  case APM_POWER_CHANGE:
    bat_get(fd);
    break;
  }
}

int main(int ac, char **av)
{
  struct timespec ts;
  struct kevent kev;
  int fdx;
  int ret;
  int fd;
  int kq;

  mbat.x = xcb_connect(NULL, &(mbat.def_scr));
  if (mbat.x == NULL)
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

  create_win();
  mbat.black = create_color(0, 0, 0);
  mbat.red = create_color(65535, 0, 0);
  mbat.green = create_color(0, 42000, 0);
  mbat.blue = create_color(12000, 23000, 45535);
  bat_get(fd);
  xcb_map_window(mbat.x, mbat.win);
  xcb_flush(mbat.x);
  fdx = xcb_get_file_descriptor(mbat.x);

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

  ts.tv_sec = 5;
  ts.tv_nsec = 0;

  for (;;)
  {
    ret = kevent(kq, NULL, 0, &kev, 1, &ts);
    if (ret == 1)
    {
      if (kev.ident == fdx)
	win_event();
      else if (kev.ident == fd)
	apm_event(fd, &kev);
    }
    else if (ret == 0)
      bat_get(fd);
  }

  xcb_disconnect(mbat.x);

  return EXIT_SUCCESS;
}
