#include "modules.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char *nw_fileoutput_filename = NULL;

static int nw_fileoutput_start_acquire_data(nodewatcher_module_t *module) {

  json_object *object = json_object_new_object();

  if (nw_fileoutput_filename) {

    json_object *data = nw_module_get_output();
    mode_t pmask = umask(0022);

    /* Export JSON to configured output file. */
    FILE *file = fopen(nw_fileoutput_filename, "w");
    if (file) {
      fprintf(file, "%s\n", json_object_to_json_string(data));
      fclose(file);
    }

    /* Restore umask. */
    umask(pmask);

    json_object_put(data);
  }

  /* Store resulting JSON object. */
  return nw_module_finish_acquire_data(module, object);
}

static int nw_fileoutput_init(nodewatcher_module_t *module) {

  char c;

  while ((c = lu_getopt(module->args, "f:")) != EOF) {
    switch (c) {
      case 'f':
        if (nw_fileoutput_filename)
          free(nw_fileoutput_filename);
        nw_fileoutput_filename = strdup(lu_getarg());
        break;
    }
  }

  if (!nw_fileoutput_filename) {
    syslog(LOG_ERR, "Module %s: Output filename is missing!", module->name);
    return -1;
  }

  syslog(LOG_INFO, "Module %s: Output filename set to '%s'.", module->name, nw_fileoutput_filename);

  return 0;
}

/* Module descriptor. */
MODULE_DESC = {
  .name = "core.fileoutput",
  .author = "jaka@live.jp",
  .version = 1,
  .hooks = {
    .init = nw_fileoutput_init,
    .start_acquire_data = nw_fileoutput_start_acquire_data,
  },
  .schedule = {
    .refresh_interval = 60,
  },
};
