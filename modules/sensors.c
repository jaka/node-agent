#include <arpa/inet.h>
#include <libre/scheduler.h>
#include <libre/stream.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "modules.h"

struct nw_usbtemp_client_s {
  lu_fdn_t *fdn;
  lu_stream_t *stream;
  json_object *object;
  nodewatcher_module_t *module;
};

static struct nw_usbtemp_client_s bc;

static void nw_sensors_usbtemp_close(struct nw_usbtemp_client_s *bc)
{
  int fd;

  fd = bc->fdn->fd;
  lu_fd_del(bc->fdn);

  lu_stream_destroy(bc->stream);
  bc->stream = NULL;

  lu_task_remove((void *)bc);

  close(fd);

  nw_module_finish_acquire_data(bc->module, bc->object);

  bc->object = NULL;
}

static void nw_sensors_usbtemp_recv(void *arg)
{
  struct nw_usbtemp_client_s *bc;
  json_object *temperature;
  char line[1024];
  char *start;
  float temp;
  int rv;

  bc = (struct nw_usbtemp_client_s *)arg;

  if (bc->stream == NULL)
  {
    return;
  }

  lu_stream_readin_fd(bc->stream, bc->fdn->fd);

  for(;;)
  {
    rv = lu_stream_readline(bc->stream, line, sizeof(line));
    if (rv < 0)
    {
      break;
    }

    start = strchr(line, ':');
    if (start)
    {
      if (sscanf(start + 1, " %f", &temp) == 1)
      {
        temperature = json_object_new_object();
        json_object_object_add(bc->object, "temperature", temperature);
        json_object_object_add(temperature, "name", json_object_new_string("Outdoor"));
        json_object_object_add(temperature, "unit", json_object_new_string("C"));
        json_object_object_add(temperature, "value", json_object_new_double(temp));
      }
    }
    return nw_sensors_usbtemp_close(bc);
  }
}

static void nw_sensors_usbtemp_timeout(void *arg)
{
  struct nw_usbtemp_client_s *bc;

  bc = (struct nw_usbtemp_client_s *)arg;
  syslog(LOG_WARNING, "%s: Connection with local Babel instance timed out.", bc->module->name);
  nw_sensors_usbtemp_close(bc);
}

static int nw_sensors_start_acquire_data(nodewatcher_module_t *module)
{
  struct sockaddr_in temp_addr;
  lu_fdn_t fdn;

  if (bc.object)
  {
    return -1;
  }

  bc.object = json_object_new_object();

  fdn.fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (fdn.fd < 0)
  {
    syslog(LOG_WARNING, "%s: Could not create socket.", module->name);
    return nw_module_finish_acquire_data(module, bc.object);
  }

  memset((char *)&temp_addr, 0, sizeof(temp_addr));
  temp_addr.sin_family = AF_INET;
  temp_addr.sin_port = htons(2000);
  inet_pton(AF_INET, "127.0.0.1", &(temp_addr.sin_addr));

  if (connect(fdn.fd, (struct sockaddr *)&temp_addr, sizeof(temp_addr)) < 0) {
    syslog(LOG_WARNING, "%s: Could not connect to local usbtempd instance.", module->name);
    return nw_module_finish_acquire_data(module, bc.object);
  }

  bc.stream = lu_stream_create(256);

  fdn.recv = nw_sensors_usbtemp_recv;
  fdn.options = LS_READ;
  fdn.data = &bc;

  bc.fdn = lu_fd_add(&fdn);

  lu_task_insert(5, nw_sensors_usbtemp_timeout, (void *)&bc);

  return 0;
}

static int nw_sensors_init(nodewatcher_module_t *module)
{
  bc.module = module;
  bc.object = NULL;

  return 0;
}

MODULE_DESC = {
  .name = "sensors.generic",
  .author = "jaka@live.jp",
  .version = 1,
  .hooks = {
    .init = nw_sensors_init,
    .start_acquire_data = nw_sensors_start_acquire_data,
  },
  .schedule = {
    .refresh_interval = 30,
  },
};
