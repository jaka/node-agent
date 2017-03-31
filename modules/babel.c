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

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <libre/scheduler.h>
#include <libre/stream.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <syslog.h>
#include <unistd.h>

#include "modules.h"
#include "utils.h"

enum babel_info_type {
  none,
  self,
  neighbour,
  xroute,
  route
};

#define INFO_SELF_NAME "self"
#define INFO_NEIGHBOUR_NAME "neighbour"
#define INFO_XROUTE_NAME "xroute"
#define INFO_ROUTE_NAME "route"

struct nw_babel_client_s {
  int fd;
  lu_fdn_t *fdn;
  lu_stream_t *stream;
  json_object *object;
  nodewatcher_module_t *module;
};

static struct nw_babel_client_s bc;

static json_object *nw_routing_babel_add_array_item(json_object *object, const char *key) {

  /* Get the existing list or create a new one. */
  json_object *list;
  json_object_object_get_ex(object, key, &list);
  if (!list) {
    list = json_object_new_array();
    json_object_object_add(object, key, list);
  }

  /* Create a new array entry. */
  json_object *item = json_object_new_object();
  json_object_array_add(list, item);

  return item;
}

static void nw_routing_babel_close(struct nw_babel_client_s *bc) {

  lu_fd_del(bc->fdn);
  lu_task_remove((void *)bc);

  lu_stream_destroy(bc->stream);
  bc->stream = NULL;

  close(bc->fd);
  bc->fd = -1;

  nw_module_finish_acquire_data(bc->module, bc->object);

  bc->object = NULL;
}

static void nw_routing_babel_recv(void *arg) {

  char *type, *info_type, *info_id, *key, *value;
  enum babel_info_type info;
  json_object *item;
  char line[1024];
  int rv;
  struct nw_babel_client_s *bc = (struct nw_babel_client_s *)arg;
  json_object *object = bc->object;

  if (bc->stream == NULL)
    return;

  lu_stream_readin_fd(bc->stream, bc->fd);

  for(;;) {

    rv = lu_stream_readline(bc->stream, line, sizeof(line));

    /*

    Sample lines:

    add self zeds id c0:56:b0:c1:11:17:e7:cf
    add xroute 10.254.234.2/32-::/0 prefix 10.254.234.2/32 from ::/0 metric 0

    */

    if (rv < 0)
      break;

    /* Tokenize by spaces. */
    type = strtok(line, " ");

    if (!strcmp(type, "BABEL")) {
      /* Header. */
    }
    else if (!strcmp(type, "add")) {
      /* Information. */
      info_type = strtok(NULL, " ");
      info_id = strtok(NULL, " ");
      UNUSED(info_id);

      info = none;
      item = NULL;

      if (!strcmp(info_type, INFO_SELF_NAME)) {
        /* Router ID. */
        info = self;
      }
      else if (!strcmp(info_type, INFO_NEIGHBOUR_NAME)) {
        /* Neighbours. */
        info = neighbour;
        item = nw_routing_babel_add_array_item(object, "neighbours");
      }
      else if (!strcmp(info_type, INFO_XROUTE_NAME)) {
        /* Exported routes. */
        info = xroute;
        item = nw_routing_babel_add_array_item(object, "exported_routes");
      }
      else if (!strcmp(info_type, INFO_ROUTE_NAME)) {
        /* Imported routes. */
        info = route;
      }

      for (;;) {
        key = strtok(NULL, " ");
        value = strtok(NULL, " ");
        if (!key || !value)
          break;

        switch (info) {

          case none:
            break;

          case self:
            if (!strcmp(key, "id")) {
              /* Router identifier. */
              json_object_object_add(object, "router_id", json_object_new_string(value));
            }
            break;

          case neighbour:
            if (!strcmp(key, "address")) {
              /* Link-local address of the neighbour. */
              json_object_object_add(item, "address", json_object_new_string(value));
            }
            else if (!strcmp(key, "if")) {
              /* Neighbour interface. */
              json_object_object_add(item, "interface", json_object_new_string(value));
            }
            else if (!strcmp(key, "reach")) {
              /* Neighbour reachability. */
              json_object_object_add(item, "reachability", json_object_new_int(strtol(value, NULL, 16)));
            }
            else if (!strcmp(key, "rxcost")) {
              /* Neighbour RX cost. */
              json_object_object_add(item, "rxcost", json_object_new_int(atoi(value)));
            }
            else if (!strcmp(key, "txcost")) {
              /* Neighbour TX cost. */
              json_object_object_add(item, "txcost", json_object_new_int(atoi(value)));
            }
            else if (!strcmp(key, "rtt")) {
              /* Neighbour RTT. */
              unsigned int thousands, rest;
              if (sscanf(value, "%d.%d", &thousands, &rest) == 2)
                json_object_object_add(item, "rtt", json_object_new_int(thousands * 1000 + rest));
            }
            else if (!strcmp(key, "rttcost")) {
              /* Neighbour RTT cost. */
              json_object_object_add(item, "rttcost", json_object_new_int(atoi(value)));
            }
            else if (!strcmp(key, "cost")) {
              /* Neighbour cost. */
              json_object_object_add(item, "cost", json_object_new_int(atoi(value)));
            }
            break;

          case xroute:
            if (!strcmp(key, "prefix")) {
              /* Advertised destination prefix. */
              json_object_object_add(item, "dst_prefix", json_object_new_string(value));
            }
            else if (!strcmp(key, "from")) {
              /* Advertised source prefix. */
              json_object_object_add(item, "src_prefix", json_object_new_string(value));
            }
            else if (!strcmp(key, "metric")) {
              /* Advertised metric. */
              json_object_object_add(item, "metric", json_object_new_int(atoi(value)));
            }
            break;

          case route:
            /* Currently we do not report imported routes. */
            break;

        }
      }
    }
    else if (!strcmp(type, "done")) {
      /* Finished. */
      return nw_routing_babel_close(bc);
    }

  }

}

static void nw_routing_babel_timeout(void *arg) {

  struct nw_babel_client_s *bc = (struct nw_babel_client_s *)arg;

  syslog(LOG_WARNING, "%s: Connection with local Babel instance timed out.", bc->module->name);
  nw_routing_babel_close(bc);
}

static int nw_routing_babel_start_acquire_data(nodewatcher_module_t *module) {

  int fd;
  struct ifaddrs *ifaddr, *ifa;
  struct sockaddr_in6 babel_addr;

  if (bc.fd > 0)
    return -1;

  bc.object = json_object_new_object();

  /* Get the link-local addresses of the local interfaces. */
  json_object *link_local = json_object_new_array();
  json_object_object_add(bc.object, "link_local", link_local);

  if (!getifaddrs(&ifaddr)) {

    char host[NI_MAXHOST];

    for (ifa = ifaddr; ifa; ifa = ifa->ifa_next) {

      if (ifa->ifa_addr == NULL)
        continue;

      /* Skip interfaces which are down. */
      if (!(ifa->ifa_flags & IFF_UP))
        continue;

      /* Skip non-ipv6 addresses. */
      if (ifa->ifa_addr->sa_family != AF_INET6)
        continue;

      /* Skip non-link-local addresses. */
      if (!IN6_IS_ADDR_LINKLOCAL(&((struct sockaddr_in6*) ifa->ifa_addr)->sin6_addr))
        continue;

      if (getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in6), host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST))
        continue;

      json_object_array_add(link_local, json_object_new_string(host));
    }

    freeifaddrs(ifaddr);

  }
  else {
    syslog(LOG_WARNING, "%s: Failed to obtain link-local addresses.", module->name);
  }

  fd = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
  if (fd) {

    memset((char *)&babel_addr, 0, sizeof(babel_addr));
    babel_addr.sin6_family = AF_INET6;
    babel_addr.sin6_port = htons(33123);
    inet_pton(AF_INET6, "::1", babel_addr.sin6_addr.s6_addr);

    if (connect(fd, (struct sockaddr *)&babel_addr, sizeof(babel_addr)) < 0) {
      syslog(LOG_WARNING, "%s: Could not connect to babeld.", module->name);
    }
    else {


      bc.fd = fd;
      bc.stream = lu_stream_create(2048);

      lu_fdn_t fdn;
      fdn.fd = fd;
      fdn.recv = nw_routing_babel_recv;
      fdn.options = LS_READ;
      fdn.data = &bc;

      bc.fdn = lu_fd_add(&fdn);

      lu_task_insert(5, nw_routing_babel_timeout, (void *)&bc);
    }

  }

  return 0;
}

static int nw_routing_babel_init(nodewatcher_module_t *module) {

  bc.fd = -1;
  bc.module = module;

  return 0;
}

/* Module descriptor. */
MODULE_DESC = {
  .name = "core.routing.babel",
  .author = "Jernej Kos <jernej@kos.mx>",
  .version = 1,
  .hooks = {
    .init = nw_routing_babel_init,
    .start_acquire_data = nw_routing_babel_start_acquire_data
  },
  .schedule = {
    .refresh_interval = 3
  }
};
