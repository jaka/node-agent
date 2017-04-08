#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "modules.h"

#define DEFAULT_DHCPLEASES_FILENAME "dhcp.leases"

static char *nw_dhcpleases_filename = NULL;

static int nw_dhcpleases_start_acquire_data(nodewatcher_module_t *module) {

  int client_uuid = 1;
  char client_uuid_string[10];

  if (!nw_dhcpleases_filename)
    return 0;

  json_object *object = json_object_new_object();

  /* Iterate over DHCP leases. */
  FILE *leases_file = fopen(nw_dhcpleases_filename, "r");

  if (leases_file) {

    while (!feof(leases_file)) {
      unsigned int expiry;
      char mac[18];
      char ip_address[46];
      char hostname[65];

      if (fscanf(leases_file, "%u %17s %45s %64s %*[^\n]\n", &expiry, mac, ip_address, hostname) >= 3) {

        json_object *client = json_object_new_object();
        json_object *addresses = json_object_new_array();
        json_object *address = json_object_new_object();
        json_object_object_add(address, "family", json_object_new_string("ipv4"));
        json_object_object_add(address, "address", json_object_new_string(ip_address));
        json_object_object_add(address, "expires", json_object_new_int(expiry));
        json_object_array_add(addresses, address);
        json_object_object_add(client, "addresses", addresses);

        snprintf(client_uuid_string, 10, "%d", client_uuid);
        json_object_object_add(object, client_uuid_string, client);
        client_uuid++;

      }

    }

    fclose(leases_file);
  }

  /* Store resulting JSON object. */
  return nw_module_finish_acquire_data(module, object);
}

static int nw_dhcpleases_init(nodewatcher_module_t *module) {

  char c;
  struct stat s;

  while ((c = lu_getopt(module->args, "l:")) != EOF) {
    switch (c) {
      case 'l': nw_dhcpleases_filename = strdup(lu_getarg()); break;
    }
  }

  if (!nw_dhcpleases_filename)
    nw_dhcpleases_filename = strdup(DEFAULT_DHCPLEASES_FILENAME);

  if (stat(nw_dhcpleases_filename, &s) < 0) {
    syslog(LOG_ERR, "Module %s: Could not find dhcplease file '%s'!", module->name, nw_dhcpleases_filename);
    return -1;
  }
  if (!S_ISREG(s.st_mode)) {
    syslog(LOG_ERR, "Module %s: '%s' is not a regular file!", module->name, nw_dhcpleases_filename);
    return -1;
  }

  syslog(LOG_INFO, "Module %s: Reading leases from '%s'.", module->name, nw_dhcpleases_filename);

  return 0;
}

/* Module descriptor. */
MODULE_DESC = {
  .name = "core.clients",
  .author = "Jernej Kos <jernej@kos.mx>",
  .version = 1,
  .hooks = {
    .init = nw_dhcpleases_init,
    .start_acquire_data = nw_dhcpleases_start_acquire_data
  },
  .schedule = {
    .refresh_interval = 30,
  },
};
