#ifndef SPX_COMMON_H
#define SPX_COMMON_H

#define _POSIX_C_SOURCE 200809L

#include <signal.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/errno.h>
#include <stdbool.h>
#include <time.h>

#define LOG_PREFIX "[SPX]"
#define FIFO_EXCHANGE "/tmp/spx_exchange_%d"
#define FIFO_TRADER "/tmp/spx_trader_%d"
#define FEE_PERCENTAGE 1
#define int64_t long long int
#define BUFFER_SIZE (1024)
#define TIME_250MS (250000000L)
#define TIME_100MS (100000000L)
#define TIME_500MS (500000000L)

typedef struct node node;
typedef struct queue queue;
typedef struct order order;
typedef struct trader trader;
typedef struct position position;

enum order_type {
    BUY = 0,
    SELL = 1,
    CANCEL = 2,
    AMEND = 3
};

// Stores the trader_id
struct trader {
    int trader_id;
    bool is_connected;
    bool is_autotrader;

    int current_order_id;

    pid_t pid;
    position *positions;
    int num_positions;

    int e2t_fd_wronly;
    int t2e_fd_rdonly;
};

struct position {
    char *product_name;
    int64_t quantity;
    int64_t value;

    position *next;
    position *prev;
};

struct node {
    int pid;
    int signal;

    node *next;
    node *prev;
};

struct queue {
    int size;
    node *head;

};

struct order {
    char *product_name;
    enum order_type type;

    bool amended;

    trader *owner;
    int order_id;

    int quantity;
    int price;

    order *next;
    order *prev;
};

#endif
