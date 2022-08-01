### How my exchange works

#### SETUP
Store command-line arguments into arrays. fork the exchange, then exec (using trader filename argument) to replace the process image with the trader image. The trader_filename and trader_id are passed in as arguments.

Open named pipes on both sides to ensure exchange2trader/trader2exchange communication.

#### COMMAND PROCESSING
Diagram: https://imgur.com/a/wowj5LP

The orderbook is a linked-list of product_orders. Product_orders contain BUY/SELL linked-lists corresponding to the product.
- BUY is arranged in descending order
- SELL is arranged in ascending order
- Both maintain price-time priority

The main loop waits for a signal. Signals are queued, with the queue also storing the PID of the process that sent signal. When the signal is dequeued, read the named pipe of the corresponding trader, and process the command.

##### Commands
BUY/SELL: initialise new_order, store in orderbook
AMEND: delete old_order, add new_order to orderbook
CANCEL: delete order from orderbook

If there is an order-match, then fill the orders.

The exchange always writes to the pipe first, then sends SIGUSR1.

#### TEARDOWN
If the signal is SIGCHLD, we disconnect the trader and decrement count of connected traders. Exit when no more connected traders. Cleanup memory/named pipes.

### Explanation of my design decisions for the trader and how it's fault-tolerant.

Diagram: https://imgur.com/a/G1PBGSm

SIGUSR1_sighandler is short (1 line) to ensure faster execution and hence less time for race conditions to occur. The global signal_count is *atomically* incremented using __sync_fetch_and_add. This makes the auto-trader more fault-tolerant of rapid signals.

The main loop waits for SIGUSR1. When SIGUSR1 is received, decrement sigurs1_count and process the signal.

Read from the corresponding exchange_to_trader pipe. Use a signal mask to block further signal interrupts (fault-tolerance).

Ignore the order if is not MARKET SELL. If it is MARKET SELL, init order struct containing price, quantity, product name. Exit if the quantity >= 1000.

Send the opposite order (same values but BUY). Write to the exchange_pipe and send SIGUSR1 to exchange. Construct a signal mask to block further interrupts.

Use a nested while loop to periodically re-sends the SIGUSR1 until the auto-trader receives ACCEPTED from the exchange. Busy exchanges can lose signals, so this is another fault-tolerance mechanism.

When we receive ACCEPTED, increment the order_id, and wait for the next SIGUSR1.

### Descriptions of my tests and how to run them

To run the tests, simply run ./run_tests.
E2E tests all functionality, cmocka tests (linked-list) orderbook functionality, and negative cases eg. invalid input to functions.

#### ============ EXCHANGE E2E ============
BUY
1. BUY levels are correctly updated
2. multiple products, multiple BUY levels
3. new BUY order filled

SELL
1. SELL levels are correctly updated
2. multiple products, multiple SELL levels
3. new SELL order filled

AMEND
1. price-time priority (one order)
2. price-time priority (many orders)
3. amended orders can match matching and fill

CANCEL
1. cancelling BUY/SELL/AMEND orders

INVALID
1. non-existent products
2. non-existent orders
3. invalid price and quantity
4. invalid command (missing/extra values)
5. invalid command (excess whitespace)
6. invalid command (excess ; delmitter)

FILL
1. fill BUY orders from SELL order
2. fill SELL orders from BUY order

#### ============ AUTO-TRADER E2E ============
1. BUY/SELL market orders
2. BUY/SELL/AMEND market orders
