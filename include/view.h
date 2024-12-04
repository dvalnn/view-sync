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

Result *next_view(view_t *previous, uint16_t gmid, view_act_t act);

#endif
