#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <json-c/json.h>

#include "utils.h"

char *nw_utils_string_trim(char *str) {

  char *end;

  /* Trim leading spaces. */
  while (isspace(*str)) str++;

  if (*str == 0)
    return str;

  /* Trim trailing spaces. */
  end = str + strlen(str) - 1;
  while (end > str && isspace(*end)) end--;

  /* Write new null terminator */
  *(end + 1) = 0;

  return str;
}

int nw_utils_string_cmp(char *str1, const char* str2) {

  return (strcmp(nw_utils_string_trim(str1), str2) == 0);
}

int nw_file_line_count(const char *filename) {

  FILE *file = fopen(filename, "r");

  if (!file)
    return -1;

  int lines = 0;
  while (!feof(file)) {
    fscanf(file, "%*[^\n]\n");
    lines++;
  }

  fclose(file);
  return lines;
}

int nw_json_from_file(const char *filename,
                      json_object *object,
                      const char *key,
                      int integer) {
  char tmp[1024];
  FILE *file = fopen(filename, "r");
  if (!file)
    return -1;

  char *buffer = NULL;
  size_t buffer_len = 0;

  while (!feof(file)) {
    size_t n = fread(tmp, 1, sizeof(tmp), file);
    char *tbuffer = (char *)realloc(buffer, buffer_len + n + 1);
    if (!tbuffer) {
      free(buffer);
      return -1;
    }
    buffer = tbuffer;

    memcpy(buffer + buffer_len, tmp, n);
    buffer_len += n;
  }
  fclose(file);

  buffer[buffer_len] = 0;
  if (integer)
    json_object_object_add(object, key, json_object_new_int(atoi(nw_utils_string_trim(buffer))));
  else
    json_object_object_add(object, key, json_object_new_string(nw_utils_string_trim(buffer)));
  free(buffer);
  return 0;
}
