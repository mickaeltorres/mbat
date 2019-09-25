#ifndef MBAT_H
#define MBAT_H

#define WIN_HEIGHT 16

#define BAT_STR_MAX 128

typedef struct
{
  uint32_t pct;
  uint32_t power;
  char str[BAT_STR_MAX];
} bat_t;

#endif
