#include "json_parser.h"

#ifndef JSON_PARSE_STACK_INIT_SIZE
#define JSON_PARSE_STACK_INIT_SIZE 256
#endif

#define EXPECT(c, ch)    do { assert(*c->json == (ch)); c->json++; } while (0)
#define ISDIGIT(ch)       ((ch) >= '0' && (ch) <= '9')
#define ISDIGIT1TO9(ch)   ((ch) >= '1' && (ch) <= '9')
#define PUTC(c, ch)          do { *(char *) json_context_push(c, sizeof(char)) = (ch); } while (0)

typedef struct {
    const char *json;
    char *stack;
    size_t size, top;
} json_context;

static void *json_context_push(json_context *c, size_t size)
{
    void *ret;

    assert(size > 0);
    if (c->top + size >= c->size) {
        if (c->size == 0)
            c->size = JSON_PARSE_STACK_INIT_SIZE;
        while (c->top + size >= c->size)
            c->size += c->size >> 1;
        c->stack = (char *) realloc(c->stack, c->size);
    }
    ret = c->stack + c->top;
    c->top += size;
    return ret;
}

static void *json_context_pop(json_context *c, size_t size)
{
    assert(c->top >= size);
    return c->stack + (c->top -= size);
}

/* ws = *(%x20 / %x09 / %x0A / %x0D) */
static void json_parse_whitespace(json_context *c)
{
    const char *p = c->json;

    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
        ++p;
    c->json = p;
}

/* null = "null", false = "false", true = "true" */
static int json_parse_literal(json_context *c, json_value *v,
        const char *literal, json_type type)
{
    size_t i;

    EXPECT(c, *literal);
    for (i = 0; *++literal != '\0'; ++i)
        if (c->json[i] != *literal)
            return JSON_PARSE_INVALID_VALUE;
    c->json += i;
    v->type = type;
    return JSON_PARSE_OK;
}

static int json_parse_number(json_context *c, json_value *v)
{
    char *end, *p;
    p = (char *) c->json;

    /* validate number */
    if (*p == '-')
        ++p;
    if (*p == '0')
        ++p;
    else {
        if (!ISDIGIT1TO9(*p))
            return JSON_PARSE_INVALID_VALUE;
        for (++p; ISDIGIT(*p); ++p)
            ;
    }
    if (*p == '.') {
        ++p;
        if (!ISDIGIT(*p))
            return JSON_PARSE_INVALID_VALUE;
        for (++p; ISDIGIT(*p); ++p)
            ;
    }
    if (*p == 'e' || *p == 'E') {
        ++p;
        if (*p == '-' || *p == '+')
            ++p;
        if (!ISDIGIT(*p))
            return JSON_PARSE_INVALID_VALUE;
        for (++p; ISDIGIT(*p); ++p)
            ;
    }

    v->json_n = strtod(c->json, &end);
    if (end == c->json)
        return JSON_PARSE_INVALID_VALUE;
    if (errno == ERANGE && v->json_n == HUGE_VAL)
        return JSON_PARSE_NUMBER_TOO_BIG;
    c->json = end;
    v->type = JSON_NUMBER;
    return JSON_PARSE_OK;
}

static int json_parse_string(json_context *c, json_value *v)
{
    size_t head, len;
    const char *p;

    EXPECT(c, '\"');
    head = c->top;
    p = c->json;
    for ( ; ; ) {
        char ch = *p++;
        switch (ch) {
        case '\"':
            len = c->top - head;
            json_set_string(v, (const char *) json_context_pop(c, len), len);
            c->json = p;
            return JSON_PARSE_OK;
        case '\0':
            c->top = head;
            return LEPT_PARSE_MISS_QUOTATION_MARK;
        default:
            PUTC(c, ch);
        }
    }
}

/* value = null / false / true / number */
static int json_parse_value(json_context *c, json_value *v)
{
    switch (*c->json) {
    case 'n':  return json_parse_literal(c, v, "null", JSON_NULL);
    case 't':  return json_parse_literal(c, v, "true", JSON_TRUE);
    case 'f':  return json_parse_literal(c, v, "false", JSON_FALSE);
    case '\"': return json_parse_string(c, v);
    case '\0': return JSON_PARSE_EXPECT_VALUE;
    default:   return json_parse_number(c, v);
    }
}

void json_free(json_value *v)
{
    assert(v != NULL);
    if (v->type == JSON_STRING)
        free(v->json_s);
    v->type = JSON_NULL;
}

int json_parse(json_value *v, const char *json)
{
    int ret;
    json_context c;

    assert(v != NULL);
    c.json = json;
    c.stack = NULL;
    c.size = c.top = 0;
    json_init(v);
    json_parse_whitespace(&c);
    if ((ret = json_parse_value(&c, v)) == JSON_PARSE_OK) {
        json_parse_whitespace(&c);
        if (c.json[0] != '\0')
            ret = JSON_PARSE_ROOT_NOT_SINGULAR;
    }
    assert(c.top == 0);
    free(c.stack);
    return ret;
}

json_type json_get_type(const json_value *v)
{
    assert(v != NULL && v->type < JSON_INVALID);
    return v->type;
}

/* Fix them */
int json_get_boolean(const json_value *v)
{
    assert(v != NULL && (v->type == JSON_TRUE || v->type == JSON_FALSE));
    return 0;
}

void json_set_boolean(json_value *v, int b)
{
    assert(v != NULL && (v->type == JSON_TRUE || v->type == JSON_FALSE));
}

double json_get_number(const json_value *v)
{
    assert(v != NULL && v->type == JSON_NUMBER);
    return v->json_n;
}

void json_set_number(json_value *v, double n)
{
    assert(v != NULL && v->type == JSON_NUMBER);
    v->json_n = n;
}

const char *json_get_string(const json_value *v)
{
    assert(v != NULL && v->type == JSON_STRING);
    return v->json_s;
}

void json_set_string(json_value *v, const char *s, size_t len)
{
    assert(v != NULL && (s != NULL || len == 0));
    json_free(v);
    v->json_s = (char *) malloc(len + 1);
    memcpy(v->json_s, s, len);
    v->json_s[len] = '\0';
    v->json_len = len;
    v->type = JSON_STRING;
}

size_t json_get_string_length(const json_value *v)
{
    assert(v != NULL && v->type == JSON_STRING);
    return v->json_len;
}
