/*
 * nodewatcher-agent - remote monitoring daemon
 *
 * Copyright (C) 2015 Jernej Kos <jernej@kos.mx>
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Affero General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Affero General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <dirent.h>
#include <libre/scheduler.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "node-agent.h"
#include "modules.h"

static nodewatcher_module_node_t *module_list = NULL;

static void nw_module_run_module(void *arg) {

  nodewatcher_module_t *module = (nodewatcher_module_t *)arg;
  nw_module_start_acquire_data(module);
}

static int nw_module_schedule(nodewatcher_module_t *module) {

  time_t timeout;

  if (module->sched_status == NW_MODULE_PENDING_DATA || module->sched_status == NW_MODULE_SCHEDULED)
    return -1;

  /* If the module has just been initialized, we schedule it for immediate execution. */
  timeout = module->sched_status == NW_MODULE_INIT ? 0 : module->schedule.refresh_interval;

  /* Schedule the module. */
  lu_task_insert(timeout, nw_module_run_module, (void *)module);
  module->sched_status = NW_MODULE_SCHEDULED;

  return 0;
}

static int nw_module_add(nodewatcher_module_node_t **node, nodewatcher_module_t *module) {

  int ret = 0;

  /* Initialize module data object. */
  module->data = json_object_new_object();
  json_object *meta = json_object_new_object();
  json_object_object_add(meta, "version", json_object_new_int(module->version));
  json_object_object_add(module->data, "_meta", meta);

  /* Perform module initialization. */
  module->sched_status = NW_MODULE_INIT;
  syslog(LOG_INFO, "Initializing module '%s'.", module->name);
  ret = module->hooks.init(module);

  if (ret)
    return ret;

  /* Create new node. */
  nodewatcher_module_node_t *new_node = malloc(sizeof(nodewatcher_module_node_t));
  new_node->module = module;
  new_node->next = *node;
  *node = new_node;

  if (module->schedule.refresh_interval)
    ret = nw_module_schedule(module);

  return ret;
}

static int nw_module_load_library(const char *path, const lu_args *args) {

  nodewatcher_module_t *module;
  void *handle;
  int ret = 0;

  /* Load library. */
  handle = dlopen(path, RTLD_LAZY | RTLD_GLOBAL);
  if (!handle) {
    syslog(LOG_WARNING, "Unable to open module '%s'!", path);
    return -1;
  }

  /* Load module. */
  module = dlsym(handle, "nw_module");
  if (!module) {
    syslog(LOG_WARNING, "Module '%s' is not a valid %s module!", path, APP);
    return -1;
  }

  module->args = args;

  /* Add module to our list of modules. */
  ret = nw_module_add(&module_list, module);
  if (ret)
    syslog(LOG_WARNING, "Loading of module '%s' (%s) has failed!", module->name, path);
  else
    syslog(LOG_INFO, "Loaded module '%s' (%s).", module->name, path);

  return ret;
}

static inline unsigned int file_ext(char *name, char *ext) {
  size_t len_filename = strlen(name);
  size_t len_ext = strlen(ext);
  return (len_filename > len_ext) && !strcmp(name + len_filename - len_ext, ext);
}

int nw_module_init(const lu_args *args) {

  char c;
  DIR *dir;
  struct stat s;
  struct dirent *dir_entry;
  char path[PATH_MAX];
  char *moddir = NULL;
  int ret = 0;

  while ((c = lu_getopt(args, "m:")) != EOF) {
    switch (c) {
      case 'm': moddir = strdup(lu_getarg()); break;
    }
  }

  if (!moddir) {
    syslog(LOG_INFO, "Using default directory for modules.");
    moddir = strdup(NA_MODULE_DIRECTORY);
  }

  /* Discover and initialize all the modules. */
  dir = opendir(moddir);
  if (!dir) {
    syslog(LOG_INFO, "Could not open module directory '%s'.", moddir);
    return -1;
  }

  syslog(LOG_INFO, "Loading modules from '%s'.", moddir);
  while ((dir_entry = readdir(dir)) != NULL) {
    if (dir_entry->d_type != DT_REG || !file_ext(dir_entry->d_name, "so"))
      continue;
    snprintf(path, sizeof(path)-1, "%s/%s", moddir, dir_entry->d_name);
    if (stat(path, &s) || !S_ISREG(s.st_mode))
      continue;
    ret |= nw_module_load_library(path, args);
  }
  closedir(dir);

  return ret;
}

int nw_module_start_acquire_data(nodewatcher_module_t *module) {

  module->hooks.start_acquire_data(module);
  return 0;
}

int nw_module_finish_acquire_data(nodewatcher_module_t *module, json_object *object) {

  if (!object)
    return -1;

  /* Copy metadata from old data to new data. */
  json_object *meta;
  json_object_object_get_ex(module->data, "_meta", &meta);
  json_object_object_add(object, "_meta", json_object_get(meta));

  /* Dump old data and move new data to module. */
  json_object_put(module->data);
  module->data = object;

  /* Reschedule module. */
  module->sched_status = NW_MODULE_NONE;
  nw_module_schedule(module);

  return 0;
}

json_object *nw_module_get_output() {

  nodewatcher_module_t *module;
  nodewatcher_module_node_t *node = module_list;

  json_object *object = json_object_new_object();

  /* Iterate through all modules and add content. */
  while (node) {
    module = node->module;
    json_object_object_add(object, module->name, json_object_get(module->data));
    node = node->next;
  }

  return object;
}
