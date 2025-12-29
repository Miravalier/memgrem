#ifndef _STRING_LIST_H
#define _STRING_LIST_H

#include <stdbool.h>
#include <stddef.h>


typedef struct string_list_t {
    char **strings;
    size_t capacity;
    size_t length;
} string_list_t;


bool streq(const char *a, const char *b);
bool str_contains(const char *haystack, const char *needle);

string_list_t *string_list_new(void);
bool string_list_append_n(string_list_t *list, const char *string, size_t string_length);
bool string_list_append(string_list_t *list, const char *string);
void string_list_pop(string_list_t *list);
void string_list_free(string_list_t *list);
string_list_t *string_split(const char *string, const char *separator, bool keep_empty);

#endif
