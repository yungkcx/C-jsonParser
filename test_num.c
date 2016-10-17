#include "json_parser.h"

static int main_ret = 0;
static int test_count = 0;
static int test_pass = 0;

#define EXPECT_EQ_BASE(equlity, expect, actual, format)\
    do {\
        test_count++;\
        if (equlity)\
            ++test_pass;\
        else {\
            fprintf(stderr, "%s:%d expect: " format " actual: " format "\n", __FILE__, __LINE__, expect, actual);\
            main_ret = 1;\
        }\
    } while (0);

#define EXPECT_EQ_INT(expect, actual) EXPECT_EQ_BASE((expect) == (actual), expect, actual, "%d")

#define EXPECT_EQ_DOUBLE(expect, actual) EXPECT_EQ_BASE((expect) == (actual), expect, actual, "%lf")

#define EXPECT_EQ_STRING(expect, actual, length)\
    EXPECT_EQ_BASE(sizeof(expect) - 1 == length && memcmp(expect, actual, length) == 0, expect, actual, "%s")

#define TEST_PARSE_EXPECT_VALUE(value, json)\
    do {\
        json_value v;\
        EXPECT_EQ_INT(JSON_PARSE_OK, json_parse(&v, json));\
        EXPECT_EQ_INT(value, json_get_type(&v));\
    } while (0)

#define TEST_ERROR(error, json)\
    do {\
        json_value v;\
        v.type = JSON_NULL;\
        EXPECT_EQ_INT(error, json_parse(&v, json));\
    } while (0)

#define TEST_NUMBER(expect, json)\
    do {\
        json_value v;\
        TEST_PARSE_EXPECT_VALUE(JSON_NUMBER, json);\
        EXPECT_EQ_DOUBLE(expect, json_get_number(&v));\
    } while (0)

void test_parse_number()
{
    TEST_NUMBER(0.0, "0.0");
    TEST_NUMBER(0.0, "-0");
    TEST_NUMBER(0.0, "-0.0");
    TEST_NUMBER(1.0, "1");
    TEST_NUMBER(-1.0, "-1");
    TEST_NUMBER(1.5, "1.5");
    TEST_NUMBER(-1.5, "-1.5");
    TEST_NUMBER(3.1416, "3.1416");
    TEST_NUMBER(1E10, "1E10");
    TEST_NUMBER(1e10, "1e10");
    TEST_NUMBER(1E+10, "1E+10");
    TEST_NUMBER(1E-10, "1E-10");
    TEST_NUMBER(-1E10, "-1E10");
    TEST_NUMBER(-1e10, "-1e10");
    TEST_NUMBER(-1E+10, "-1E+10");
    TEST_NUMBER(-1E-10, "-1E-10");
    TEST_NUMBER(1.234E+10, "1.234E+10");
    TEST_NUMBER(1.234E-10, "1.234E-10");
    /* must underflow */
    TEST_NUMBER(0.0, "1e-10000");
    /* the smallest number > 1 */
    TEST_NUMBER(1.0000000000000002, "1.0000000000000002");
    /* minimum denormal */
    TEST_NUMBER( 4.9406564584124654e-324, "4.9406564584124654e-324");
    TEST_NUMBER(-4.9406564584124654e-324, "-4.9406564584124654e-324");
    /* Max subnormal double */
    TEST_NUMBER( 2.2250738585072009e-308, "2.2250738585072009e-308"); 
    TEST_NUMBER(-2.2250738585072009e-308, "-2.2250738585072009e-308");
    /* Min normal positive double */
    TEST_NUMBER( 2.2250738585072014e-308, "2.2250738585072014e-308"); 
    TEST_NUMBER(-2.2250738585072014e-308, "-2.2250738585072014e-308");
    /* Max double */
    TEST_NUMBER( 1.7976931348623157e+308, "1.7976931348623157e+308");
    TEST_NUMBER(-1.7976931348623157e+308, "-1.7976931348623157e+308");
}

void test_parse_invalid_value()
{
    TEST_ERROR(JSON_PARSE_INVALID_VALUE, "+0");
    TEST_ERROR(JSON_PARSE_INVALID_VALUE, "+1");
    /* at least one digit before '.' */
    TEST_ERROR(JSON_PARSE_INVALID_VALUE, ".123");
    /* at least one digit after '.' */
    TEST_ERROR(JSON_PARSE_INVALID_VALUE, "1.");
    TEST_ERROR(JSON_PARSE_INVALID_VALUE, "INF");
    TEST_ERROR(JSON_PARSE_INVALID_VALUE, "inf");
    TEST_ERROR(JSON_PARSE_INVALID_VALUE, "NAN");
    TEST_ERROR(JSON_PARSE_INVALID_VALUE, "nan");
    TEST_ERROR(JSON_PARSE_ROOT_NOT_SINGULAR, "null x");
    TEST_ERROR(JSON_PARSE_EXPECT_VALUE, " ");
    TEST_ERROR(JSON_PARSE_INVALID_VALUE, " x ");
}

void test_parse_expect_value()
{
    TEST_PARSE_EXPECT_VALUE(JSON_NULL, "null");
    TEST_PARSE_EXPECT_VALUE(JSON_TRUE, "true");
    TEST_PARSE_EXPECT_VALUE(JSON_FALSE, "false");
}

static void test_access_string() {
    json_value v;
    json_init(&v);
    json_set_string(&v, "", 0);
    EXPECT_EQ_STRING("", json_get_string(&v), json_get_string_length(&v));
    json_set_string(&v, "Hello", 5);
    EXPECT_EQ_STRING("Hello", json_get_string(&v), json_get_string_length(&v));
    json_free(&v);
}
/* .. */

static void test_parse()
{
    test_parse_expect_value();
    test_parse_invalid_value();
    test_parse_number();
    test_access_string();
    /* ... */
}

int main()
{
    test_parse();
    printf("%d/%d (%3.2f%%) passed\n", test_pass, test_count, test_pass * 100.0 / test_count);
    return main_ret;
}
