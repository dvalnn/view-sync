#ifndef _VIEW_H_
#define _VIEW_H_

#include <stdint.h>

#include "lib/stb_ds.h"

#include "result.h"

struct View {
  uint16_t id;
  uint16_t *gm_ids;
};
typedef struct View view_t;

enum ViewAct {
  V_DROP,
  V_ADD,
};
typedef enum ViewAct view_act_t;

struct ViewChange {
  uint16_t gm_id;
  view_act_t action;
};
typedef struct ViewChange view_change_t;

Result *next_view(view_t *previous, view_change_t *changes);

#endif
