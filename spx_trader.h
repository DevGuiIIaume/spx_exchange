#ifndef SPX_TRADER_H
#define SPX_TRADER_H

#include "spx_common.h"
#include <stdatomic.h>

#define MAX_QUANTITY (1000)
#define SIZE (128)

typedef struct event event;

struct event {
    char *order_message;
    int trader_id;
    int time;
    bool received;

    event *prev;
    event *next;
};

void *my_calloc(size_t count, size_t size);
void my_free(void *ptr);
void sighandler(int signo, siginfo_t* sinfo, void* context);
void unlink_pipes(int trader_id);
int *connect_pipes(int trader_id);
void free_all(int fd_e2t, int fd_t2e, int trader_id, int *fds);
void free_order(order *current_order);
order *init_buy_order(char buffer[BUFFER_SIZE]);
int send_opposite_order(int order_id, order *new_order, int fd_t2e);
bool is_market_sell(char buffer[BUFFER_SIZE]);
int send_signal(pid_t pid, int signal);

#endif
