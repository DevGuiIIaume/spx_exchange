#ifndef SPX_EXCHANGE_H
#define SPX_EXCHANGE_H

#include "spx_common.h"
#include <inttypes.h>
#include <errno.h>
#include <math.h>
#include <limits.h>
#include <ctype.h>

#define STRLEN_AMEND (5)
#define STRLEN_CANCEL (6)
#define STRLEN_BUY (3)
#define STRLEN_SELL (4)

#define int64_t long long int

enum order_state {INVALID, AMENDED, CANCELLED, ACCEPTED_BUY, ACCEPTED_SELL};

typedef struct product_order product_order;

struct product_order {
    char *product_name;

    order *sell_orders;
    int sell_size;


    order *buy_orders;
    int buy_size;
};

void *my_calloc(size_t count, size_t size);
void my_free(void *ptr);
void free_order(order *current_order);
int free_pipenames(char **e2t_pipenames, char **t2e_pipenames, int size);
int exchange_parse_args(int argc, char **argv, char *product_filename,
                            char **trader_filenames, char *testing_filename);
int get_pipes(char **e2t_pipenames, char **t2e_pipenames, int num_traders);
trader *launch_trader(char *trader_filename, char *e2t_pipename,
                        char *t2e_pipename, int trader_id,
                        char *testing_filename);
void unlink_pipes(char **e2t_pipenames, int size);
int open_market(trader **traders, int num_traders);
order *search_orders(trader *current_trader, int order_id, order *orders);
order *search_orderbook(trader *current_trader, int order_id,
                        product_order **orderbook, int num_products);
void enqueue(queue *my_queue, int pid, int signal_type);
node *dequeue(queue *my_queue);
void sigusr1_handler(int signo, siginfo_t* sinfo, void* context);
void sigchild_handler(int signo, siginfo_t* sinfo, void* context);
char **get_products(char *product_filename, int *num_products_ptr);
void free_2d_char_array(char **array, int size);
void print_products(char **products, int num_products);
void free_trader(trader *current_trader);
void free_traders(trader **traders, int size);
trader *get_trader_id(trader **traders, int num_traders, int pid);
order *init_amended_order(char buffer[BUFFER_SIZE],
                            trader *current_trader, order *old_order);
order *init_new_order(enum order_state cmd, char buffer[BUFFER_SIZE],
                        trader *current_trader, enum order_type type);
order *insert_linked_list(order *head, order *cursor, order *new_order);
order *insert_order(order *head, order *new_order, enum order_type cmd);
product_order *get_product_from_orderbook(product_order **orderbook,
                                            char *product_name, int num_products);
order *delete_order(order *current_order, order *head);
position *get_position(trader *current_trader, char *product_name);
void update_position(trader *current_trader, char *product_name, int quantity,
                        int value, enum order_type type);
void fill_notify_trader(order *current_order, int quantity);
void fill_notify_traders(order *buy_order, order *sell_order, int quantity);
int64_t calculate_value(int64_t quantity, int64_t price);
void update_trader_position(order *current_order, int64_t final_value,
                            int64_t final_quantity);
void update_trader_positions(order *matched_order, order *new_order,
                                int64_t total_value, int64_t fee, int quantity);
void remove_order_from_orderbook(order *current_order,
                                product_order **orderbook, int num_products);
int64_t fill_buy_order(order *buy_order, product_order *product);
int64_t fill_sell_order(order *sell_order, product_order *product);
bool is_order_match(product_order *product);
order *process_command(enum order_state cmd, char buffer[BUFFER_SIZE],
                        trader *current_trader, product_order **orderbook,
                        int num_products);
int64_t process_amend(char buffer[BUFFER_SIZE], trader *current_trader,
                        product_order **orderbook, int num_products);
int64_t process_sell(char buffer[BUFFER_SIZE], trader *current_trader,
                        product_order **orderbook, int num_products);
int64_t process_buy(char buffer[BUFFER_SIZE], trader *current_trader,
                    product_order **orderbook, int num_products);
order *get_head(order *cursor);
int process_cancel(char buffer[BUFFER_SIZE], trader *current_trader,
                    product_order **orderbook, int num_products);
bool is_valid_command_name(char buffer[BUFFER_SIZE]);
bool is_valid_product(char buffer[BUFFER_SIZE], product_order **orderbook,
                        int num_products);
bool is_valid_order_id(char buffer[BUFFER_SIZE], trader *current_trader);
bool is_valid_order(char buffer[BUFFER_SIZE], trader *current_trader,
                    product_order **orderbook, int num_products);
bool is_valid_value(char buffer[BUFFER_SIZE]);
int check_product_name(char *buffer, enum order_state cmd);
int check_price(char *buffer, enum order_state cmd);
int check_quantity(char *buffer, enum order_state cmd);
int check_order_id(char *buffer, enum order_state cmd);
bool is_valid_command_format(char buffer[BUFFER_SIZE]);
bool is_semicolon_delimitted(char buffer[BUFFER_SIZE]);
bool is_valid_command(char buffer[BUFFER_SIZE], trader *current_trader,
                        product_order **orderbook, int num_products);
void replace_semicolon_with_null(char buffer[BUFFER_SIZE]);
enum order_state get_command(char buffer[BUFFER_SIZE], trader *current_trader,
                                product_order **orderbook, int num_products);
int64_t check_order_match(enum order_state cmd, char buffer[BUFFER_SIZE],
                            trader *current_trader, product_order **orderbook,
                            int num_products);
void respond_to_trader(int order_id, trader *current_trader, enum order_state cmd);
void notify_all_traders(enum order_state cmd, order *new_order,
                        trader *skip_trader, trader **traders,
                        product_order **orderbook, int num_products,
                        int num_traders);
void free_all(char **e2t_pipenames, char **t2e_pipenames, char **products,
                int num_products, trader **traders,int num_traders);
void free_linked_list(order *head);
void free_orderbook(product_order **orderbook, int num_products);
void init_orderbook(product_order **orderbook, char **products,
                        int num_products);
order *get_total_quantity(order *list, int *quantity_ptr,
                            int *num_orders_ptr, enum order_type type);
int get_num_levels(order *head);
void test_print_orders(order *head, enum order_type type);
void print_orders(order *head, enum order_type type);
void print_orderbook(product_order **orderbook, int num_products);
position *init_new_position(char *product_name);
position *get_positions(char **products, int num_products);
void load_positions(trader **traders, int num_traders,
                    char **products, int num_products);
trader **launch_traders(char *trader_filenames[BUFFER_SIZE],
                        char **e2t_pipenames, char **t2e_pipenames,
                        int num_traders, char **products, int num_products,
                        char *testing_filename);
void print_positions(trader **traders, int num_traders);
void disconnect_trader(trader *current_trader);
void respond_invalid(trader *current_trader);
void init_queue();
void print_trader(int trader_id);
void print_trader_files(int num_traders);
void send_traders_all_pids(trader **traders, int num_traders);
int send_signal_to_all_traders(trader **traders, int num_traders, int signal);
#endif
