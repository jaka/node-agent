#ifndef NODEWATCHER_MODULES_H
#define NODEWATCHER_MODULES_H

#include <dlfcn.h>
#include <json-c/json.h>
#include <libre/config.h>
#include <syslog.h>
#include <time.h>

#define UNUSED(x) (void)(x)
#define MODULE_DESC nodewatcher_module_t nw_module __attribute__((visibility("default")))

enum {
  NW_MODULE_NONE = 0,
  NW_MODULE_SCHEDULED = 1,
  NW_MODULE_PENDING_DATA = 2,
  NW_MODULE_INIT = 3,
};

typedef struct {
  time_t refresh_interval;
} nodewatcher_module_schedule_t;

typedef struct nodewatcher_module nodewatcher_module_t;

typedef struct {
  int (*init)(nodewatcher_module_t *module);
  int (*start_acquire_data)(nodewatcher_module_t *module);
} nodewatcher_module_hooks_t;

typedef struct nodewatcher_module {
  const char *name;
  const char *author;
  const unsigned int version;
  const nodewatcher_module_hooks_t hooks;
  nodewatcher_module_schedule_t schedule;
  const lu_args *args;
  json_object *data;
  int sched_status;
} nodewatcher_module_t;

typedef struct nodewatcher_module_node {
  nodewatcher_module_t *module;
  struct nodewatcher_module_node *next;
} nodewatcher_module_node_t;

int nw_module_init(const lu_args *);
int nw_module_start_acquire_data(nodewatcher_module_t *module);
int nw_module_finish_acquire_data(nodewatcher_module_t *module, json_object *object);
json_object *nw_module_get_output();

#endif
