#include <stdio.h>
#include <dirent.h>
#include <string.h>
#include <math.h>
#include "modules.h"
#include "utils.h"

static int nw_resources_start_acquire_data(nodewatcher_module_t *module) {

  json_object *object = json_object_new_object();

  /* Load average */
  FILE *loadavg_file = fopen("/proc/loadavg", "r");
  if (loadavg_file) {
    char load1min[16], load5min[16], load15min[16];
    if (fscanf(loadavg_file, "%15s %15s %15s", load1min, load5min, load15min) == 3) {
      json_object *load_average = json_object_new_array();
      json_object_array_add(load_average, json_object_new_string(load1min));
      json_object_array_add(load_average, json_object_new_string(load5min));
      json_object_array_add(load_average, json_object_new_string(load15min));
      json_object_object_add(object, "load_average", load_average);
    }
    fclose(loadavg_file);
  }

  /* Memory usage counters */
  FILE *memory_file = fopen("/proc/meminfo", "r");
  if (memory_file) {
    json_object *memory = json_object_new_object();
    while (!feof(memory_file)) {
      char key[128];
      int value;

      if (fscanf(memory_file, "%127[^:]%*c%d kB", key, &value) == 2) {
        if (nw_utils_string_cmp(key, "MemTotal")) {
          json_object_object_add(memory, "total", json_object_new_int(value));
        } else if (nw_utils_string_cmp(key, "MemFree")) {
          json_object_object_add(memory, "free", json_object_new_int(value));
        } else if (nw_utils_string_cmp(key, "Buffers")) {
          json_object_object_add(memory, "buffers", json_object_new_int(value));
        } else if (nw_utils_string_cmp(key, "Cached")) {
          json_object_object_add(memory, "cache", json_object_new_int(value));
          /* We can break as we don't need entries after "cache" */
          break;
        }
      }
    }
    fclose(memory_file);
    json_object_object_add(object, "memory", memory);
  }

  /* Number of local TCP/UDP connections */
  json_object *connections = json_object_new_object();
  json_object *connections_ipv4 = json_object_new_object();
  json_object_object_add(connections_ipv4, "tcp", json_object_new_int(nw_file_line_count("/proc/net/tcp") - 1));
  json_object_object_add(connections_ipv4, "udp", json_object_new_int(nw_file_line_count("/proc/net/udp") - 1));
  json_object_object_add(connections, "ipv4", connections_ipv4);
  json_object *connections_ipv6 = json_object_new_object();
  json_object_object_add(connections_ipv6, "tcp", json_object_new_int(nw_file_line_count("/proc/net/tcp6") - 1));
  json_object_object_add(connections_ipv6, "udp", json_object_new_int(nw_file_line_count("/proc/net/udp6") - 1));
  json_object_object_add(connections, "ipv6", connections_ipv6);
  /* Number of entries in connection tracking table */
  json_object *connections_tracking = json_object_new_object();
  nw_json_from_file("/proc/sys/net/netfilter/nf_conntrack_count", connections_tracking, "count", 1);
  nw_json_from_file("/proc/sys/net/netfilter/nf_conntrack_max", connections_tracking, "max", 1);
  json_object_object_add(connections, "tracking", connections_tracking);
  json_object_object_add(object, "connections", connections);

  /* Number of processes by status */
  DIR *proc_dir;
  struct dirent *proc_entry;
  char path[PATH_MAX];

  proc_dir = opendir("/proc");
  if (proc_dir) {
    json_object *processes = json_object_new_object();
    int proc_by_state[6] = {0};

    while ((proc_entry = readdir(proc_dir)) != NULL) {

      snprintf(path, sizeof(path) - 1, "/proc/%s/stat", proc_entry->d_name);

      FILE *proc_file = fopen(path, "r");
      if (proc_file) {
        char state;
        if (fscanf(proc_file, "%*d (%*[^)]) %c", &state) == 1) {
          switch (state) {
            case 'R': proc_by_state[0]++; break;
            case 'S': proc_by_state[1]++; break;
            case 'D': proc_by_state[2]++; break;
            case 'Z': proc_by_state[3]++; break;
            case 'T': proc_by_state[4]++; break;
            case 'W': proc_by_state[5]++; break;
          }
        }
        fclose(proc_file);
      }
    }

    closedir(proc_dir);
    json_object_object_add(processes, "running", json_object_new_int(proc_by_state[0]));
    json_object_object_add(processes, "sleeping", json_object_new_int(proc_by_state[1]));
    json_object_object_add(processes, "blocked", json_object_new_int(proc_by_state[2]));
    json_object_object_add(processes, "zombie", json_object_new_int(proc_by_state[3]));
    json_object_object_add(processes, "stopped", json_object_new_int(proc_by_state[4]));
    json_object_object_add(processes, "paging", json_object_new_int(proc_by_state[5]));
    json_object_object_add(object, "processes", processes);
  }

  /* CPU usage by category */
  unsigned int cpu_times[7] = {0};

  json_object *cpu = json_object_new_object();
  json_object_object_add(cpu, "user", json_object_new_int(cpu_times[0]));
  json_object_object_add(cpu, "system", json_object_new_int(cpu_times[1]));
  json_object_object_add(cpu, "nice", json_object_new_int(cpu_times[2]));
  json_object_object_add(cpu, "idle", json_object_new_int(cpu_times[3]));
  json_object_object_add(cpu, "iowait", json_object_new_int(cpu_times[4]));
  json_object_object_add(cpu, "irq", json_object_new_int(cpu_times[5]));
  json_object_object_add(cpu, "softirq", json_object_new_int(cpu_times[6]));
  json_object_object_add(object, "cpu", cpu);

  /* Store resulting JSON object */
  return nw_module_finish_acquire_data(module, object);
}

static int nw_resources_init(nodewatcher_module_t *module) {

  UNUSED(module);
  return 0;
}

/* Module descriptor. */
MODULE_DESC = {
  .name = "core.resources",
  .author = "",
  .version = 2,
  .hooks = {
    .init = nw_resources_init,
    .start_acquire_data = nw_resources_start_acquire_data,
  },
  .schedule = {
    .refresh_interval = 30,
  },
};
