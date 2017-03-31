#include <stdio.h>
#include <string.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

#include "modules.h"
#include "utils.h"

static char *nw_system_uuid = NULL;

static int nw_system_start_acquire_data(nodewatcher_module_t *module) {

  char buffer[1024];
  json_object *object = json_object_new_object();

  /* UUID */
  if (nw_system_uuid == NULL) {
    FILE *uuid = fopen("/etc/uuid", "r");
    if (uuid) {
      fread(buffer, sizeof(char), sizeof(buffer), uuid);
      json_object_object_add(object, "uuid", json_object_new_string(nw_utils_string_trim(buffer)));
      fclose(uuid);
    }
  }
  else {
    json_object_object_add(object, "uuid", json_object_new_string(nw_system_uuid));
  }

  /* Hostname */
  gethostname(buffer, sizeof(buffer));
  json_object_object_add(object, "hostname", json_object_new_string(buffer));

  /* Kernel version */
  struct utsname uts;
  if (uname(&uts) >= 0) {
    json_object_object_add(object, "kernel", json_object_new_string(uts.release));
  }

  /* Local UNIX time */
  json_object_object_add(object, "local_time", json_object_new_int(time(NULL)));

  /* Uptime in seconds */
  FILE *uptime_file = fopen("/proc/uptime", "r");
  if (uptime_file) {
    long long int uptime;
    if (fscanf(uptime_file, "%lld", &uptime) == 1)
      json_object_object_add(object, "uptime", json_object_new_int(uptime));
    fclose(uptime_file);
  }

  /* Extract information from /proc/cpuinfo */
  json_object *hardware = json_object_new_object();
  FILE *cpuinfo_file = fopen("/proc/cpuinfo", "r");
  if (cpuinfo_file) {
    while (!feof(cpuinfo_file)) {
      char key[128];
      if (fscanf(cpuinfo_file, "%127[^:]%*c%1023[^\n]", key, buffer) == 2) {
        if (nw_utils_string_cmp(key, "machine") || nw_utils_string_cmp(key, "model name")) {
          json_object_object_add(hardware, "model", json_object_new_string(nw_utils_string_trim(buffer)));
          break;
        }
      }
    }
    fclose(cpuinfo_file);
  }
  json_object_object_add(object, "hardware", hardware);

  /* Store resulting JSON object. */
  return nw_module_finish_acquire_data(module, object);
}

static int nw_system_init(nodewatcher_module_t *module) {

  char c;

  while ((c = lu_getopt(module->args, "U:")) != EOF) {
    switch (c) {
      case 'U': nw_system_uuid = strdup(lu_getarg()); break;
    }
  }
  return 0;
}

/* Module descriptor */
MODULE_DESC = {
  .name = "core.general",
  .author = "",
  .version = 4,
  .hooks = {
     .init = nw_system_init,
     .start_acquire_data = nw_system_start_acquire_data
  },
  .schedule = {
    .refresh_interval = 30,
  }
};
