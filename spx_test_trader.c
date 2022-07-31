#include "spx_trader.h"

int num_traders = 0;
static volatile sig_atomic_t usr_interrupt = 0;
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

void sigusr1_sighandler(int signo, siginfo_t* sinfo, void* context) {
    __sync_fetch_and_add(&sigusr1_count, 1);
}

void sigusr2_sighandler(int signo, siginfo_t* sinfo, void* context) {
    __sync_fetch_and_add(&usr_interrupt, 1);
}

// Unlinks pipes
void unlink_pipes(int trader_id) {
    char exchange_to_trader[SIZE] = "";
    sprintf(exchange_to_trader, FIFO_EXCHANGE, trader_id);
    unlink(exchange_to_trader);

    char trader_to_exchange[SIZE] = "";
    sprintf(trader_to_exchange, FIFO_TRADER, trader_id);
    unlink(trader_to_exchange);
}

// Connects the pipes
int *connect_pipes(int trader_id) {
    int *fds = my_calloc(2, sizeof(int));

    // Generate the named pipe names
    char exchange_to_trader[SIZE] = "";
    sprintf(exchange_to_trader, FIFO_EXCHANGE, trader_id);

    char trader_to_exchange[SIZE] = "";
    sprintf(trader_to_exchange, FIFO_TRADER, trader_id);

    int fd_e2t = open(exchange_to_trader, O_RDONLY);
    int fd_t2e = open(trader_to_exchange, O_WRONLY);

    if ((-1 == fd_e2t) || (-1 == fd_t2e)) {
        printf("Error: could not connect pipes for trader %d. errno: %s (%d)\n",
                trader_id, strerror(errno), errno);
        return NULL;
    }

    fds[0] = fd_e2t;
    fds[1] = fd_t2e;

    return fds;
}

void free_event(event *tmp) {
    my_free(tmp->order_message);
    my_free(tmp);
}

void free_events(event *head) {
    while (NULL != head) {
        event *tmp = head;
        head = head->next;
        free_event(tmp);
    }
}

void free_all(int fd_e2t, int fd_t2e, int trader_id, int *fds) {
    close(fd_t2e);
    close(fd_e2t);
    my_free(fds);
    unlink_pipes(trader_id);
}

// Send the order to the exchange
int send_order(int t2e_fd, int exchange_pid, char *order) {
    if (-1 == write(t2e_fd, order, strlen(order))) {
        printf("Error in send_order(): write returned -1, errno: %s (%d)\n",
                strerror(errno), errno);
        return -1;
    }

    if (0 != kill(exchange_pid, SIGUSR1)) {
        printf("Error in send_order(): kill returned != 0, errno: %s (%d)\n",
                strerror(errno), errno);
        return -1;
    }

    return 0;
}

// Initialise an event struct
event *init_event(char *buffer, int trader_id, int time, bool received) {
    event *new_event = my_calloc(1, sizeof(event));

    new_event->order_message = my_calloc(strlen(buffer) + 1, sizeof(char));
    strcpy(new_event->order_message, buffer);

    new_event->trader_id = trader_id;
    new_event->time = time;
    new_event->received = received;

    return new_event;
}

void append_event(event *new_event, event *cursor) {
    cursor->next = new_event;
    new_event->prev = cursor;
    new_event->next = NULL;
}

// Iterate through the file and extract each line into an event struct
event *get_trader_events(char *filename) {
    FILE *fptr = fopen(filename, "r");
    if (NULL == fptr) {
        printf("Error: trader file (%s) does not exist!\n", filename);
        return NULL;
    }

    // Get the number of traders
    fscanf(fptr, "%d\n", &num_traders);

    char buffer[SIZE] = {0};

    int current_trader_id = -1;
    int idx = 1;

    // Iterate through the file
    event *head = NULL;
    event *cursor = NULL;
    while (EOF != fscanf(fptr, "[T%d] ", &current_trader_id)) {
        fgets(buffer, SIZE, fptr);
        buffer[strcspn(buffer, "\n")] = '\0';

        // Initialise a new event
        event *new_event = init_event(buffer, current_trader_id, idx, false);

        // Add it to the list of events
        if (1 == idx) {
            head = new_event;
        } else {
            append_event(new_event, cursor);
        }

        cursor = new_event;

        idx++;
    }

    fclose(fptr);
    return head;
}

void free_order(order *current_order) {
    if (NULL != current_order->product_name) {
        my_free(current_order->product_name);
    }
    my_free(current_order);
}

bool is_fill_order(char buffer[SIZE]) {
    return (0 == strncmp(buffer, "FILL", 4));
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Not enough arguments\n");
        return 1;
    }

    // Register signal handlers
    struct sigaction sigusr1;
    sigusr1.sa_sigaction = sigusr1_sighandler;
    sigusr1.sa_flags = SA_SIGINFO | SA_RESTART;
    sigaction(SIGUSR1, &sigusr1, NULL);

    struct sigaction sigusr2;
    sigusr2.sa_sigaction = sigusr2_sighandler;
    sigusr2.sa_flags = SA_SIGINFO | SA_RESTART;
    sigaction(SIGUSR2, &sigusr2, NULL);

    // Register masks for signals
    sigset_t oldmask, mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR2);
    sigaddset(&mask, SIGUSR1);

    // Get trader id
    int trader_id = atoi(argv[1]);
    int exchange_pid = getppid();
    char *test_filename = argv[2];

    event *events = get_trader_events(test_filename);

    if (NULL == events) {
        printf("Error: events is NULL\n");
    }

    // Connect to named pipes
    int *fds = connect_pipes(trader_id);

    if (NULL == fds) {
        return -1;
    }

    // Wait for exchange to open the market
    while (0 == sigusr1_count) {
        nanosleep((const struct timespec[]){{0, TIME_250MS}}, NULL);
    }

    sigusr1_count -= 1;

    // Extract file descriptors
    int e2t_fd = fds[0];
    int t2e_fd = fds[1];

    char buffer[SIZE] = {0};

    for (int i = 0; i < SIZE; i++) {
        read(e2t_fd, buffer + i, sizeof(char));
        if (';' == buffer[i]) {
            break;
        }
    }

    if (0 != strcmp(buffer, "MARKET OPEN;")) {
        printf("Error: did not send correct message, wanted MARKET OPEN; \
                got: %s", buffer);
        return -1;
    }

    // Main Trader Loop
    event *current_event = events;
    while (NULL != current_event) {

        // Send order message to trader
        if (trader_id == current_event->trader_id) {
            if (0 == strcmp(current_event->order_message, "DISCONNECT;")) {
                break;
            }
            // Send order
            send_order(t2e_fd, exchange_pid, current_event->order_message);
        }

        // Wait for market message/accepted
        while (0 == usr_interrupt) {
            nanosleep((const struct timespec[]){{0, TIME_250MS}}, NULL);
        }

        sigprocmask(SIG_BLOCK, &mask, &oldmask);
        usr_interrupt = 0;
        current_event = current_event->next;
        nanosleep((const struct timespec[]){{0, TIME_250MS}}, NULL);
        sigprocmask(SIG_UNBLOCK, &mask, NULL);
    }

    sigprocmask(SIG_BLOCK, &mask, &oldmask);
    free_events(events);
    free_all(e2t_fd, t2e_fd, trader_id, fds);
    sigprocmask(SIG_UNBLOCK, &mask, NULL);

    fflush(stdout);

    return 0;


}