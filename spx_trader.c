#include "spx_common.h"
#include "spx_trader.h"

// Global counter that keeps track of received SIGUSR1 signals
static volatile int sigusr1_count = 0;

// Wrapper function for calloc
void *my_calloc(size_t count, size_t size) {
    void *ptr = calloc(count, size);
    return ptr;
}

// Wrapper function for free
void my_free(void *ptr) {
    free(ptr);
}

// SIGUSR1 signal handler
void sighandler(int signo, siginfo_t* sinfo, void* context) {
    __sync_fetch_and_add(&sigusr1_count, 1);
}

// Unlinks the named pipes
void unlink_pipes(int trader_id) {
    char exchange_to_trader[BUFFER_SIZE] = "";
    sprintf(exchange_to_trader, FIFO_EXCHANGE, trader_id);
    unlink(exchange_to_trader);

    char trader_to_exchange[BUFFER_SIZE] = "";
    sprintf(trader_to_exchange, FIFO_TRADER, trader_id);
    unlink(trader_to_exchange);
}

// Connects the named pipes
int *connect_pipes(int trader_id) {
    int *fds = my_calloc(2, sizeof(int));
    char exchange_to_trader[BUFFER_SIZE] = "";
    sprintf(exchange_to_trader, FIFO_EXCHANGE, trader_id);

    char trader_to_exchange[BUFFER_SIZE] = "";
    sprintf(trader_to_exchange, FIFO_TRADER, trader_id);

    int fd_e2t = open(exchange_to_trader, O_RDONLY);
    int fd_t2e = open(trader_to_exchange, O_WRONLY);

    if ((-1 == fd_e2t) || (-1 == fd_t2e)) {
        printf("Error: could not connect pipes for trader %d\n", trader_id);
        return NULL;
    }

    fds[0] = fd_e2t;
    fds[1] = fd_t2e;

    return fds;
}

// Cleans up file descriptors and memory on the heap
void free_all(int fd_e2t, int fd_t2e, int trader_id, int *fds) {
    close(fd_t2e);
    close(fd_e2t);
    unlink_pipes(trader_id);
    my_free(fds);
}

// Frees the order struct
void free_order(order *current_order) {
    if (NULL != current_order->product_name) {
        my_free(current_order->product_name);
    }
    my_free(current_order);
}

// Creates a order struct that stores: order type, product name, price, quantity
order *init_buy_order(char buffer[BUFFER_SIZE]) {
    order *new_order = my_calloc(1, sizeof(order));
    char tmp[BUFFER_SIZE] = {0};
    strcpy(tmp, buffer);

    strtok(tmp, " ");
    char *order_type = strtok(NULL, " ");
    char *product_name= strtok(NULL, " ");
    int quantity = atoi(strtok(NULL, " "));
    int price = atoi(strtok(NULL, " "));

    if (0 != strcmp(order_type, "SELL")) {
        my_free(new_order);
        return NULL;
    }

    new_order->product_name = my_calloc(strlen(product_name) + 1, sizeof(char));
    strcpy(new_order->product_name, product_name);

    new_order->type = BUY;
    new_order->quantity = quantity;
    new_order->price = price;

    return new_order;
}


// Takes in an order struct and sends the opposite order
// eg. Receives BUY, sends out SELL (with same price and quantity)
int send_opposite_order(int order_id, order *new_order, int fd_t2e) {
    char response[BUFFER_SIZE] = {0};
    sprintf(response, "BUY %d %s %d %d;", order_id, new_order->product_name,
            new_order->quantity, new_order->price);
    free_order(new_order);

    if (-1 == write(fd_t2e, response, strlen(response))) {
        #ifdef DEBUG
            printf("Error in send_opposite_order(): \
                    write returned -1, errno: %s (%d)\n", strerror(errno), errno);
        #endif
        return -1;
    }

    if (0 != kill(getppid(), SIGUSR1)) {
        printf("Error in send_opposite_order(): \
                errno: %s (%d)\n", strerror(errno), errno);
        return -1;
    }

    return 0;
}

bool is_market_sell(char buffer[BUFFER_SIZE]) {
    char *str = "MARKET SELL";
    return (0 == strncmp(buffer, str, strlen(str)));
}

// Sends a message to the exchange
int send_message_to_exchange(char *message, int fd_t2e) {
    if (-1 == write(fd_t2e, message, strlen(message))) {
        #ifdef DEBUG
            printf("Error in send_message_to_exchange(): write returned -1, \
                    errno: %s (%d)\n", strerror(errno), errno);
        #endif
        return -1;
    }

    if (0 != kill(getppid(), SIGUSR1)) {
        #ifdef DEBUG
            printf("Error in send_message_to_exchange(): \
                    errno: %s (%d)\n", strerror(errno), errno);
        #endif
        return -1;
    }

    return 0;
}

int send_signal(pid_t pid, int signal) {
    if (0 != kill(pid, signal)) {
        printf("Error:, errno: %s (%d)\n", strerror(errno), errno);
        return -1;
    }
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Not enough arguments\n");
        return 1;
    }

    // Register signal handler
    struct sigaction sig = {0};
    sig.sa_sigaction = sighandler;
    sig.sa_flags = SA_SIGINFO | SA_RESTART;
    sigaction(SIGUSR1, &sig, NULL);

    // Register signal mask
    sigset_t oldmask;
    sigemptyset(&oldmask);
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR1);

    // Get trader id
    int trader_id = atoi(argv[1]);

    // Connect to named pipes
    int *fds = connect_pipes(trader_id);
    if (NULL == fds) {
        return -1;
    }

    // Wait for market open
    while (0 == sigusr1_count) {
        nanosleep((const struct timespec[]){{0, TIME_250MS}}, NULL);
    }
    __sync_fetch_and_sub(&sigusr1_count, 1);

    int fd_e2t = fds[0];
    int fd_t2e = fds[1];

    char buffer[BUFFER_SIZE] = {0};

    // Get the message in the pipe
    read(fd_e2t, buffer, BUFFER_SIZE);
    char *market_open = "MARKET OPEN;";
    if (0 != strncmp(buffer, market_open, strlen(market_open))) {
        printf("Error: did not send correct message, wanted \"MARKET OPEN;\"");
        return -1;
    }

    memset(buffer, 0, BUFFER_SIZE);

    // Market has opened
    int order_id = 0;
    while (true) {
        while (0 == sigusr1_count) {
            nanosleep((const struct timespec[]){{0, TIME_250MS}}, NULL);
        }
        __sync_fetch_and_sub(&sigusr1_count, 1);


        // Read pipe up to the next ;
        sigprocmask(SIG_BLOCK, &mask, &oldmask);
        for (int i = 0; i < BUFFER_SIZE; i++) {
            read(fd_e2t, buffer + i, sizeof(char));
            if (';' == buffer[i]) {
                break;
            }
        }
        sigprocmask(SIG_UNBLOCK, &mask, NULL);

        // Check that the command is MARKET SELL
        if (!is_market_sell(buffer)) {
            memset(buffer, 0, BUFFER_SIZE);
            continue;
        }


        // Create an order struct using the buffer contents
        order *new_order = init_buy_order(buffer);
        if (new_order->quantity >= MAX_QUANTITY) {
            free_order(new_order);
            break;
        } else if (0 == new_order->quantity) {
            free_order(new_order);
            continue;
        }

        sigprocmask(SIG_BLOCK, &mask, &oldmask);
        send_opposite_order(order_id, new_order, fd_t2e);
        sigprocmask(SIG_UNBLOCK, &mask, NULL);

        // Wait for the ACCEPTED response
        bool accepted = false;
        while (!accepted) {
            // Continue to send a signal while it hasn't been ACCEPTED
            while (0 == sigusr1_count) {
                nanosleep((const struct timespec[]){{0, TIME_250MS}}, NULL);
                if (0 == sigusr1_count) {
                    send_signal(getppid(), SIGUSR1);
                }
            }

            __sync_fetch_and_sub(&sigusr1_count, 1);

            // Read response
            for (int i = 0; i < BUFFER_SIZE; i++) {
                read(fd_e2t, buffer + i, sizeof(char));
                if (';' == buffer[i]) {
                    break;
                }
            }

            // Check if the order has been accepted
            char expected_response[BUFFER_SIZE] = {0};
            sprintf(expected_response, "ACCEPTED %d;", order_id);
            if (0 == strncmp(expected_response, buffer,
                                strlen(expected_response))) {
                order_id += 1;
                accepted = true;
            }
        }

        // Reset buffer
        memset(buffer, 0, BUFFER_SIZE);

    }

    free_all(fd_e2t, fd_t2e, trader_id, fds);

    return 0;

}