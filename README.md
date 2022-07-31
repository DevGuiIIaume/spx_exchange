### How my exchange works

#### SETUP
Store command-line arguments into arrays. fork exchange, then exec (using trader filename argument) to replace process image with trader image. trader_filename and trader_id are arguments.

Open named pipes on both sides to ensure exchange2trader/trader2exchange communication.

#### COMMAND PROCESSING
Diagram: https://imgur.com/a/wowj5LP

Orderbook is linked-list of product_orders. Product_orders contain BUY/SELL linked-lists corresponding product.
-BUY arranged in descending order
-SELL arranged in ascending order
-Both maintain price-time priority

Main loop waits for a signal. Signals queued with PID of process that sent signal. Read named pipe of the corresponding trader, process the command.

BUY/SELL: initialise new_order, store in orderbook
AMEND: delete old_order, add new_order to orderbook
CANCEL: delete order from orderbook

If ordermatch, fill orders.

Exchange writes first, then sends SIGUSR1.

#### TEARDOWN
If the signal is SIGCHLD, we disconnect the trader and decrement count of connected traders. Exit when no more connected traders. Cleanup memory/named pipes.

### Explanation of my design decisions for the trader and how it's fault-tolerant

Diagram: https://imgur.com/a/G1PBGSm

SIGUSR1_sighandler is short (1 line). Global signal_count is *atomically* incremented using __sync_fetch_and_add. This makes the AT more fault-tolerant of rapid signals.

order_id stores number of accepted orders, hence next order_id.

Main loop waits for SIGUSR1. When SIGUSR1 is received, decrement sigurs1_count.

Read from exchange_to_trader pipe. Use signal mask to block further interrupts (fault-tolerance).

Ignore order if not MARKET SELL. If MARKET SELL, init order struct containing price, quantity, product name. break if quantity >= 1000.

Send opposite order (same values but BUY). Write to exchange_pipe send SIGUSR1 to exchange. Construct signal mask to block interrupts (fault-tolerance)

Nested while loop to periodically re-sends the SIGUSR1 until receive ACCEPTED. Busy exchanges can lose signals, this is fault-tolerance.

When we receive ACCEPTED, increment order_id, wait for the next SIGUSR1

### Descriptions of my tests and how to run them

To run the tests, simply run ./run_tests.
E2E tests all functionality, cmocka tests (linked-list) orderbook functionality and negative cases eg. invalid input to functions.

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
2. BUY//SELL/AMEND market orders
