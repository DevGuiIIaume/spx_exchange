#define _POSIX_C_SOURCE 200809L

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include "cmocka.h"

#include "../spx_common.h"
#include "../spx_exchange.h"

#define BUFFER_SIZE (1024)

static void test_positive_exchange_parse_args(void **state) {
    char *argv[] = {"./spx_exchange", "products.txt", "./spx_trader_a",
                        "./spx_trader_b", "test.in"};
    int argc = 5;

    char product_filename[BUFFER_SIZE] = {0};
    char testing_filename[BUFFER_SIZE] = {0};
    char *trader_filenames[BUFFER_SIZE] = {0};

    int num_traders = exchange_parse_args(argc, argv, product_filename,
                                            trader_filenames, testing_filename);

    assert_int_equal(num_traders, 2);
    assert_string_equal(product_filename, "products.txt");
    assert_string_equal(trader_filenames[0], "./spx_trader_a");
    assert_string_equal(trader_filenames[1], "./spx_trader_b");
}

static void test_negative_exchange_parse_args(void **state) {
    char *argv[] = {"./spx_exchange products.txt"};
    int argc = 2;

    char product_filename[BUFFER_SIZE] = {0};
    char testing_filename[BUFFER_SIZE] = {0};
    char *trader_filenames[BUFFER_SIZE] = {0};

    int num_traders = exchange_parse_args(argc, argv, product_filename,
                                            trader_filenames, testing_filename);

    assert_int_equal(-1, num_traders);
}

static void test_negative_init_amended_order(void **state) {
    char buffer[BUFFER_SIZE] = "AMEND 0 -1 5;";
    order *old_order = {0};

    order *new_order = init_amended_order(buffer, NULL, old_order);
    assert_true(NULL == new_order);
}

static void test_negative_init_new_order(void **state) {
    char buffer[BUFFER_SIZE] = "BUY 0 GPU -1 5;";
    order *new_order = init_new_order(ACCEPTED_BUY, buffer, NULL, BUY);
    assert_true(NULL == new_order);
}

static void test_negative_delete_order(void **state) {
    order *deleted_order = delete_order(NULL, NULL);
    assert_true(NULL == deleted_order);
}

static void test_negative_is_valid_order_id(void **state) {
    char buffer[BUFFER_SIZE] = "BUY 1000000 GPU 10 1000;";
    bool result = is_valid_order_id(buffer, NULL);
    assert_false(result);
}

static void test_negative_is_valid_command_format(void **state) {
    char buffer[BUFFER_SIZE] = "BUY 0 GPU 10 500";
    bool result = is_valid_command_format(buffer);
    assert_false(result);
}

void assert_order_equal(const order *order_a, const order *order_b) {
    assert_string_equal(order_a->product_name, order_b->product_name);
    assert_true(order_a->type == order_b->type);
    assert_true(order_a->amended == order_b->amended);
    assert_true(order_a->owner == order_b->owner);
    assert_true(order_a->order_id == order_b->order_id);
    assert_true(order_a->quantity == order_b->quantity);
    assert_true(order_a->price == order_b->price);
}

static void test_positive_buy_linked_list(void **state) {
    char buffer_a[BUFFER_SIZE] = "BUY 0 GPU 10 500";
    char buffer_b[BUFFER_SIZE] = "BUY 1 GPU 20 600";
    char buffer_c[BUFFER_SIZE] = "BUY 2 GPU 30 500";
    char buffer_d[BUFFER_SIZE] = "BUY 3 GPU 40 400";

    order *order_a = init_new_order(ACCEPTED_BUY, buffer_a, NULL, BUY);
    order *order_b = init_new_order(ACCEPTED_BUY, buffer_b, NULL, BUY);
    order *order_c = init_new_order(ACCEPTED_BUY, buffer_c, NULL, BUY);
    order *order_d = init_new_order(ACCEPTED_BUY, buffer_d, NULL, BUY);

    order *head = order_a;
    head = insert_order(head, order_b, BUY);
    head = insert_order(head, order_c, BUY);
    head = insert_order(head, order_d, BUY);

    order *cursor = head;
    assert_order_equal(cursor, order_b);
    cursor = cursor->next;
    assert_order_equal(cursor, order_a);
    cursor = cursor->next;
    assert_order_equal(cursor, order_c);
    cursor = cursor->next;
    assert_order_equal(cursor, order_d);
    cursor = cursor->next;
    assert_true(NULL == cursor);

    head = delete_order(order_c, head);
    cursor = head;
    assert_order_equal(cursor, order_b);
    cursor = cursor->next;
    assert_order_equal(cursor, order_a);
    cursor = cursor->next;
    assert_order_equal(cursor, order_d);
    cursor = cursor->next;
    assert_true(NULL == cursor);

    head = delete_order(order_d, head);
    cursor = head;
    assert_order_equal(cursor, order_b);
    cursor = cursor->next;
    assert_order_equal(cursor, order_a);
    cursor = cursor->next;
    assert_true(NULL == cursor);

    head = delete_order(order_b, head);
    cursor = head;
    assert_order_equal(cursor, order_a);
    cursor = cursor->next;
    assert_true(NULL == cursor);

    head = delete_order(order_a, head);
    assert_true(NULL == head);
}

static void test_positive_sell_linked_list(void **state) {
    char buffer_a[BUFFER_SIZE] = "SELL 0 GPU 10 500";
    char buffer_b[BUFFER_SIZE] = "SELL 1 GPU 20 600";
    char buffer_c[BUFFER_SIZE] = "SELL 2 GPU 30 500";
    char buffer_d[BUFFER_SIZE] = "SELL 3 GPU 40 400";

    order *order_a = init_new_order(ACCEPTED_SELL, buffer_a, NULL, SELL);
    order *order_b = init_new_order(ACCEPTED_SELL, buffer_b, NULL, SELL);
    order *order_c = init_new_order(ACCEPTED_SELL, buffer_c, NULL, SELL);
    order *order_d = init_new_order(ACCEPTED_SELL, buffer_d, NULL, SELL);

    order *head = order_a;
    head = insert_order(head, order_b, SELL);
    head = insert_order(head, order_c, SELL);
    head = insert_order(head, order_d, SELL);

    order *cursor = head;
    assert_order_equal(cursor, order_d);
    cursor = cursor->next;
    assert_order_equal(cursor, order_a);
    cursor = cursor->next;
    assert_order_equal(cursor, order_c);
    cursor = cursor->next;
    assert_order_equal(cursor, order_b);
    cursor = cursor->next;
    assert_true(NULL == cursor);

    head = delete_order(order_a, head);
    cursor = head;
    assert_order_equal(cursor, order_d);
    cursor = cursor->next;
    assert_order_equal(cursor, order_c);
    cursor = cursor->next;
    assert_order_equal(cursor, order_b);
    cursor = cursor->next;
    assert_true(NULL == cursor);

    head = delete_order(order_b, head);
    cursor = head;
    assert_order_equal(cursor, order_d);
    cursor = cursor->next;
    assert_order_equal(cursor, order_c);
    cursor = cursor->next;
    assert_true(NULL == cursor);

    head = delete_order(order_d, head);
    cursor = head;
    assert_order_equal(cursor, order_c);
    cursor = cursor->next;
    assert_true(NULL == cursor);

    head = delete_order(order_c, head);
    assert_true(NULL == head);
}

int main() {
    // Construct a test struct containing all the tests
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_positive_exchange_parse_args),
        cmocka_unit_test(test_negative_exchange_parse_args),
        cmocka_unit_test(test_negative_init_amended_order),
        cmocka_unit_test(test_negative_init_new_order),
        cmocka_unit_test(test_negative_delete_order),
        cmocka_unit_test(test_negative_is_valid_order_id),
        cmocka_unit_test(test_negative_is_valid_command_format),
        cmocka_unit_test(test_positive_buy_linked_list),
        cmocka_unit_test(test_positive_sell_linked_list)
    };

    // Run the tests
    return cmocka_run_group_tests(tests, NULL, NULL);
}