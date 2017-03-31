#include "modules.h"

static int nw_dummy_counter;

static int nw_dummy_start_acquire_data(nodewatcher_module_t *module) {

  json_object *object = json_object_new_object();

  /* Save the counter. */
  json_object_object_add(object, "value", json_object_new_int(nw_dummy_counter));
  nw_dummy_counter++;

  /* Store resulting JSON object. */
  return nw_module_finish_acquire_data(module, object);
}

static int nw_dummy_init(nodewatcher_module_t *module) {

  UNUSED(module);

  /* Init the counter. */
  nw_dummy_counter = 0;

  return 0;
}

/* Module descriptor. */
MODULE_DESC = {
  .name = "dummy.counter",
  .author = "jaka@live.jp",
  .version = 1,
  .hooks = {
    .init = nw_dummy_init,
    .start_acquire_data = nw_dummy_start_acquire_data,
  },
  .schedule = {
    .refresh_interval = 5,
  },
};
