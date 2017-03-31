#ifndef NODEWATCHER_UTILS_H
#define NODEWATCHER_UTILS_H

#include <json-c/json.h>

char *nw_utils_string_trim(char *);
int nw_utils_string_cmp(char *, const char *);
int nw_file_line_count(const char *);
int nw_json_from_file(const char *, json_object *, const char *, int);

#endif
