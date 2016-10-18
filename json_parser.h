#ifndef JSON_PARSER_H__
# define JSON_PARSER_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <assert.h>

typedef enum {
    JSON_NULL,
    JSON_FALSE,
    JSON_TRUE,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT,
} json_type;

typedef struct json_value json_value;
struct json_value {
    union {
        struct {
            char *s;
            size_t len;
        } s;
        struct {
            json_value *e;
            size_t size;
        } a;
        double n;
    } u;
#define json_s     u.s.s
#define json_len   u.s.len
#define json_n     u.n
#define json_e     u.a.e
#define json_size  u.a.size
    json_type type;
};

enum {
    JSON_PARSE_OK = 0,
    JSON_PARSE_EXPECT_VALUE,
    JSON_PARSE_INVALID_VALUE,
    JSON_PARSE_NUMBER_TOO_BIG,
    JSON_PARSE_MISS_QUOTATION_MARK,
    JSON_PARSE_INVALID_STRING_ESCAPE,
    JSON_PARSE_INVALID_STRING_CHAR,
    JSON_PARSE_INVALID_UNICODE_HEX,
    JSON_PARSE_INVALID_UNICODE_SURROGATE,
    JSON_PARSE_MISS_COMMA_OR_SQUARE_BRACKET,
    JSON_PARSE_ROOT_NOT_SINGULAR
};

#define json_init(v) do { (v)->type = JSON_NULL; } while (0)
void json_free(json_value *v);
#define json_set_null(v) json_free(v)

int json_get_boolean(const json_value *v);
void json_set_boolean(json_value *v, int b);

double json_get_number(const json_value *v);
void json_set_number(json_value *v, double n);

const char *json_get_string(const json_value *v);
void json_set_string(json_value *v, const char *s, size_t len);
size_t json_get_string_length(const json_value *v);

int json_parse(json_value *v, const char *json);
json_type json_get_type(const json_value *v);

json_value *json_get_array_element(const json_value *v, size_t index);
size_t json_get_array_size(const json_value *v);

#endif //JSON_PARSER_H__
