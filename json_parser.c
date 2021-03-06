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
            c->size += c->size >> 1;   /* c->size *= 1.5 */
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

static int json_parse_string_raw(json_context *c, char **str, size_t *len)
{
    size_t head;
    unsigned u, low = 0;  /* low surrogate */
    const char *p;

    EXPECT(c, '\"');
    head = c->top;
    p = c->json;
    for ( ; ; ) {
        char ch = *p++;
        switch (ch) {
        case '\"':
            *len = c->top - head;
            *str = (char *) malloc(*len + 1);
            memcpy(*str, (const char *) json_context_pop(c, *len), *len);
            (*str)[*len] = 0;
            c->json = p;
            return JSON_PARSE_OK;
        case '\\':
            switch (*p++) {
            case '\\': PUTC(c, '\\'); break;
            case '/':  PUTC(c, '/' ); break;
            case '"': PUTC(c, '"'); break;
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

static int json_parse_string(json_context *c, json_value *v)
{
    int ret;
    char *s;
    size_t len;
    if ((ret = json_parse_string_raw(c, &s, &len)) == JSON_PARSE_OK) {
        json_set_string(v, s, len);
        free(s);
    }
    return ret;
}

static int json_parse_value(json_context *c, json_value *v);
static int json_parse_array(json_context *c, json_value *v)
{
    size_t size;
    int ret;

    EXPECT(c, '[');
    size = 0;
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
    while (c->top != 0)
        json_free(json_context_pop(c, sizeof(json_value)));
    return ret;
}

static void json_free_object_member(json_member *m);

static int json_parse_object(json_context *c, json_value *v)
{
    size_t size;
    int ret;
    char *s;
    size_t len;
    json_member m;

    EXPECT(c, '{');
    json_parse_whitespace(c);
    if (*c->json == '}') {
        c->json++;
        v->type = JSON_OBJECT;
        v->json_m = NULL;
        v->json_osz = 0;
        return JSON_PARSE_OK;
    }
    m.k = NULL;
    size = 0;
    for ( ; ; ) {
        json_parse_whitespace(c);
        if (*c->json != '\"') {
            ret = JSON_PARSE_MISS_KEY;
            goto free;
        }
        if ((ret = json_parse_string_raw(c, &s, &len)) != JSON_PARSE_OK)
            goto free;
        m.k = s;
        m.klen = len;
        json_parse_whitespace(c);
        if (*c->json != ':') {
            ret = JSON_PARSE_MISS_COLON;
            goto miss_colon;
        } else {
            c->json++;
        }
        json_parse_whitespace(c);
        json_init(&m.v);
        if ((ret = json_parse_value(c, &m.v)) != JSON_PARSE_OK)
            goto free;
        memcpy(json_context_push(c, sizeof(json_member)), &m, sizeof(json_member));
        ++size;
        json_parse_whitespace(c);
        if (*c->json == '}') {
            c->json++;
            v->type = JSON_OBJECT;
            v->json_osz = size;
            size *= sizeof(json_member);
            memcpy(v->json_m = (json_member *) malloc(size), json_context_pop(c, size), size);
            return JSON_PARSE_OK;
        } else if (*c->json == ',') {
            c->json++;
            continue;
        } else {
            ret = JSON_PARSE_MISS_COMMA_OR_CURLY_BRACKET;
            goto free;
        }
        m.k = NULL;
    }
miss_colon:
    free(m.k);
free:
    while (c->top != 0)
        json_free_object_member(json_context_pop(c, sizeof(json_member)));
    return ret;
}

/* value = null / false / true / number / array / object */
static int json_parse_value(json_context *c, json_value *v)
{
    switch (*c->json) {
    case 'n':  return json_parse_literal(c, v, "null", JSON_NULL);
    case 't':  return json_parse_literal(c, v, "true", JSON_TRUE);
    case 'f':  return json_parse_literal(c, v, "false", JSON_FALSE);
    case '\"': return json_parse_string(c, v);
    case '[':  return json_parse_array(c, v);
    case '{':  return json_parse_object(c, v);
    case '\0': return JSON_PARSE_EXPECT_VALUE;
    default:   return json_parse_number(c, v);
    }
}

void json_free(json_value *v)
{
    assert(v != NULL);
    switch (v->type) {
    case JSON_STRING:
        free(v->json_s);
        break;
    case JSON_ARRAY:
        for ( ; v->json_size > 0; v->json_size--)
            json_free(&v->json_e[v->json_size - 1]);
        free(v->json_e);
        break;
    case JSON_OBJECT:
        for ( ; v->json_osz > 0; v->json_osz--)
            json_free_object_member(&v->json_m[v->json_osz - 1]);
        free(v->json_m);
        break;
    default:
        ;
    }
    v->type = JSON_NULL;
}

static void json_free_object_member(json_member *m)
{
    json_free(&m->v);
    free(m->k);
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
    v->json_s[len] = 0;
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

size_t json_get_object_size(const json_value *v)
{
    assert(v != NULL && v->type == JSON_OBJECT);
    return v->json_osz;
}

const char *json_get_object_key(const json_value *v, size_t index)
{
    assert(v != NULL && v->type == JSON_OBJECT);
    return v->json_m[index].k;
}

size_t json_get_object_key_length(const json_value *v, size_t index)
{
    assert(v != NULL && v->type == JSON_OBJECT);
    return v->json_m[index].klen;
}

json_value *json_get_object_value(const json_value *v, size_t index)
{
    assert(v != NULL && v->type == JSON_OBJECT);
    return &v->json_m[index].v;
}

#ifndef JSON_PARSE_STRINGIFY_INIT_SIZE
# define JSON_PARSE_STRINGIFY_INIT_SIZE 256
#endif

#define PUTS(c, s, len)   memcpy(json_context_push(c, len), s, len);

static unsigned json_decode_utf8(const u_char* ch, size_t* pos)
{
    unsigned u = 0;
    ch += *pos;
    unsigned hex = *ch;
    if (hex <= 0xdf) {
        u |= (*ch & 0x1f);
        u <<= 6, ch++, (*pos)++;
        u |= (*ch & 0x3f);
    } else if (hex <= 0xef) {
        u |= (*ch & 0x0f);
        u <<= 6, ch++, (*pos)++;
        u |= (*ch & 0x3f);
        u <<= 6, ch++, (*pos)++;
        u |= (*ch & 0x3f);
    } else if (hex <= 0xf7) {
        u |= (*ch & 0x07);
        u <<= 6, ch++, (*pos)++;
        u |= (*ch & 0x3f);
        u <<= 6, ch++, (*pos)++;
        u |= (*ch & 0x3f);
        u <<= 6, ch++, (*pos)++;
        u |= (*ch & 0x3f);
    }
    return u;
}

static void json_stringify_string(json_context* c, const json_value* v)
{
    static const char hex_digits[] = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F' };
    size_t size;
    u_char* p;
    u_char* head;

    assert(v->json_s != NULL);
    size = v->json_len * 6 + 2;
    p = head = json_context_push(c, size);
    *p++ = '"';
    for (size_t i = 0; i < v->json_len; ++i) {
        u_char ch = (u_char) v->json_s[i];
        switch (ch) {
        case '\\': *p++ = '\\'; *p++ = '\\'; break;
        case '"':  *p++ = '\\'; *p++ = '"'; break;
        case '/':  *p++ = '\\'; *p++ = '/'; break;
        case '\t': *p++ = '\\'; *p++ = 't'; break;
        case '\b': *p++ = '\\'; *p++ = 'b'; break;
        case '\n': *p++ = '\\'; *p++ = 'n'; break;
        case '\r': *p++ = '\\'; *p++ = 'r'; break;
        case '\f': *p++ = '\\'; *p++ = 'f'; break;
        default:
            if (ch < 0x20) {
                *p++ = '\\'; *p++ = 'u'; *p++ = '0'; *p++ = '0';
                *p++ = hex_digits[ch >> 4];
                *p++ = hex_digits[ch & 15];
            } else if (ch > 0x7f) { /* Handle UTF-8. */
                unsigned u = json_decode_utf8((const u_char*) v->json_s, &i);;
                if (u <= 0xffff) {
                    *p++ = '\\'; *p++ = 'u';
                    *p++ = hex_digits[u >> 12];
                    *p++ = hex_digits[(u >> 8) & 15];
                    *p++ = hex_digits[(u >> 4) & 15];
                    *p++ = hex_digits[u & 15];
                } else if (u <= 0x10ffff) { /* Transfer codepoint to surrogate pair. */
                    unsigned h, l;
                    u -= 0x10000;
                    h = (u - (l = u % 0x400)) / 0x400;
                    h += 0xd800, l += 0xdc00;
                    *p++ = '\\'; *p++ = 'u';
                    *p++ = hex_digits[h >> 12];
                    *p++ = hex_digits[(h >> 8) & 15];
                    *p++ = hex_digits[(h >> 4) & 15];
                    *p++ = hex_digits[h & 15];
                    *p++ = '\\'; *p++ = 'u';
                    *p++ = hex_digits[l >> 12];
                    *p++ = hex_digits[(l >> 8) & 15];
                    *p++ = hex_digits[(l >> 4) & 15];
                    *p++ = hex_digits[l & 15];
                }
            } else {
                *p++ = v->json_s[i];
            }
            break;
        }
    }
    *p++ = '"';
    c->top -= size - (p - head);
}

static int json_stringify_value(json_context* c, const json_value* v);
static void json_stringify_object_member(json_context* c, const json_member* m)
{
    assert(m->k != NULL);
    PUTC(c, '"');
    PUTS(c, m->k, m->klen);
    PUTC(c, '"');
    PUTC(c, ':');
    json_stringify_value(c, &m->v);
}

static int json_stringify_value(json_context* c, const json_value* v)
{
    switch (v->type) {
    case JSON_NULL:  PUTS(c, "null", 4); break;
    case JSON_TRUE:  PUTS(c, "true", 4); break;
    case JSON_FALSE: PUTS(c, "false", 5); break;
    case JSON_NUMBER:
        c->top -= 32 - sprintf(json_context_push(c, 32), "%.17g", v->json_n);
        break;
    case JSON_OBJECT:
        PUTC(c, '{');
        for (size_t i = 0; i < v->json_osz; ++i) {
            json_stringify_object_member(c, &v->json_m[i]);
            PUTC(c, ',');
        }
        if (v->json_osz > 0)
            json_context_pop(c, 1);   /* Delete the last ',' */
        PUTC(c, '}');
        break;
    case JSON_ARRAY:
        PUTC(c, '[');
        for (size_t i = 0; i < v->json_size; ++i) {
            json_stringify_value(c, &v->json_e[i]);
            PUTC(c, ',');
        }
        if (v->json_size)
            json_context_pop(c, 1);  /* Delete the last ',' */
        PUTC(c, ']');
        break;
    case JSON_STRING: json_stringify_string(c, v); break;
    }
    return JSON_STRINGIFY_OK;
}

int json_stringify(const json_value* v, char** json, size_t* length)
{
    json_context c;
    int ret;
    
    assert(v != NULL);
    assert(json != NULL);
    c.stack = malloc(c.size = JSON_PARSE_STRINGIFY_INIT_SIZE);
    c.top = 0;
    if ((ret = json_stringify_value(&c, v)) != JSON_STRINGIFY_OK) {
        free(c.stack);
        *json = NULL;
        return ret;
    }
    if (length)
        *length = c.top;
    PUTC(&c, '\0');
    *json = c.stack;
    return JSON_STRINGIFY_OK;
}
