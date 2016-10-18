#include "json_parser.h"

#ifndef JSON_PARSE_STACK_INIT_SIZE
#define JSON_PARSE_STACK_INIT_SIZE 256
#endif

#define EXPECT(c, ch)    do { assert(*c->json == (ch)); c->json++; } while (0)
#define ISDIGIT(ch)       ((ch) >= '0' && (ch) <= '9')
#define ISDIGIT1TO9(ch)   ((ch) >= '1' && (ch) <= '9')
#define PUTC(c, ch)          do { *(char *) json_context_push(c, sizeof(char)) = (ch); } while (0)
#define STRING_ERROR(ret) do { c->top = head; return ret; } while (0)

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
    char *p;
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

    errno = 0;
    v->json_n = strtod(c->json, NULL);
    if (errno == ERANGE && (v->json_n == HUGE_VAL || v->json_n == -HUGE_VAL))
        return JSON_PARSE_NUMBER_TOO_BIG;
    c->json = p;
    v->type = JSON_NUMBER;
    return JSON_PARSE_OK;
}

static const char *json_parse_hex4(const char *p, unsigned *u)
{
    int i;

    *u = 0;
    for (i = 0; i < 4; ++i) {
        *u <<= 4;
        if (ISDIGIT(p[i]))
            *u |= p[i] - '0';
        else if (p[i] <= 'f' && p[i] >= 'a')
            *u |= p[i] - 'a' + 10;
        else if (p[i] <= 'F' && p[i] >= 'A')
            *u |= p[i] - 'A' + 10;
        else
            return NULL;
    }
    return p + 4;
}

static void json_encode_utf8(json_context *c, unsigned u)
{
    assert(u >= 0x0000 && u <= 0x10ffff);
    if (u <= 0x007f)
        PUTC(c, u & 0x7f);
    else if (u <= 0x07ff) {
        PUTC(c, 0xc0 | ((u >>  6) & 0xff));
        PUTC(c, 0x80 | ( u        & 0x3f));
    } else if (u <= 0xffff) {
        PUTC(c, 0xe0 | ((u >> 12) & 0xff));
        PUTC(c, 0x80 | ((u >>  6) & 0x3f));
        PUTC(c, 0x80 | ( u        & 0x3f));
    } else { /* 0x10000 ~ 0x10ffff */
        PUTC(c, 0xf0 | ((u >> 18) & 0xff));
        PUTC(c, 0x80 | ((u >> 12) & 0x3f));
        PUTC(c, 0x80 | ((u >>  6) & 0x3f));
        PUTC(c, 0x80 | ( u        & 0x3f));
    }
}

static int json_parse_string(json_context *c, json_value *v)
{
    size_t head, len;
    unsigned u, low = 0;  /* low surrogate */
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
        case '\\':
            switch (*p++) {
            case '\\': PUTC(c, '\\'); break;
            case '/':  PUTC(c, '/' ); break;
            case '\"': PUTC(c, '\"'); break;
            case 't':  PUTC(c, '\t'); break;
            case 'b':  PUTC(c, '\b'); break;
            case 'f':  PUTC(c, '\f'); break;
            case 'n':  PUTC(c, '\n'); break;
            case 'r':  PUTC(c, '\r'); break;
            case 'u':  /* UTF-8 */
                if (!(p = json_parse_hex4(p, &u)))
                    STRING_ERROR(JSON_PARSE_INVALID_UNICODE_HEX);
                if (u >= 0xd800 && u <= 0xdbff) { /* high surrogate */
                    if (p[0] != '\\' || p[1] != 'u')
                        STRING_ERROR(JSON_PARSE_INVALID_UNICODE_SURROGATE);
                    if (!(p = json_parse_hex4(p + 2, &low)))
                        STRING_ERROR(JSON_PARSE_INVALID_UNICODE_HEX);
                    if (low > 0xdfff || low < 0xdc00)
                        STRING_ERROR(JSON_PARSE_INVALID_UNICODE_SURROGATE);
                    u = 0x10000 + (u - 0xD800) * 0x400 + (low - 0xDC00);
                }
                json_encode_utf8(c, u);
                break;
            default:
                c->top = head;
                return JSON_PARSE_INVALID_STRING_ESCAPE;
            }
            break;
        case '\0':
            c->top = head;
            return JSON_PARSE_MISS_QUOTATION_MARK;
        default:
            if (ch >= '\x00' && ch <= '\x1F') {
                c->top = head;
                return JSON_PARSE_INVALID_STRING_CHAR;
            }
            PUTC(c, ch);
        }
    }
}

static int json_parse_value(json_context *c, json_value *v);
static int json_parse_array(json_context *c, json_value *v)
{
    const char *p;
    size_t size;
    int ret;

    EXPECT(c, '[');
    size = 0;
    p = c->json;
    for ( ; ; ) {
        json_parse_whitespace(c);
        if (*c->json == ']') {
            c->json++;
            v->type = JSON_ARRAY;
            v->json_size = size;
            size *= sizeof(json_value);
            if (size > 0)
                memcpy(v->json_e = (json_value *) malloc(size), json_context_pop(c, size), size);
            else
                v->json_e = NULL;
            return JSON_PARSE_OK;
        } else {
            json_value e;
            json_init(&e);
            if ((ret = json_parse_value(c, &e)) != JSON_PARSE_OK) {
                goto free;
            }
            memcpy(json_context_push(c, sizeof(json_value)), &e, sizeof(json_value));
            ++size;
            json_parse_whitespace(c);
            if (*c->json == ']') {
                continue;
            } else if (*c->json == ',') {
                c->json++;
                if (*c->json == ']') {
                    ret = JSON_PARSE_INVALID_VALUE;
                    goto free;
                }
            } else {
                ret = JSON_PARSE_MISS_COMMA_OR_SQUARE_BRACKET;
                goto free;
            }
        }
    }
free:
    while (c->top != 0) {
        p = json_context_pop(c, sizeof(json_value));
        json_free((json_value *) p);
    }
    return ret;
}

/* value = null / false / true / number */
static int json_parse_value(json_context *c, json_value *v)
{
    switch (*c->json) {
    case 'n':  return json_parse_literal(c, v, "null", JSON_NULL);
    case 't':  return json_parse_literal(c, v, "true", JSON_TRUE);
    case 'f':  return json_parse_literal(c, v, "false", JSON_FALSE);
    case '\"': return json_parse_string(c, v);
    case '[':  return json_parse_array(c, v);
    case '\0': return JSON_PARSE_EXPECT_VALUE;
    default:   return json_parse_number(c, v);
    }
}

void json_free(json_value *v)
{
    assert(v != NULL);
    if (v->type == JSON_STRING) {
        free(v->json_s);
    } else if (v->type == JSON_ARRAY) {
        for ( ; v->json_size > 0; v->json_size--)
            json_free(&v->json_e[v->json_size - 1]);
        free(v->json_e);
    }
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
        if (c.json[0] != '\0') {
            v->type = JSON_NULL;
            ret = JSON_PARSE_ROOT_NOT_SINGULAR;
        }
    }
    assert(c.top == 0);
    free(c.stack);
    return ret;
}

json_type json_get_type(const json_value *v)
{
    assert(v != NULL);
    return v->type;
}

int json_get_boolean(const json_value *v)
{
    assert(v != NULL && (v->type == JSON_TRUE || v->type == JSON_FALSE));
    return v->type == JSON_TRUE;
}

void json_set_boolean(json_value *v, int b)
{
    json_free(v);
    v->type = b ? JSON_TRUE : JSON_FALSE;
}

double json_get_number(const json_value *v)
{
    assert(v != NULL && v->type == JSON_NUMBER);
    return v->json_n;
}

void json_set_number(json_value *v, double n)
{
    json_free(v);
    v->json_n = n;
    v->type = JSON_NUMBER;
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

json_value *json_get_array_element(const json_value *v, size_t index)
{
    assert(v != NULL && v->type == JSON_ARRAY);
    assert(index < v->json_size);
    return &v->json_e[index];
}

size_t json_get_array_size(const json_value *v)
{
    assert(v != NULL && v->type == JSON_ARRAY);
    return v->json_size;
}
