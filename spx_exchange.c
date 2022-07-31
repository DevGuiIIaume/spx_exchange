/**
 * COMP2017 - Assignment 3
 * Guillaume Troadec
 * gtro3802
 */

#include "spx_exchange.h"

static volatile int num_current_traders = 0;
static queue *my_queue = NULL;

// Wrapper function for calloc
void *my_calloc(size_t count, size_t size) {
    void *ptr = calloc(count, size);
    return ptr;
}

// Wrapper function for free
void my_free(void *ptr) {
    free(ptr);
}

// Frees the memory associated with the order struct
void free_order(order *current_order) {
    if (NULL != current_order->product_name) {
        my_free(current_order->product_name);
    }
    my_free(current_order);
}

// Frees the memory on the heap that stores names of the named pipes
int free_pipenames(char **e2t_pipenames, char **t2e_pipenames, int size) {
    for (int i = 0; i < size; i++) {
        my_free(e2t_pipenames[i]);
        my_free(t2e_pipenames[i]);
    }
    my_free(e2t_pipenames);
    my_free(t2e_pipenames);

    return 0;
}

// Parses the command line arguments
// Loads in product filename, trader filenames, testing filename
// Returns the number of traders
int exchange_parse_args(int argc, char **argv, char *product_filename,
                            char **trader_filenames, char *testing_filename) {
    if (argc <= 2) {
        #ifdef DEBUG
            printf("Syntax: ./spx_exchange <product file> <traders>\n");
        #endif
        return -1;
    }

    strcpy(product_filename, argv[1]);

    int i = 0;
    #ifdef TESTING
        for (; i < argc-3; i++) {
            trader_filenames[i] = argv[i+2];
        }
        strcpy(testing_filename, argv[argc-1]);
    #else
       for (; i < argc-2; i++) {
           trader_filenames[i] = argv[i+2];
       }
    #endif

    return i;
}

// Generates the names of the named pipes based on trader id
int get_pipes(char **e2t_pipenames, char **t2e_pipenames, int num_traders) {
    char exchange_to_trader[BUFFER_SIZE] = "";
    char trader_to_exchange[BUFFER_SIZE] = "";

    for (int id = 0; id < num_traders; id++) {
        sprintf(exchange_to_trader, FIFO_EXCHANGE, id);
        e2t_pipenames[id] = my_calloc(strlen(exchange_to_trader) + 1,
                                        sizeof(char));
        strcpy(e2t_pipenames[id], exchange_to_trader);

        sprintf(trader_to_exchange, FIFO_TRADER, id);
        t2e_pipenames[id] = my_calloc(strlen(trader_to_exchange) + 1,
                                        sizeof(char));
        strcpy(t2e_pipenames[id], trader_to_exchange);

        memset(exchange_to_trader, 0, BUFFER_SIZE);
        memset(trader_to_exchange, 0, BUFFER_SIZE);
    }
    return 0;
}

// Launches the trader
trader *launch_trader(char *trader_filename, char *e2t_pipename,
                        char *t2e_pipename, int trader_id,
                        char *testing_filename) {
    mode_t mode = 0770;

    // Create the named pipes
    int res_e2t = mkfifo(e2t_pipename, mode);
    int res_t2e = mkfifo(t2e_pipename, mode);

    if ((-1 == res_e2t) || (-1 == res_t2e)) {
        printf("Error in launch_trader(): mkfifo returned -1, \
                errno: %s (%d)\n", strerror(errno), errno);
        return NULL;
    }

    printf("%s Created FIFO %s\n", LOG_PREFIX, e2t_pipename);
    printf("%s Created FIFO %s\n", LOG_PREFIX, t2e_pipename);

    // Initialise trader fields
    trader *current_trader = my_calloc(1, sizeof(trader));
    current_trader->trader_id = trader_id;
    current_trader->is_connected = true;
    current_trader->current_order_id = 0;
    current_trader->is_autotrader = (0 == strcmp(trader_filename,
                                        "./spx_trader"));

    int pid = fork();

    if (pid < 0) {
        printf("Error in launch_trader(): pid < 0\n");
        return NULL;
    } else if (0 == pid) {
        // Child
        char char_trader_id[16] = "";
        sprintf(char_trader_id, "%d", trader_id);

        printf("%s Starting trader %d (%s)\n", LOG_PREFIX, trader_id,
                trader_filename);

        // fork() exchange and then replace it with a trader image
        #ifdef TESTING
            char *char_array[]= {trader_filename, char_trader_id,
                                    testing_filename, NULL};
            if (-1 == execv(trader_filename, char_array)) {
                printf("Error in launch_trader(): execv returned -1, \
                        errno: %s (%d)\n", strerror(errno), errno);
            }
        #else
            char *char_array[] = {trader_filename, char_trader_id, NULL};
            if (-1 == execv(trader_filename, char_array)) {
                printf("Error in launch_trader(): execv returned -1, \
                        errno: %s (%d)\n", strerror(errno), errno);
            }
        #endif

        return NULL;
    } else {
        // Parent
        current_trader->pid = pid;
        current_trader->e2t_fd_wronly = open(e2t_pipename, O_WRONLY);
        current_trader->t2e_fd_rdonly = open(t2e_pipename, O_RDONLY);

        printf("%s Connected to %s\n", LOG_PREFIX, e2t_pipename);
        printf("%s Connected to %s\n", LOG_PREFIX, t2e_pipename);

        return current_trader;
    }
}

// Unlinks the pipes
void unlink_pipes(char **e2t_pipenames, int size) {
    for (int i = 0; i < size; i++) {
        unlink(e2t_pipenames[i]);
    }
}

// Opens the market
int open_market(trader **traders, int num_traders) {
    char *market_open = "MARKET OPEN;";

    // Writes to all the named pipes
    for (int i = 0; i < num_traders; i++) {
        trader *current_trader = traders[i];
        if (-1 == write(current_trader->e2t_fd_wronly, market_open,
                            strlen(market_open))) {
            printf("Error in open_market(): write returned -1, \
                    errno: %s (%d)\n", strerror(errno), errno);
        }
    }

    // Send SIGUSR1 to every trader
    for (int i = 0; i < num_traders; i++) {
        trader *current_trader = traders[i];
        if (0 != kill(current_trader->pid, SIGUSR1)) {
            printf("Error in open_market(): kill returned -1, \
                    errno: %s (%d)\n", strerror(errno), errno);
        }
    }

    return 0;
}

// Search for the order that with the order id associated with the current trader
order *search_orders(trader *current_trader, int order_id, order *orders) {
    order *cursor = orders;
    while (NULL != cursor) {
        bool equal_order_ids = (cursor->order_id == order_id);
        bool equal_pids = (cursor->owner->pid == current_trader->pid);
        if (equal_order_ids && equal_pids) {
            return cursor;
        }
        cursor = cursor->next;
    }
    return NULL;
}

// Search through the buy and sell orders of every product for the current trader's
// order with the matching order id
order *search_orderbook(trader *current_trader, int order_id,
                        product_order **orderbook, int num_products) {

    // Search through every product
    for (int i = 0; i < num_products; i++) {
        product_order *current_product = orderbook[i];

        order *buy_order = search_orders(current_trader, order_id,
                                            current_product->buy_orders);
        order *sell_order = search_orders(current_trader, order_id,
                                            current_product->sell_orders);

        if (NULL != buy_order) {
            return buy_order;
        } else if (NULL != sell_order) {
            return sell_order;
        }
    }

    return NULL;
}

// Enqueue a node at the head
// The node contains the pid of the trader that sent SIGUSR1
void enqueue(queue *my_queue, int pid, int signal_type) {
    node *new_node = my_calloc(1, sizeof(node));
    new_node->pid = pid;
    new_node->signal = signal_type;

    node *head = my_queue->head;
    if (NULL == head) {
        new_node->next = NULL;
        new_node->prev = NULL;
    } else {
        new_node->next = head;
        new_node->prev = NULL;
        head->prev = new_node;
    }

    my_queue->size += 1;
    my_queue->head = new_node;
}

// Dequeue the node from the tail
node *dequeue(queue *my_queue) {
    node *head = my_queue->head;
    if (NULL == head) {
        return NULL;
    }

    node *cursor = my_queue->head;

    // Traverse to the tail node
    while (NULL != cursor->next) {
        cursor = cursor->next;
    }

    // Remove the tail node
    bool reset = true;
    if (NULL != cursor->prev) {
        cursor->prev->next = NULL;
        reset = false;
    }

    if (reset) {
        my_queue->head = NULL;
    }

    my_queue->size -= 1;
    return cursor;
}

void sigusr1_handler(int signo, siginfo_t* sinfo, void* context) {
    enqueue(my_queue, sinfo->si_pid, SIGUSR1);
}

void sigchild_handler(int signo, siginfo_t* sinfo, void* context) {
    enqueue(my_queue, sinfo->si_pid, SIGCHLD);
}

// Gets the information of the products from the product file
char **get_products(char *product_filename, int *num_products_ptr) {
    FILE *fp = fopen(product_filename, "r");

    char buffer[BUFFER_SIZE] = {0};
    fgets(buffer, BUFFER_SIZE, fp);

    *num_products_ptr = atoi(buffer);

    if (*num_products_ptr < 0) {
        return NULL;
    }

    char **products = my_calloc(*num_products_ptr, sizeof(char *));

    // Store each product name in a array of strings
    for (int i = 0; i < *num_products_ptr; i++) {
        memset(buffer, 0, BUFFER_SIZE);

        if (NULL == fgets(buffer, BUFFER_SIZE, fp)) {
            #ifdef DEBUG
                printf("Error: missing lines in product file\n");
            #endif
        }

        // Strip trailing newline
        char *ptr = strchr(buffer, '\n');
        if (ptr) {
            *ptr = '\0';
        }

        // Allocate memory and store within the array
        products[i] = my_calloc(strlen(buffer) + 1, sizeof(char));
        strcpy(products[i], buffer);

    }

    return products;
}

// Frees the memory associated with a 2d char array
void free_2d_char_array(char **array, int size) {
    for (int i = 0; i < size; i++) {
        my_free(array[i]);
    }

    my_free(array);
}

// Print out the products
void print_products(char **products, int num_products) {
    printf("%s Trading %d products:", LOG_PREFIX, num_products);
    int i = 0;
    for (; i < num_products-1; i++) {
        printf(" %s", products[i]);
    }
    printf(" %s\n", products[i]);
}

// Free the memory on the heap associated with the trader
void free_trader(trader *current_trader) {
    position *head = current_trader->positions;
    while (NULL != head) {
        position *tmp = head;
        head = head->next;
        my_free(tmp);
    }
}

// Free the memory on the heap associated with all traders
void free_traders(trader **traders, int size) {
    for (int i = 0; i < size; i++) {
        close(traders[i]->e2t_fd_wronly);
        close(traders[i]->t2e_fd_rdonly);
        free_trader(traders[i]);
        my_free(traders[i]);
    }
    my_free(traders);
}

// Given a pid, get the corresponding trader struct
trader *get_trader_id(trader **traders, int num_traders, int pid) {
    for (int i = 0; i < num_traders; i++) {
        if (pid == traders[i]->pid) {
            return traders[i];
        }
    }
    return NULL;
}

// Creates an order struct that stores the associated information from the buffer
order *init_amended_order(char buffer[BUFFER_SIZE],
                            trader *current_trader, order *old_order) {

    char tmp[BUFFER_SIZE] = {0};
    strcpy(tmp, buffer);

    // Get the values from the buffer
    order *new_order = my_calloc(1, sizeof(order));
    strtok(tmp, " ");
    int order_id = atoi(strtok(NULL, " "));
    int quantity = atoi(strtok(NULL, " "));
    int price = atoi(strtok(NULL, " "));

    if (price < 0 || quantity < 0 || (old_order->order_id != order_id)) {
        #ifdef DEBUG
            printf("Error in init_amended_order(): \
                    price and/or quantity < 0 and/or order id not equal\n");
        #endif
        free_order(new_order);
        return NULL;
    }

    // Initialise trader fields
    new_order->product_name = my_calloc(strlen(old_order->product_name) + 1,
                                        sizeof(char));
    strcpy(new_order->product_name, old_order->product_name);

    new_order->type = old_order->type;
    new_order->amended = true;

    new_order->owner = current_trader;
    new_order->order_id = order_id;
    new_order->quantity = quantity;
    new_order->price = price;

    return new_order;
}

// Creates an order struct that stores the associated information from the buffer
order *init_new_order(enum order_state cmd, char buffer[BUFFER_SIZE],
                        trader *current_trader, enum order_type type) {

    char tmp[BUFFER_SIZE] = {0};
    strcpy(tmp, buffer);

    order *new_order = my_calloc(1, sizeof(order));

    // Get the values from the buffer
    strtok(tmp, " ");
    int order_id = atoi(strtok(NULL, " "));
    char *product_name = strtok(NULL, " ");
    int quantity = atoi(strtok(NULL, " "));
    int price = atoi(strtok(NULL, " "));

    if (price < 0 || quantity < 0) {
        #ifdef DEBUG
            printf("Error in init_new_order(): price and/or quantity < 0\n");
        #endif
        free_order(new_order);
        return NULL;
    }

    // Initialise order fields
    new_order->product_name = my_calloc(strlen(product_name) + 1, sizeof(char));
    strcpy(new_order->product_name, product_name);

    new_order->type = type;
    new_order->amended = false;

    new_order->owner = current_trader;
    new_order->order_id = order_id;
    new_order->quantity = quantity;
    new_order->price = price;

    return new_order;
}

// Insert a new order into the linked list of orders
order *insert_linked_list(order *head, order *cursor, order *new_order) {
    if (NULL == cursor) {
        // Travel to the tail
        order *tmp = head;
        while (NULL != tmp->next) {
            tmp = tmp->next;
        }

        // Insert at the tail
        tmp->next = new_order;
        new_order->prev = tmp;
        new_order->next = NULL;

        return head;
    } else if (NULL == cursor->prev) {
        // Insert at the head
        cursor->prev = new_order;
        new_order->next = cursor;
        new_order->prev = NULL;
        return new_order;
    } else {
        // Insert in the middle
        new_order->prev = cursor->prev;
        new_order->next = cursor;
        (cursor->prev)->next = new_order;
        cursor->prev = new_order;
        return head;
    }
}

// Inserts new order into the BUY or SELL linked list
// BUY is arranged in descending order
// SELL is arranged in ascending order
order *insert_order(order *head, order *new_order, enum order_type cmd) {
    if (NULL == head) {
        return new_order;
    }

    order *cursor = head;
    while (NULL != cursor) {
        if ((BUY == cmd) && (new_order->price > cursor->price)) {
            // Stop once reach a lower priced order
            break;
        } else if ((SELL == cmd) && (new_order->price < cursor->price)) {
            // Stop once reach a higher priced order
            break;
        }
        cursor = cursor->next;
    }

    // Insert the order within the linked list
    order *new_head = insert_linked_list(head, cursor, new_order);;

    return new_head;
}

// Get the product struct corresponding to the product name
// The product struct stores the BUY and SELL orders associated with the name
product_order *get_product_from_orderbook(product_order **orderbook,
                                            char *product_name,
                                            int num_products) {

    for (int i = 0; i < num_products; i++) {
        if (0 == strcmp(orderbook[i]->product_name, product_name)) {
            return orderbook[i];
        }
    }
    return NULL;
}

// Delete the order within the linked list
order *delete_order(order *current_order, order *head) {
    if (NULL == head) {
        return NULL;
    }

    if (NULL == current_order->prev && NULL == current_order->next) {
        // Only node in the linked list
        free_order(current_order);
        return NULL;
    } else if (NULL == current_order->prev && NULL != current_order->next) {
        // Head node in the linked list
        order *tmp = current_order->next;
        tmp->prev = NULL;
        free_order(current_order);
        return tmp;
    } else if (NULL != current_order->prev && NULL == current_order->next) {
        // Tail node
        order *tmp = current_order->prev;
        tmp->next = NULL;
        free_order(current_order);
        return head;
    } else if (NULL != current_order->prev && NULL != current_order->next) {
       // Middle node
       (current_order->next)->prev = current_order->prev;
       (current_order->prev)->next = current_order->next;
       free_order(current_order);
       return head;
   } else {
       return NULL;
   }
}

// Get the trader's position on the input product
position *get_position(trader *current_trader, char *product_name) {
    position *head = current_trader->positions;

    // Search the positions for the position corresponding to the product
    while (NULL != head) {
        if (0 == strcmp(head->product_name, product_name)) {
            return head;
        }
        head = head->next;
    }
    return NULL;
}

// Notify the trader that their order has been filled
void fill_notify_trader(order *current_order, int quantity) {
    trader *current_trader = current_order->owner;
    if (!current_trader->is_connected) {
        return;
    }

    char response[BUFFER_SIZE] = {0};
    sprintf(response, "FILL %d %d;", current_order->order_id, quantity);

    // Write to the trader
    if (-1 == write(current_trader->e2t_fd_wronly, response, strlen(response))) {
        printf("Error in fill_notify_trader(): write returned -1, \
                errno: %s (%d)\n", strerror(errno), errno);
    }


    // Send the trader SIGUSR1
    if (0 != kill(current_trader->pid, SIGUSR1)) {
        printf("Error in fill_notify_trader(): kill returned -1, \
                errno: %s (%d)\n", strerror(errno), errno);
    }
}

// Notify the traders that their orders have been filled
void fill_notify_traders(order *buy_order, order *sell_order, int quantity) {
    fill_notify_trader(buy_order, quantity);
    nanosleep((const struct timespec[]){{0, TIME_100MS}}, NULL);
    fill_notify_trader(sell_order, quantity);
}


// Calculate the fee associated with a trade
int64_t calculate_fee(int64_t value) {
    if (value < INT_MAX) {
    return (int64_t) round(((double) value * FEE_PERCENTAGE) / 100);
    }

    // Calculate the rounded value for large numbers
    // eg. to the nearest 100 when FEE is 1%
    int reciprocal = round(100 / (double) FEE_PERCENTAGE);
    int64_t mod = value % reciprocal;
    int64_t result = 0;
    if (mod >= reciprocal / 2) {
        result = (value + (reciprocal - mod)) / reciprocal;
    } else {
        result = (value - mod) / reciprocal;
    }
    return result;
}

int64_t calculate_value(int64_t quantity, int64_t price) {
    return (int64_t) quantity * (int64_t) price;
}

// Updates the current trader's position
void update_trader_position(order *current_order, int64_t final_value,
                            int64_t final_quantity) {

    position *current_position = get_position(current_order->owner,
                                                current_order->product_name);
    if (NULL == current_position) {
        printf("Error in update_trader_position(): position not found\n");
    }

    current_position->value += final_value;
    current_position->quantity += final_quantity;
}

void update_trader_positions(order *matched_order, order *new_order,
                            int64_t total_value, int64_t fee, int quantity) {
    if (BUY == new_order->type) {
        // We are in fill_buy_order()

        // Matched order is the SELL order
        // hence we are PAID to DECREASE our quantity
        update_trader_position(matched_order, total_value, -1 * quantity);

        // New order is the BUY order
        // hence we PAY (plus fees) to INCREASE our quantity
        update_trader_position(new_order, -1 * (total_value + fee), quantity);

    } else {
        // We are in fill_sell_order()

        // Matched order is the BUY order
        // hence we PAY to INCREASE our quantity
        update_trader_position(matched_order, -1 * total_value, quantity);

        // New order is the SELL order
        // hence we are PAID (minus fees) to DECREASE our quantity
        update_trader_position(new_order, total_value - fee, -1 * quantity);

    }
}

// Removes the current order from the orderbook
void remove_order_from_orderbook(order *current_order,
                                    product_order **orderbook, int num_products) {
    product_order *current_product = get_product_from_orderbook(orderbook,
                                    current_order->product_name, num_products);

    if (BUY == current_order->type) {
        current_product->buy_orders = delete_order(current_order,
                                                    current_product->buy_orders);
        current_product->buy_size -= 1;

    } else {
        current_product->sell_orders = delete_order(current_order,
                                                    current_product->sell_orders);
        current_product->sell_size -= 1;
    }
}

// Updates the orderbook when a BUY order results in an order match
int64_t fill_buy_order(order *buy_order, product_order *product) {
    order *cursor = product->sell_orders;
    int64_t total_fee = 0;

    // Travel through the SELL orders of the product
    // Continue filling orders until either there are no more sell orders
    // Or the buy order is consumed (ie. quantity = 0)
    while (NULL != cursor) {
        if (buy_order->price < cursor->price) {
            break;
        } else if (NULL == product->sell_orders) {
            break;
        }

        bool consumed_buy_order = false;
        bool consumed_sell_order = false;

        int tmp_buy_quantity = buy_order->quantity;
        int tmp_sell_quantity = cursor->quantity;

        if (buy_order->quantity < cursor->quantity) {
            // Buy order is filled
            cursor->quantity -= buy_order->quantity;
            buy_order->quantity = 0;
            consumed_buy_order = true;
        } else if (buy_order->quantity > cursor->quantity) {
            // Sell order is filled
            buy_order->quantity -= cursor->quantity;
            cursor->quantity = 0;
            consumed_sell_order = true;
        } else {
            // Orders are equal in quantity -> both are filled
            buy_order->quantity = 0;
            cursor->quantity = 0;
            consumed_buy_order = true;
            consumed_sell_order = true;
        }

        order *tmp = cursor;
        cursor = cursor->next;

        if (consumed_sell_order) {
            // Calculate the fees
            int64_t value = calculate_value(tmp_sell_quantity, tmp->price);
            int64_t fee = calculate_fee(value);
            total_fee += fee;

            int order_id = tmp->order_id;
            int trader_id = tmp->owner->trader_id;

            printf("%s Match: Order %d [T%d], New Order %d [T%d], value: $%lld, fee: $%lld.\n",
            LOG_PREFIX, order_id, trader_id,
            buy_order->order_id, buy_order->owner->trader_id, value, fee);

            update_trader_positions(tmp, buy_order, value, fee, tmp_sell_quantity);
            fill_notify_traders(buy_order, tmp, tmp_sell_quantity);

            // Update the orderbook
            product->sell_orders = delete_order(tmp, product->sell_orders);
            product->sell_size -= 1;
        }

        if (consumed_buy_order && consumed_sell_order) {
            product->buy_orders = delete_order(buy_order, product->buy_orders);
            product->buy_size -= 1;
            break;
        }

        if (consumed_buy_order) {
            // Calculate the fees
            int64_t value = calculate_value(tmp_buy_quantity, tmp->price);
            int64_t fee = calculate_fee(value);
            total_fee += fee;

            int order_id = buy_order->order_id;
            int trader_id = buy_order->owner->trader_id;

            printf("%s Match: Order %d [T%d], New Order %d [T%d], value: $%lld, fee: $%lld.\n",
            LOG_PREFIX, tmp->order_id,
            tmp->owner->trader_id, order_id, trader_id, value, fee);

            update_trader_positions(tmp, buy_order, value, fee, tmp_buy_quantity);
            fill_notify_traders(buy_order, tmp, tmp_buy_quantity);

            // Update the orderbook
            product->buy_orders = delete_order(buy_order, product->buy_orders);
            product->buy_size -= 1;
            break;
        }
    }
    return total_fee;
}

// Updates the orderbook when a SELL order results in an order match
int64_t fill_sell_order(order *sell_order, product_order *product) {
    order *cursor = product->buy_orders;
    int64_t total_fee = 0;

    // Travel through the BUY orders of the product
    // Continue filling orders until either there are no more buy orders
    // Or the sell order is consumed (ie. quantity = 0)
    while (NULL != cursor) {
        if (cursor->price < sell_order->price) {
            break;
        } else if (NULL == product->buy_orders) {
            break;
        }

        bool consumed_sell_order = false;
        bool consumed_buy_order = false;

        int tmp_sell_quantity = sell_order->quantity;
        int tmp_buy_quantity = cursor->quantity;

        if (sell_order->quantity < cursor->quantity) {
            // Consume sell order
            cursor->quantity -= sell_order->quantity;
            sell_order->quantity = 0;
            consumed_sell_order = true;
        } else if (sell_order->quantity > cursor->quantity) {
            // Consume buy order
            sell_order->quantity -= cursor->quantity;
            cursor->quantity = 0;
            consumed_buy_order = true;
        } else {
            // Orders are equal in quantity -> both are filled
            cursor->quantity = 0;
            sell_order->quantity = 0;
            consumed_sell_order = true;
            consumed_buy_order = true;
        }

        order *tmp = cursor;
        cursor = cursor->next;

        if (consumed_buy_order) {
            // Calculate the new fee
            int64_t value = calculate_value(tmp_buy_quantity, tmp->price);
            int64_t fee = calculate_fee(value);
            total_fee += fee;

            int order_id = tmp->order_id;
            int trader_id = tmp->owner->trader_id;

            printf("%s Match: Order %d [T%d], New Order %d [T%d], value: $%lld, fee: $%lld.\n",
            LOG_PREFIX, order_id, trader_id,
            sell_order->order_id, sell_order->owner->trader_id, value, fee);

            // Update the positions of the owners of both matched orders
            update_trader_positions(tmp, sell_order, value, fee,
                                        tmp_buy_quantity);
            fill_notify_traders(tmp, sell_order, tmp_buy_quantity);

            // Remove the buy order from linked list
            product->buy_orders = delete_order(tmp, product->buy_orders);
            product->buy_size -= 1;
        }

        if (consumed_buy_order && consumed_sell_order) {
            product->sell_orders = delete_order(sell_order, product->sell_orders);
            product->sell_size -= 1;
            break;
        }

        if (consumed_sell_order) {
            // Calculate the new fee
            int64_t value = calculate_value(tmp_sell_quantity, tmp->price);
            int64_t fee = calculate_fee(value);
            total_fee += fee;

            // Remove sell order from the linked list
            int order_id = sell_order->order_id;
            int trader_id = sell_order->owner->trader_id;

            printf("%s Match: Order %d [T%d], New Order %d [T%d], value: $%lld, fee: $%lld.\n",
            LOG_PREFIX, tmp->order_id,
            tmp->owner->trader_id, order_id, trader_id, value, fee);

            // Update the positions of the owners of both matched orders
            update_trader_positions(tmp, sell_order, value, fee,
                                    tmp_sell_quantity);
            fill_notify_traders(tmp, sell_order, tmp_sell_quantity);

            // Remove the sell order from linked list
            product->sell_orders = delete_order(sell_order,
                                                product->sell_orders);
            product->sell_size -= 1;
            break;
        }
    }
    return total_fee;
}

// Returns whether there is a match of orders
bool is_order_match(product_order *product) {
    if (NULL == product->buy_orders || NULL == product->sell_orders) {
        return false;
    }
    return product->buy_orders->price >= product->sell_orders->price;
}

// Processes the commands written by the traders to the exchange
order *process_command(enum order_state cmd, char buffer[BUFFER_SIZE],
                        trader *current_trader, product_order **orderbook,
                        int num_products) {

    if (CANCELLED == cmd) {
        order *tmp_order = my_calloc(1, sizeof(order));
        char tmp[BUFFER_SIZE] = {0};
        strcpy(tmp, buffer);

        // Extract tokens from the string
        strtok(tmp, " ");
        int order_id = atoi(strtok(NULL, " "));

        tmp_order->order_id = order_id;
        tmp_order->owner = current_trader;
        tmp_order->type = CANCEL;
        return tmp_order;
    }

    order *new_order = NULL;

    if (AMENDED == cmd) {
        char tmp[BUFFER_SIZE] = {0};
        strcpy(tmp, buffer);

        // Extract tokens from the string
        strtok(tmp, " ");
        int order_id = atoi(strtok(NULL, " "));

        // Get the old order (and hence product name)
        order *old_order = search_orderbook(current_trader,
                                            order_id, orderbook, num_products);
        if (NULL == old_order) {
            #ifdef DEBUG
                printf("Error in process command\n");
            #endif
        }

        // Create an amended order
        new_order = init_amended_order(buffer, current_trader, old_order);

        // Remove the old order from the orderbook
        remove_order_from_orderbook(old_order, orderbook, num_products);
    } else {
        // Assign the fields to an order struct
        enum order_type type = (ACCEPTED_BUY == cmd) ? BUY : SELL;
        new_order = init_new_order(cmd, buffer, current_trader, type);
    }

    // Find which product is associated with the order
    product_order *product = get_product_from_orderbook(orderbook,
                                                        new_order->product_name,
                                                        num_products);
    if (NULL == product) {
        #ifdef DEBUG
            printf("Error in process_buy(): product does not exist\n");
        #endif
        free_order(new_order);
        return NULL;
    }

    // Insert the new order into the corresponding linked list
    order *new_head = NULL;
    if (BUY == new_order->type) {
        new_head = insert_order(product->buy_orders, new_order, BUY);
    } else {
        new_head = insert_order(product->sell_orders, new_order, SELL);
    }

    if (NULL == new_head) {
        #ifdef DEBUG
            printf("Error: insert_order returned NULL\n");
        #endif
    }

    // Update the linked list head pointers
    if (BUY == new_order->type) {
        product->buy_orders = new_head;
        product->buy_size++;
    } else {
        product->sell_orders = new_head;
        product->sell_size++;
    }

    return new_order;
}

// Process an AMEND command
int64_t process_amend(char buffer[BUFFER_SIZE], trader *current_trader,
                        product_order **orderbook, int num_products) {
    char tmp[BUFFER_SIZE] = {0};
    strcpy(tmp, buffer);

    strtok(tmp, " ");
    int order_id = atoi(strtok(NULL, " "));

    // Find the order that corresponds to the order id
    order *tmp_order = search_orderbook(current_trader, order_id,
                                        orderbook, num_products);
    // Get the corresponding product name
    product_order *product = get_product_from_orderbook(orderbook,
                                                        tmp_order->product_name,
                                                        num_products);

    int64_t fee = 0;
    // Check if there is an order match as a result of the amended order
    if (is_order_match(product)) {
        // If there is an order match, fill the orders
        if (BUY == tmp_order->type) {
            fee = fill_buy_order(product->buy_orders, product);
        } else {
            fee = fill_sell_order(product->sell_orders, product);
        }
    }

    return fee;
}

// Processes the SELL command
int64_t process_sell(char buffer[BUFFER_SIZE],
                        trader *current_trader, product_order **orderbook,
                        int num_products) {

    // Get the corresponding product from the orderbook
    order *tmp_order = init_new_order(ACCEPTED_SELL, buffer,
                                        current_trader, SELL);
    product_order *product = get_product_from_orderbook(orderbook,
                                                        tmp_order->product_name,
                                                        num_products);

    order *sell_order = product->sell_orders;
    int64_t fee = 0;

    // Check if there is an order match
    if (is_order_match(product)) {
        // Fill the orders
        fee = fill_sell_order(sell_order, product);
    }
    free_order(tmp_order);
    return fee;
}

// Processes the BUY command
int64_t process_buy(char buffer[BUFFER_SIZE], trader *current_trader,
                    product_order **orderbook, int num_products) {

    // Get the corresponding product from the orderbook
    order *tmp_order = init_new_order(ACCEPTED_BUY, buffer, current_trader, BUY);
    product_order *product = get_product_from_orderbook(orderbook,
                                                        tmp_order->product_name,
                                                        num_products);

    order *buy_order = product->buy_orders;
    int64_t fee = 0;

    // Check if there is an order match
    if (is_order_match(product)) {
        // Fill the orders
        fee = fill_buy_order(buy_order, product);
    }

    free_order(tmp_order);
    return fee;
}

// Process the CANCEL command
int process_cancel(char buffer[BUFFER_SIZE], trader *current_trader,
                    product_order **orderbook, int num_products) {
    char tmp[BUFFER_SIZE] = {0};
    strcpy(tmp, buffer);

    strtok(tmp, " ");
    int order_id = atoi(strtok(NULL, " "));

    // Get the order matching with the order id
    order *current_order = search_orderbook(current_trader, order_id,
                                            orderbook, num_products);
    if (NULL == current_order) {
        #ifdef DEBUG
            printf("Error in process cancel(): current_order is NULL\n");
        #endif
    }

    // Remove the order from the orderbook
    remove_order_from_orderbook(current_order, orderbook, num_products);
    return 0;
}

// Check that the command name is valid
bool is_valid_command_name(char buffer[BUFFER_SIZE]) {
    bool amend = (0 == strncmp("AMEND ", buffer, 6));
    bool cancel = (0 == strncmp("CANCEL ", buffer, 7));
    bool buy = (0 == strncmp("BUY ", buffer, 4));
    bool sell = (0 == strncmp("SELL ", buffer, 5));

    return (amend || cancel || buy || sell);
}

// Check that the product exists
bool is_valid_product(char buffer[BUFFER_SIZE], product_order **orderbook,
                        int num_products) {

    bool amend = (0 == strncmp("AMEND ", buffer, STRLEN_AMEND + 1));
    bool cancel = (0 == strncmp("CANCEL ", buffer, STRLEN_CANCEL + 1));
    if (amend || cancel) {
        return true;
    }

    char tmp[BUFFER_SIZE] = {0};
    strcpy(tmp, buffer);

    strtok(tmp, " ");
    strtok(NULL, " ");
    char *product_name = strtok(NULL, " ");

    // Search the orderbook for a match on the order product name
    for (int i = 0; i < num_products; i++) {
        if (0 == strcmp(product_name, orderbook[i]->product_name)) {
            return true;
        }
    }
    return false;
}

// Check that the order id is valid
bool is_valid_order_id(char buffer[BUFFER_SIZE], trader *current_trader) {
    bool cancel = (0 == strncmp("CANCEL ", buffer, STRLEN_CANCEL + 1));
    bool amend = (0 == strncmp("AMEND ", buffer, STRLEN_AMEND + 1));
    if (cancel || amend) {
        return true;
    }

    char tmp[BUFFER_SIZE] = {0};
    strcpy(tmp, buffer);

    strtok(tmp, " ");
    int order_id = atoi(strtok(NULL, " "));

    if (order_id > 999999) {
        return false;
    }

    return (current_trader->current_order_id == order_id);
}

// Check that the order referred to in AMEND/CANCEL exists
bool is_valid_order(char buffer[BUFFER_SIZE], trader *current_trader,
                    product_order **orderbook, int num_products) {
    bool buy = (0 == strncmp("BUY ", buffer, STRLEN_BUY + 1));
    bool sell = (0 == strncmp("SELL ", buffer, STRLEN_SELL + 1));
    if (buy || sell) {
        return true;
    }

    char tmp[BUFFER_SIZE] = {0};
    strcpy(tmp, buffer);

    strtok(tmp, " ");
    int order_id = atoi(strtok(NULL, " "));

    order *current_order = search_orderbook(current_trader, order_id,
                                            orderbook, num_products);

    if (NULL == current_order) {
        return false;
    } else {
        return true;
    }
}

// Check that the value in the order is valid ie. [1, 999999]
bool is_valid_value(char buffer[BUFFER_SIZE]) {
    bool cancel = (0 == strncmp("CANCEL ", buffer, STRLEN_CANCEL + 1));
    bool amend = (0 == strncmp("AMEND ", buffer, STRLEN_AMEND + 1));

    char tmp[BUFFER_SIZE] = {0};
    strcpy(tmp, buffer);

    int quantity = 0;
    int price = 0;

    // Extract tokens from the string
    if (cancel) {
        return true;
    } else if (amend) {
        strtok(tmp, " ");
        atoi(strtok(NULL, " "));
        quantity = atoi(strtok(NULL, " "));
        price = atoi(strtok(NULL, " "));
    } else {
        strtok(tmp, " ");
        atoi(strtok(NULL, " "));
        strtok(NULL, " ");
        quantity = atoi(strtok(NULL, " "));
        price = atoi(strtok(NULL, " "));
    }

    // Check that the quantity and price are valid
    if (quantity <= 0 || price <= 0) {
        return false;
    }  else if (quantity > 999999 || price > 999999) {
        return false;
    } else {
        return true;
    }
}

// Check the product name is alphanumeric
int check_product_name(char *buffer, enum order_state cmd) {
    int i = 0;
    while (isalnum(buffer[i])) {
        i++;
    }

    if (' ' == buffer[i]) {
        return i+1;
    } else {
        return -1;
    }
}

// Check the price is a numeric
int check_price(char *buffer, enum order_state cmd) {
    int i = 0;
    while (isdigit(buffer[i])) {
        i++;
    }

    if (';' == buffer[i]) {
        return i+1;
    } else {
        return -1;
    }
}

// Check the quantity is a numeric
int check_quantity(char *buffer, enum order_state cmd) {
    int i = 0;
    while (isdigit(buffer[i])) {
        i++;
    }

    if (' ' == buffer[i]) {
        return i+1;
    } else {
        return -1;
    }
}

// Check the order id numeric
int check_order_id(char *buffer, enum order_state cmd) {
    int i = 0;
    while (isdigit(buffer[i])) {
        i++;
    }

    if (CANCELLED == cmd && ';' == buffer[i]) {
        return i+1;
    } else if (CANCELLED != cmd && ' ' == buffer[i]) {
        return i+1;
    } else {
        return -1;
    }
}

// Check the command syntax is valid
bool is_valid_command_format(char buffer[BUFFER_SIZE]) {
    bool amend = (0 == strncmp("AMEND ", buffer, STRLEN_AMEND + 1));
    bool cancel = (0 == strncmp("CANCEL ", buffer, STRLEN_CANCEL + 1));
    bool buy = (0 == strncmp("BUY ", buffer, STRLEN_BUY + 1));
    bool sell = (0 == strncmp("SELL ", buffer, STRLEN_SELL + 1));

    int offset = 0;
    int result = 0;
    if (amend) {
        offset = STRLEN_AMEND + 1;
        result = check_order_id(buffer + offset, AMENDED);
        if (result <= 0) {
            return false;
        } else {
            offset += result;
        }

        result = check_quantity(buffer + offset, AMENDED);
        if (result <= 0) {
            return false;
        } else {
            offset += result;
        }

        result = check_price(buffer + offset, AMENDED);
        if (result <= 0) {
            return false;
        } else {
            offset += result;
        }

    } else if (cancel) {
        offset = STRLEN_CANCEL + 1;
        result = check_order_id(buffer + offset, CANCELLED);
        if (result <= 0) {
            return false;
        } else {
            offset += result;
        }

    } else if (buy) {
        offset = STRLEN_BUY + 1;
        result = check_order_id(buffer + offset, ACCEPTED_BUY);
        if (result <= 0) {
            return false;
        } else {
            offset += result;
        }

        result = check_product_name(buffer + offset, ACCEPTED_BUY);
        if (result <= 0) {
            return false;
        } else {
            offset += result;
        }

        result = check_quantity(buffer + offset, ACCEPTED_BUY);
        if (result <= 0) {
            return false;
        } else {
            offset += result;
        }

        result = check_price(buffer + offset, ACCEPTED_BUY);
        if (result <= 0) {
            return false;
        } else {
            offset += result;
        }
    } else if (sell) {
        offset = STRLEN_SELL + 1;
        result = check_order_id(buffer + offset, ACCEPTED_SELL);
        if (result <= 0) {
            return false;
        } else {
            offset += result;
        }

        result = check_product_name(buffer + offset, ACCEPTED_SELL);
        if (result <= 0) {
            return false;
        } else {
            offset += result;
        }

        result = check_quantity(buffer + offset, ACCEPTED_SELL);
        if (result <= 0) {
            return false;
        } else {
            offset += result;
        }

        result = check_price(buffer + offset, ACCEPTED_SELL);
        if (result <= 0) {
            return false;
        } else {
            offset += result;
        }

    } else {
        return false;
    }

    return true;
}

bool is_semicolon_delimitted(char buffer[BUFFER_SIZE]) {
    return (';' == buffer[strlen(buffer)-1]);
}

bool is_valid_command(char buffer[BUFFER_SIZE], trader *current_trader,
                        product_order **orderbook, int num_products) {
    if (!is_semicolon_delimitted(buffer)) {
        // Command is not ";" delimitted
        return false;
    } else if (!is_valid_command_name(buffer)) {
        // Invalid command name
        return false;
    } else if (!is_valid_command_format(buffer)) {
        // Invalid command format
        return false;
    } else if (!is_valid_value(buffer)) {
        // Invalid value (order id, quantity, price)
        return false;
    } else if (!is_valid_product(buffer, orderbook, num_products)) {
        // Product does not exist
        return false;
    } else if (!is_valid_order_id(buffer, current_trader)) {
        // Invalid order id
        return false;
    } else if (!is_valid_order(buffer, current_trader, orderbook, num_products)) {
        // Order does not exist (cannot amend or cancel)
        return false;
    }
    return true;
}

void replace_semicolon_with_null(char buffer[BUFFER_SIZE]) {
    int i = 0;
    while ('\0' != buffer[i]) {
        if (';' == buffer[i]) {
            buffer[i] = '\0';
        }
        i++;
    }
}

// Get the command from the buffer and return a corresponding enum
enum order_state get_command(char buffer[BUFFER_SIZE], trader *current_trader,
                                product_order **orderbook, int num_products) {
    char tmp[BUFFER_SIZE] = {0};
    strcpy(tmp, buffer);
    bool is_invalid = false;
    if (!is_valid_command(tmp, current_trader, orderbook, num_products)) {
        is_invalid = true;
    }

    buffer[strlen(buffer)-1] = '\0';
    replace_semicolon_with_null(buffer);

    printf("%s [T%d] Parsing command: <%s>\n", LOG_PREFIX,
            current_trader->trader_id, buffer);

    if (is_invalid) {
        return INVALID;
    }

    if (0 == strncmp("AMEND", buffer, STRLEN_AMEND)) {
        return AMENDED;
    } else if (0 == (strncmp("CANCEL", buffer, STRLEN_CANCEL))) {
        return CANCELLED;
    } else if (0 == strncmp("BUY", buffer, STRLEN_BUY)) {
        return ACCEPTED_BUY;
    } else if (0 == strncmp("SELL", buffer, STRLEN_SELL)) {
        return ACCEPTED_SELL;
    } else {
        return INVALID;
    }
}

// Check whether there is an order match
int64_t check_order_match(enum order_state cmd, char buffer[BUFFER_SIZE],
                            trader *current_trader, product_order **orderbook,
                            int num_products) {

    // Update the order id counter if the BUY/SELL order is valid
    if (AMENDED == cmd) {
        int64_t fee = process_amend(buffer, current_trader,
                                    orderbook, num_products);
        return fee;
    } else if (CANCELLED == cmd) {
        process_cancel(buffer, current_trader, orderbook, num_products);
        return 0;
    } else if (ACCEPTED_BUY == cmd) {
        int64_t fee = process_buy(buffer, current_trader, orderbook,
                                    num_products);
        current_trader->current_order_id += 1;
        return fee;
    } else if (ACCEPTED_SELL == cmd) {
        int64_t fee = process_sell(buffer, current_trader, orderbook,
                                    num_products);
        current_trader->current_order_id += 1;
        return fee;
    } else {
        #ifdef DEBUG
            printf("Error in check_order_match\n");
        #endif
        return 0;
    }
}

// Respond to the trader with the appropriate message
void respond_to_trader(int order_id, trader *current_trader,
                        enum order_state cmd) {
    // Ignore disconnected traders
    if (!current_trader->is_connected) {
        #ifdef DEBUG
            printf("Error in respond_to_trader(): trader is not connected\n");
        #endif
        return;
    }

    char response[BUFFER_SIZE] = {0};
    if (ACCEPTED_BUY == cmd) {
        sprintf(response, "ACCEPTED %d;", order_id);
    } else if (ACCEPTED_SELL == cmd) {
        sprintf(response, "ACCEPTED %d;", order_id);
    } else if (AMENDED == cmd) {
        sprintf(response, "AMENDED %d;", order_id);
    } else if (CANCELLED == cmd) {
        sprintf(response, "CANCELLED %d;", order_id);
    } else {
        #ifdef DEBUG
            printf("Error in respond_to_trader(): incorrect cmd enum\n");
        #endif
        return;
    }

    // Write response
    if (-1 == write(current_trader->e2t_fd_wronly, response, strlen(response))) {
        #ifdef DEBUG
            printf("Error: write returned -1, errno: %s (%d)\n",
                    strerror(errno), errno);
        #endif
    }

    // Send SIGUSR1
    if (0 != kill(current_trader->pid, SIGUSR1)) {
        #ifdef DEBUG
            printf("Error: kill returned -1, errno: %s (%d)\n",
                    strerror(errno), errno);
        #endif
    }
}

// Notify all the traders of MARKET events
void notify_all_traders(enum order_state cmd, order *new_order,
                        trader *skip_trader, trader **traders,
                        product_order **orderbook, int num_products,
                         int num_traders) {

    char response[BUFFER_SIZE] = "";
    if (ACCEPTED_BUY == cmd) {
        sprintf(response, "MARKET BUY %s %d %d;", new_order->product_name,
                new_order->quantity, new_order->price);
    } else if (ACCEPTED_SELL == cmd) {
        sprintf(response, "MARKET SELL %s %d %d;", new_order->product_name,
                new_order->quantity, new_order->price);
    } else if (CANCELLED == cmd) {
        order *old_order = search_orderbook(new_order->owner,
                                            new_order->order_id, orderbook,
                                            num_products);
        sprintf(response, "MARKET %s %s 0 0;",
                (BUY == old_order->type) ? "BUY" : "SELL",
                old_order->product_name);
    } else if (AMENDED == cmd) {
        sprintf(response, "MARKET %s %s %d %d;",
                (BUY == new_order->type) ? "BUY" : "SELL",
                new_order->product_name, new_order->quantity, new_order->price);
    }

    // Write to all the traders (excluding the trader that made the order)
    for (int i = 0; i < num_traders; i++) {
        trader *current_trader = traders[i];
        if (current_trader->trader_id == skip_trader->trader_id) {
            continue;
        } else if (!current_trader->is_connected) {
            continue;
        }
        if (-1 == write(current_trader->e2t_fd_wronly, response,
                        strlen(response))) {
            #ifdef DEBUG
                printf("Error: write returned -1, errno: %s (%d)\n",
                        strerror(errno), errno);
            #endif
        }
    }

    // Send a signal to all the traders (excluding the trader that made the order)
    for (int i = 0; i < num_traders; i++) {
        trader *current_trader = traders[i];
        if (current_trader->trader_id == skip_trader->trader_id) {
            continue;
        } else if (!current_trader->is_connected) {
            continue;
        }

        #ifdef TESTING
            nanosleep((const struct timespec[]){{0, TIME_100MS}}, NULL);
        #endif

        if (0 != kill(current_trader->pid, SIGUSR1)) {
            #ifdef DEBUG
                printf("Error: kill returned -1, errno: %s (%d)\n",
                        strerror(errno), errno);
            #endif
        }
    }
}

// Cleanup all memory and file descriptors
void free_all(char **e2t_pipenames, char **t2e_pipenames, char **products,
                int num_products, trader **traders,int num_traders) {
    unlink_pipes(e2t_pipenames, num_traders);
    unlink_pipes(t2e_pipenames, num_traders);
    free_pipenames(e2t_pipenames, t2e_pipenames, num_traders);
    free_2d_char_array(products, num_products);
    free_traders(traders, num_traders);
}

// Free the memory on the heap associated with the linked list
void free_linked_list(order *head) {
    while (NULL != head) {
        order *tmp = head;
        head = head->next;
        free_order(tmp);
    }
}

// Free the orderbook heap memory
void free_orderbook(product_order **orderbook, int num_products) {
    for (int i = 0; i < num_products; i++) {
        if (NULL != orderbook[i]->buy_orders) {
            free_linked_list(orderbook[i]->buy_orders);
        }
        if (NULL != orderbook[i]->sell_orders) {
            free_linked_list(orderbook[i]->sell_orders);
        }
        my_free(orderbook[i]);
    }
    my_free(orderbook);
}

// Initialise a new orderbook
void init_orderbook(product_order **orderbook, char **products,
                        int num_products) {
    for (int i = 0; i < num_products; i++) {
        char *current_product = products[i];

        product_order *new_product_order = my_calloc(1, sizeof(product_order));
        new_product_order->product_name = current_product;
        new_product_order->sell_orders = NULL;
        new_product_order->sell_size = 0;
        new_product_order->buy_orders = NULL;
        new_product_order->buy_size = 0;

        orderbook[i] = new_product_order;
    }
}

// Get the total quantity of orders at the current price
order *get_total_quantity(order *list, int *quantity_ptr, int *num_orders_ptr,
                            enum order_type type) {

    int current_price = list->price;

    order *cursor = list;
    while (NULL != cursor) {
        if (current_price != cursor->price) {
            break;
        }

        *quantity_ptr += cursor->quantity;
        *num_orders_ptr += 1;

        if (BUY == type) {
            cursor = cursor->next;
        } else {
            cursor = cursor->prev;
        }
    }

    return cursor;
}

// Get the number of BUY/SELL levels
int get_num_levels(order *head) {
    if (NULL == head) {
        return 0;
    }

    int counter = 1;
    int tmp_price = head->price;

    // Increment the counter when there is a new price level
    order *cursor = head->next;
    while (NULL != cursor) {
        if (tmp_price != cursor->price) {
            tmp_price = cursor->price;
            counter++;
        }
        cursor = cursor->next;
    }
    return counter;
}

// Print the orders at each BUY/SELL level
void print_orders(order *head, enum order_type type) {
    if (NULL == head) {
        return;
    }

    if (SELL == type) {
        while (NULL != head->next) {
            head = head->next;
        }
    }

    order *cursor = head;
    while (NULL != cursor) {
        int total_quantity = 0;
        int price = cursor->price;
        int num_orders = 0;
        char *order_type = (BUY == type) ? "BUY" : "SELL";
        order *new_cursor = get_total_quantity(cursor, &total_quantity,
                                                &num_orders, type);

        printf("%s\t\t%s %d @ $%d (%d order%s)\n", LOG_PREFIX, order_type,
                total_quantity, price, num_orders, (1 == num_orders) ? "" : "s");
        cursor = new_cursor;
    }
}

// Print out the orderbook
void print_orderbook(product_order **orderbook, int num_products) {
    printf("%s\t--ORDERBOOK--\n", LOG_PREFIX);

    // Iterate through all the products
    for (int i = 0; i < num_products; i++) {
        product_order *current_product = orderbook[i];
        int buy_levels = get_num_levels(current_product->buy_orders);
        int sell_levels = get_num_levels(current_product->sell_orders);

        printf("%s\tProduct: %s; Buy levels: %d; Sell levels: %d\n", LOG_PREFIX,
                current_product->product_name, buy_levels, sell_levels);

        // Print out the SELL orders, then the BUY orders
        print_orders(current_product->sell_orders, SELL);
        print_orders(current_product->buy_orders, BUY);
    }
}

// Initialise a new position
position *init_new_position(char *product_name) {
    position *new_position = my_calloc(1, sizeof(position));
    new_position->product_name = product_name;
    new_position->quantity = 0;
    new_position->value = 0;
    return new_position;
}

// Initialise all new positions
position *init_positions(char **products, int num_products) {
    if (0 == num_products) {
        return NULL;
    }

    position *tmp = init_new_position(products[0]);

    position *head = tmp;

    for (int i = 1; i < num_products; i++) {
        position *new_position = init_new_position(products[i]);
        new_position->prev = tmp;
        tmp->next = new_position;

        tmp = new_position;
    }

    return head;
}

void load_positions(trader **traders, int num_traders, char **products,
                    int num_products) {
    for (int i = 0; i < num_traders; i++) {
        trader *current_trader = traders[i];
        current_trader->positions = init_positions(products, num_products);
    }
}

// Launch the trader binaries
trader **launch_traders(char *trader_filenames[BUFFER_SIZE], char **e2t_pipenames,
                        char **t2e_pipenames, int num_traders, char **products,
                        int num_products, char *testing_filename) {

    // Store the traders in an array of traders
    trader **traders = my_calloc(num_traders, sizeof(trader));

    for (int i = 0; i < num_traders; i++) {
        trader *new_trader = launch_trader(trader_filenames[i], e2t_pipenames[i],
                                            t2e_pipenames[i], i, testing_filename);
        if (NULL != new_trader) {
            traders[i] = new_trader;
        } else {
            #ifdef DEBUG
                printf("Error: could not launch trader %d\n", i);
            #endif
        }
    }

    // Load the positions of all the traders
    load_positions(traders, num_traders, products, num_products);

    return traders;
}

// Print out the positions of each trader
void print_positions(trader **traders, int num_traders) {
    printf("%s\t--POSITIONS--\n", LOG_PREFIX);

    for (int i = 0; i < num_traders; i++) {
        printf("%s\tTrader %d: ", LOG_PREFIX, traders[i]->trader_id);
        position *head = traders[i]->positions;

        while (NULL != head->next) {
            printf("%s %lld ($%lld), ", head->product_name,
                    head->quantity, head->value);
            head = head->next;
        }

        printf("%s %lld ($%lld)\n", head->product_name,
                head->quantity, head->value);
    }
}

// Disconnect the trader
void disconnect_trader(trader *current_trader) {
    current_trader->is_connected = false;
}

// Write to the trader that their command is invalid
void respond_invalid(trader *current_trader) {
    if (!current_trader->is_connected) {
        return;
    }

    char *response = "INVALID;";
    if (-1 == write(current_trader->e2t_fd_wronly, response, strlen(response))) {
        #ifdef DEBUG
            printf("Error in fill_notify_trader(): write returned -1, \
                    errno: %s (%d)\n", strerror(errno), errno);
        #endif
    }

    if (0 != kill(current_trader->pid, SIGUSR1)) {
        #ifdef DEBUG
            printf("Error in fill_notify_trader(): kill returned -1, \
                    errno: %s (%d)\n", strerror(errno), errno);
        #endif
    }
}

void init_queue() {
    my_queue = my_calloc(1, sizeof(queue));
    my_queue->head = NULL;
    my_queue->size = 0;
}

int send_sigusr2_to_all_traders(trader **traders, int num_traders, int signal) {
    for (int i = 0; i < num_traders; i++) {
        trader *current_trader = traders[i];
        if (!current_trader->is_connected || current_trader->is_autotrader) {
            continue;
        }
        if (0 != kill(current_trader->pid, signal)) {
            printf("Error: kill returned -1, errno: %s (%d)\n",
                    strerror(errno), errno);
            return -1;
        }
    }
    return 0;
}

#ifndef UNIT_TEST
int main(int argc, char **argv) {
    char product_filename[BUFFER_SIZE] = {0};
    char testing_filename[BUFFER_SIZE] = {0};
    char *trader_filenames[BUFFER_SIZE] = {0};

    // Pass in the arguments to spx_exchange
    int num_traders = exchange_parse_args(argc, argv, product_filename,
                                            trader_filenames, testing_filename);
    if (-1 == num_traders) {
        return -1;
    }

    printf("%s Starting\n", LOG_PREFIX);

    // Register sighandler for SIGUSR1
    struct sigaction sigusr1;
    memset(&sigusr1, 0, sizeof(struct sigaction));
    sigusr1.sa_sigaction = sigusr1_handler;
    sigusr1.sa_flags = SA_SIGINFO | SA_RESTART;
    sigaction(SIGUSR1, &sigusr1, NULL);

    // Register sighandler for SIGCHLD
    struct sigaction sigchild;
    memset(&sigchild, 0, sizeof(struct sigaction));
    sigchild.sa_sigaction = sigchild_handler;
    sigchild.sa_flags = SA_SIGINFO | SA_RESTART;
    sigaction(SIGCHLD, &sigchild, NULL);

    // Get the products from the products file
    int num_products = -1;
    char **products = get_products(product_filename, &num_products);

    if (-1 == num_products) {
        return -1;
    }

    // Print out the products from the products file
    print_products(products, num_products);

    // Get the names of the named pipes for the traders
    char **e2t_pipenames = my_calloc(num_traders, sizeof(char *));
    char **t2e_pipenames = my_calloc(num_traders, sizeof(char *));
    if (-1 == get_pipes(e2t_pipenames, t2e_pipenames, num_traders)) {
        return -1;
    }

    unlink_pipes(e2t_pipenames, num_traders);
    unlink_pipes(t2e_pipenames, num_traders);

    // Initialise the orderbook
    product_order **orderbook = my_calloc(num_products, sizeof(product_order *));
    init_orderbook(orderbook, products, num_products);

    init_queue();

    int64_t exchange_fees_collected = 0;

    // Launch the traders
    trader **traders = launch_traders(trader_filenames, e2t_pipenames,
                                        t2e_pipenames, num_traders, products,
                                        num_products, testing_filename);

    sleep(1);

    char buffer[BUFFER_SIZE] = {0};
    num_current_traders = num_traders;

    // Open the market
    open_market(traders, num_traders);

    // MAIN PROGRAM LOOP
    while (num_current_traders > 0) {
        // Wait for SIGUSR1
        if (0 == my_queue->size) {
            pause();
        }

        node *new_signal = dequeue(my_queue);
        if (NULL == new_signal) {
            #ifdef DEBUG
                printf("Error: new_signal is NULL\n");
            #endif
            continue;
        }

        // Trader disconnection
        if (SIGCHLD == new_signal->signal) {
            trader *tmp = get_trader_id(traders, num_traders, new_signal->pid);
            printf("%s Trader %d disconnected\n", LOG_PREFIX, tmp->trader_id);
            disconnect_trader(tmp);
            num_current_traders--;
            my_free(new_signal);

            #ifdef TESTING
                nanosleep((const struct timespec[]){{0, TIME_500MS}}, NULL);
                send_sigusr2_to_all_traders(traders, num_traders, SIGUSR2);
            #endif
            continue;
        }

        // Trader wrote to the pipe (send SIGUSR1)
        trader *current_trader = get_trader_id(traders, num_traders,
                                                new_signal->pid);
        if (NULL == current_trader) {
            #ifdef DEBUG
                printf("Error: trader is NULL\n");
            #endif
            return -1;
        }

        my_free(new_signal);

        // Read from the pipe to ;
        for (int i = 0; i < BUFFER_SIZE; i++) {
            read(current_trader->t2e_fd_rdonly, buffer + i, sizeof(char));
            if (';' == buffer[i]) {
                break;
            }
        }

        // Determine whether the command is valid or not
        enum order_state cmd = get_command(buffer, current_trader, orderbook,
                                            num_products);
        if (INVALID == cmd) {
            respond_invalid(current_trader);
            #ifdef TESTING
                send_sigusr2_to_all_traders(traders, num_traders, SIGUSR2);
            #endif
            memset(buffer, 0, BUFFER_SIZE);
            continue;
        }

        // Process the (valid) command
        order *new_order = process_command(cmd, buffer, current_trader,
                                            orderbook, num_products);

        // Respond to trader
        respond_to_trader(new_order->order_id, current_trader, cmd);

        // Write market response to all pipes
        notify_all_traders(cmd, new_order, current_trader, traders,
                            orderbook, num_products, num_traders);

        if (CANCEL == new_order->type) {
            my_free(new_order);
        }

        // Check whether there is an order match, collect fees
        exchange_fees_collected += check_order_match(cmd, buffer, current_trader,
                                                        orderbook, num_products);

        print_orderbook(orderbook, num_products);
        print_positions(traders, num_traders);

        fflush(stdout);

        memset(buffer, 0, BUFFER_SIZE);

        #ifdef TESTING
            nanosleep((const struct timespec[]){{0, TIME_250MS}}, NULL);
            send_sigusr2_to_all_traders(traders, num_traders, SIGUSR2);
        #endif
    }

    printf("%s Trading completed\n", LOG_PREFIX); //
    printf("%s Exchange fees collected: $%lld\n", LOG_PREFIX,
            exchange_fees_collected);

    // Clean up memory, close pipes
    free_all(e2t_pipenames, t2e_pipenames, products, num_products,
                traders, num_traders);
    free_orderbook(orderbook, num_products);
    my_free(my_queue);

    sleep(1);

    fflush(stdout);

    return 0;
}

#endif