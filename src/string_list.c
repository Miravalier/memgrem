#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "string_list.h"


string_list_t *string_list_new(void)
{
    string_list_t *list = malloc(sizeof(string_list_t));
    if (list == NULL) {
        fprintf(stderr, "error: out of memory while allocating list\n");
        return NULL;
    }
    list->capacity = 32;
    list->length = 0;
    list->strings = malloc(list->capacity * sizeof(char *));
    if (list->strings == NULL) {
        fprintf(stderr, "error: out of memory while allocating list capacity\n");
        free(list);
        return NULL;
    }
    return list;
}


bool string_list_append_n(string_list_t *list, const char *string, size_t string_length)
{
    if (list->length == list->capacity) {
        size_t new_capacity = list->capacity * 2;
        char **resized_strings = realloc(list->strings, new_capacity * sizeof(char *));
        if (resized_strings == NULL) {
            fprintf(stderr, "error: out of memory while growing list capacity\n");
            return false;
        }
        list->capacity = new_capacity;
        list->strings = resized_strings;
    }
    char *copied_string = malloc(string_length + 1);
    if (copied_string == NULL) {
        fprintf(stderr, "error: out of memory while allocating string capacity\n");
        return false;
    }
    memcpy(copied_string, string, string_length);
    copied_string[string_length] = '\0';
    list->strings[list->length++] = copied_string;
    return true;
}


bool string_list_append(string_list_t *list, const char *string)
{
    return string_list_append_n(list, string, strlen(string));
}


void string_list_pop(string_list_t *list)
{
    if (list->length == 0) {
        return;
    }
    free(list->strings[list->length - 1]);
    list->length--;
}


void string_list_free(string_list_t *list)
{
    for (size_t i=0; i < list->length; i++) {
        free(list->strings[i]);
    }
    free(list->strings);
    free(list);
}


string_list_t *string_split(const char *string, const char *separator, bool keep_empty)
{
    size_t string_length = strlen(string);
    size_t separator_length = strlen(separator);

    string_list_t *list = string_list_new();
    if (list == NULL) {
        return NULL;
    }
    size_t substring_start = 0;

    for (size_t i=0; i < string_length; i++) {
        if ((string_length - i) >= separator_length && memcmp(string + i, separator, separator_length) == 0) {
            if (i - substring_start > 0 || keep_empty) {
                if (!string_list_append_n(list, string + substring_start, i - substring_start)) {
                    string_list_free(list);
                    return NULL;
                }
            }
            i += separator_length;
            substring_start = i;
        }
    }

    if (string_length - substring_start > 0 || keep_empty) {
        if (!string_list_append_n(list, string + substring_start, string_length - substring_start)) {
            string_list_free(list);
            return NULL;
        }
    }

    return list;
}
