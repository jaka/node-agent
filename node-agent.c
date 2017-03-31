#include <stdio.h>
#include <syslog.h>
#include <unistd.h>
#include <libre/scheduler.h>

#include "modules.h"
#include "node-agent.h"

int main(int argc, char **argv) {

  char c;
  lu_args args;
  int log_option = LOG_PID | LOG_CONS;

  args.argc = argc;
  args.argv = argv;

  while ((c = lu_getopt(&args, "d")) != EOF) {
    switch (c) {
      case 'd': log_option |= LOG_PERROR; break;
    }
  }

  openlog(APP, log_option, LOG_DAEMON);

  lu_init();

  if (nw_module_init(&args) < 0) {
    fprintf(stderr, "ERROR: Failed to initialize modules!\n");
    return 1;
  }

  if ((log_option & LOG_PERROR) == LOG_PERROR && daemon(1, 0)) {
    fprintf(stderr, "ERROR: Failed to daemonize, exit: %m\n");
    return 1;
  }
  syslog(LOG_INFO, "Entering loop.");

  lu_loop();

  return 0;
}
