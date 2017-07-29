#include <fcntl.h>
#include <libre/scheduler.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

#include "modules.h"

#define DS18X20_ROM_SIZE 8
#define DS18X20_SP_SIZE 9
#define DS18X20_GENERATOR 0x8c

static int ut_errno;

static unsigned char lsb_crc8(unsigned char *data_in, unsigned int len, const unsigned char generator)
{
  unsigned char i, bit_counter;
  unsigned char crc = 0;

  for (i = 0; i < len; i++) {
    crc ^= *(data_in + i);
    bit_counter = 8;
    do {
      if (crc & 0x01)
        crc = (((crc >> 1) & 0x7f) ^ generator);
      else
        crc = (crc >> 1) & 0x7f;
      bit_counter--;
    } while (bit_counter > 0);
  }
  return crc;
}

static int owReset(int fd)
{
  int rv;
  int wbytes, rbytes;
  unsigned char wbuff, rbuff;
  fd_set readset;
  struct timeval timeout_tv;
  struct termios term;

  tcflush(fd, TCIOFLUSH);

  if (tcgetattr(fd, &term) < 0) {
    ut_errno = 1;
    return -1;
  }
  term.c_cflag &= ~CSIZE | CS8;
  cfsetispeed(&term, B9600);
  cfsetospeed(&term, B9600);
  tcsetattr(fd, TCSANOW, &term);

  /* Send the reset pulse. */
  wbuff = 0xf0;
  wbytes = write(fd, &wbuff, 1);
  if (wbytes != 1) {
    ut_errno = 9;
    return -1;
  }

  timeout_tv.tv_usec = 0;
  timeout_tv.tv_sec = 1;

  FD_ZERO(&readset);
  FD_SET(fd, &readset);

  if (select(fd + 1, &readset, NULL, NULL, &timeout_tv) > 0) {

    if (FD_ISSET(fd, &readset)) {
      rbytes = read(fd, &rbuff, 1);
      if (rbytes != 1)
        return -1;
      switch (rbuff) {
        case 0:
          /* Ground. */
        case 0xf0:
          /* No response. */
          rv = -1; break;
        default:
          /* Got a response */
          rv = 0;
      }
    }
    else {
      rv = -1;
    }
  }
  else {
    rv = -1; /* Timed out or interrupt. */
  }

  term.c_cflag &= ~CSIZE | CS6;
  cfsetispeed(&term, B115200);
  cfsetospeed(&term, B115200);

  tcsetattr(fd, TCSANOW, &term);

  return rv;
}

static unsigned char owWriteByte(int fd, unsigned char wbuff)
{
  char buf[8];
  int wbytes;
  unsigned char rbuff, i;
  size_t remaining, rbytes;
  fd_set readset;
  struct timeval timeout_tv;

  tcflush(fd, TCIOFLUSH);

  for (i = 0; i < 8; i++)
    buf[i] = (wbuff & (1 << (i & 0x7))) ? 0xff : 0x00;
  wbytes = write(fd, buf, 8);
  if (wbytes != 8) {
    ut_errno = 9;
    return -1;
  }

  timeout_tv.tv_usec = 0;
  timeout_tv.tv_sec = 1;

  FD_ZERO(&readset);
  FD_SET(fd, &readset);

  rbuff = 0;
  remaining = 8;
  while (remaining > 0) {

    if (select(fd + 1, &readset, NULL, NULL, &timeout_tv) > 0) {

      if (FD_ISSET(fd, &readset)) {
        rbytes = read(fd, &buf, remaining);
        for (i = 0; i < rbytes; i++) {
          rbuff >>= 1;
          rbuff |= (buf[i] & 0x01) ? 0x80 : 0x00;
          remaining--;
        }
      }
      else
        return 0xff;
    }
    else {
      /* At last timeout will terminate while-loop. */
      return 0xff;
    }
  }
  return rbuff;
}

static unsigned char owReadByte(int fd)
{
  return owWriteByte(fd, 0xff);
}

int warn_owWriteByte(int fd, unsigned char wbuff)
{
  if (owWriteByte(fd, wbuff) != wbuff)
   return -1;
  return 0;
}

static int file_exists(const char *filename)
{
  struct stat st;
  return (stat(filename, &st) == 0);
}

int serial_init(const char *serial_port)
{
  int fd;
  struct termios term;

  if (!file_exists(serial_port)) {
    ut_errno = 3;
    return -1;
  }

  if (access(serial_port, R_OK|W_OK) < 0) {
    ut_errno = 4;
    return -1;
  }

  fd = open(serial_port, O_RDWR);
  if (fd < 0) {
    ut_errno = 5;
    return -1;
  }

  memset(&term, 0, sizeof(term));

  term.c_cc[VMIN] = 1;
  term.c_cc[VTIME] = 0;
  term.c_cflag |= CS6 | CREAD | HUPCL | CLOCAL;

  cfsetispeed(&term, B115200);
  cfsetospeed(&term, B115200);

  if (tcsetattr(fd, TCSANOW, &term) < 0) {
    close(fd);
    ut_errno = 0;
    return -1;
  }

  tcflush(fd, TCIOFLUSH);

  return fd;
}

int DS18B20_measure(int fd)
{
  if (owReset(fd) < 0) {
    ut_errno = 6;
    return -1;
  }
  if (warn_owWriteByte(fd, 0xcc) < 0) {
    ut_errno = 8;
    return -1;
  }
  if (warn_owWriteByte(fd, 0x44) < 0) {
    ut_errno = 8;
    return -1;
  }
  return 0;
}

int DS18B20_acquire(int fd, char *buf)
{
  unsigned short T;
  float temperature;
  unsigned char i, crc, sp_sensor[DS18X20_SP_SIZE];

  if (owReset(fd) < 0) {
    ut_errno = 6;
    return -1;
  }
  if (warn_owWriteByte(fd, 0xcc) < 0) {
    ut_errno = 8;
    return -1;
  }
  if (warn_owWriteByte(fd, 0xbe) < 0) {
    ut_errno = 8;
    return -1;
  }

  for (i = 0; i < DS18X20_SP_SIZE; i++)
    sp_sensor[i] = owReadByte(fd);

  if ((sp_sensor[4] & 0x9f) != 0x1f) {
    ut_errno = 6;
    return -1;
  }

  crc = lsb_crc8(&sp_sensor[0], DS18X20_SP_SIZE - 1, 0x8c);
  if (sp_sensor[DS18X20_SP_SIZE - 1] != crc) {
    ut_errno = 7;
    return -1;
  }

  T = (sp_sensor[1] << 8) + (sp_sensor[0] & 0xff);
  if ((T >> 15) & 0x01) {
    T--;
    T ^= 0xffff;
    T *= -1;
  }
  temperature = (float)T / 16;

  sprintf(buf, "%.2f", temperature);
  return 0;
}

/* *********** */

struct usbtemp_s {
  json_object *object;
  nodewatcher_module_t *module;
  int fd;
};

static struct usbtemp_s usbtemp;
const char *serial_port = "/dev/ttyUSB0";

static void nw_sensors_finish_acquire_data(void *arg)
{
  char temp[10];
  struct usbtemp_s *usbtemp = (struct usbtemp_s *)arg;
  if (!DS18B20_acquire(usbtemp->fd, temp)) {
    json_object *sensor = json_object_new_object();
    json_object_object_add(sensor, "name", json_object_new_string(serial_port));
    json_object_object_add(sensor, "unit", json_object_new_string("C"));
    json_object_object_add(sensor, "value", json_object_new_string(temp));
    json_object_object_add(usbtemp->object, "sensor", sensor);
  }

  close(usbtemp->fd);
  nw_module_finish_acquire_data(usbtemp->module, usbtemp->object);
  usbtemp->object = NULL;
}

static int nw_sensors_start_acquire_data(nodewatcher_module_t *module)
{
  if (usbtemp.object)
    return -1;

  usbtemp.object = json_object_new_object();

  usbtemp.fd = serial_init(serial_port);
  if (usbtemp.fd <= 0) {
    nw_module_finish_acquire_data(module, usbtemp.object);
    usbtemp.object = NULL;
  }

  if (DS18B20_measure(usbtemp.fd) < 0) {
    close(usbtemp.fd);
    nw_module_finish_acquire_data(module, usbtemp.object);
    usbtemp.object = NULL;
  }

  lu_task_insert(2, nw_sensors_finish_acquire_data, (void *)&usbtemp);
  return 0;
}

static int nw_sensors_init(nodewatcher_module_t *module)
{
  usbtemp.fd = 0;
  usbtemp.object = NULL;
  usbtemp.module = module;

  return 0;
}

/* Module descriptor. */
MODULE_DESC = {
  .name = "sensors.generic",
  .author = "jaka@live.jp",
  .version = 1,
  .hooks = {
    .init = nw_sensors_init,
    .start_acquire_data = nw_sensors_start_acquire_data,
  },
  .schedule = {
    .refresh_interval = 60,
  },
};
