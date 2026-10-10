# SmartServe - Canteen Order Management System

**Your Campus. Your Cravings. One Smart Order.**

A college mini project (CSE - 2nd year DSA mini project).

* **Frontend:** HTML, CSS, plain JavaScript
* **Backend:** C language (portable HTTP server using POSIX sockets on Linux/Render)
* **DSA concept used:** exactly one - **Queue (FIFO)**
* **Storage:** a simple text file called `orders.txt`

---

## 1. What this project does

1. A customer opens the website, searches food, filters by category or budget (₹50 / ₹100 / ₹150) and adds items to a cart.
2. The customer fills the checkout form (name, ID, department, mobile, Dine-in/Takeaway, table number, notes).
3. The browser sends the order as JSON to the **C backend** with an HTTP request.
4. The C program validates the data, creates an **order ID** and **token**, and puts the order into the **Queue** (`enqueue`).
5. The order is saved in `orders.txt`.
6. The admin dashboard shows the waiting queue in FIFO order. When cooking starts, the C program removes the first order (`dequeue`).
7. The customer can track the order status: `Placed -> Preparing -> Ready -> Completed`.

**FIFO in one line:** the first customer who places an order is the first customer whose food starts cooking.

---

## 2. Folder structure (keep it exactly like this)

```
SmartServe/
|-- index.html        the web page (all sections)
|-- style.css         the design / colours / responsive layout
|-- script.js         cart, form validation, HTTP requests
|-- server.c          C backend + Queue implementation  (the DSA part)
|-- README.md         this file
|-- images/           12 real food photographs
|   |-- hero.jpg
|   |-- masala-dosa.jpg
|   |-- idli-sambar.jpg
|   |-- pongal.jpg
|   |-- meals.jpg
|   |-- chicken-biryani.jpg
|   |-- veg-fried-rice.jpg
|   |-- burger.jpg
|   |-- pizza.jpg
|   |-- french-fries.jpg
|   |-- cold-coffee.jpg
|   `-- lime-juice.jpg
|
|-- server.exe        (created after you compile - not in the ZIP)
`-- orders.txt        (created automatically at runtime - your database)
```

**No other folders are needed.** The Queue code lives inside `server.c` only.

---

## 3. What each file does

| File | Purpose |
|------|---------|
| `index.html` | Contains every section: navigation bar, hero, search bar, filters, food cards, FIFO explanation, token board, track order, admin dashboard, cart panel, checkout form and the confirmation receipt. |
| `style.css` | Makes the site look modern: colours, cards, shadows, hero background, token board, and media queries so it works on mobile. |
| `script.js` | Reads the menu from the C server, draws the food cards, handles the cart (add / + / -), validates the checkout form, sends the order with `fetch()`, draws the receipt, and refreshes the token board + admin dashboard every 3 seconds. |
| `server.c` | The C backend. It is a small HTTP server **plus** the Queue data structure. It has 5 API endpoints, validates orders, saves them in `orders.txt`, and prints the live queue on the console. |
| `images/` | Real photographs of the food (downloaded from Wikimedia Commons). |
| `orders.txt` | Created automatically. Every order is one line. This is the "database". |

---

## 4. How HTML, CSS, JavaScript and the C backend connect

```
 Browser (index.html + style.css)
        |
        |  script.js sends HTTP requests (fetch)
        |  GET  /api/menu          -> show food cards
        |  POST /api/order         -> place an order  (enqueue)
        |  GET  /api/order?id=3    -> track one order
        |  GET  /api/queue         -> token board + admin list
        |  POST /api/admin         -> change status / availability (dequeue)
        |
        v
 server.c   (listens on port 8080, one thread per visitor)
        |
        +--> Queue  (front, rear, enqueue, dequeue)   <- the DSA part
        |
        +--> orders.txt (save + load every order)
```

Important: **the website and the C server run on the same address** `http://localhost:8080`.
`server.c` first serves `index.html`, `style.css`, `script.js` and the images,
and then answers the `/api/...` requests. So one `server.exe` does both jobs.

---

## 5. The Queue (the only DSA concept in this project)

### 5.1 Real-world example

Think of the token counter of a canteen:

* Customer **A** comes first and orders -> token 1
* Customer **B** comes next -> token 2
* Customer **C** comes last -> token 3
* The kitchen starts with **A**, then **B**, then **C**

That is **FIFO - First In, First Out**. The order that enters first leaves first.

### 5.2 How it is stored (array based)

```c
int queue[MAX_QUEUE];   /* holds ORDER IDs */
int front = 0;          /* index of the order to be prepared NEXT */
int rear = -1;          /* index where the LAST order was added   */
int queueSize = 0;      /* how many orders are waiting            */
```

* `front` - the position from which orders are **removed** (kitchen side).
* `rear` - the position at which new orders are **added** (customer side).
* The array is **circular**: `(rear + 1) % MAX_QUEUE`, so after the last index we go back to `0`.
  This lets us reuse empty places without shifting the whole array.

Example, after 3 orders (ids 1, 2, 3):

```
index:   0    1    2    3   ...  49
queue: [ 1 ][ 2 ][ 3 ][   ] ... [   ]
         ^                 ^
       front=0           rear=2
```

When order 1 starts cooking: `dequeue()` moves `front` to 1.
When a new order 4 arrives: `enqueue()` moves `rear` to 3.

### 5.3 The four operations (all in `server.c`)

| Function | What it does | Time complexity |
|----------|--------------|-----------------|
| `isEmpty()` | returns 1 if `queueSize == 0` | **O(1)** - only one comparison |
| `enqueue(orderId)` | `rear = (rear+1) % MAX_QUEUE; queue[rear] = orderId; queueSize++` | **O(1)** - one assignment |
| `dequeue()` | returns `queue[front]`, then `front = (front+1) % MAX_QUEUE; queueSize--` | **O(1)** - one assignment |
| `displayQueue()` / `queuePosition(id)` | walks through the waiting orders | **O(n)** - a loop over `queueSize` |

Extra helper: `removeFromQueue(orderId)` is used only when the admin starts cooking an order that is **not** at the front.
It shifts the orders behind it one step forward (still O(n)). Normal flow always uses `dequeue()`.

### 5.4 Where the Queue is really used (no fake demo)

* `POST /api/order`  -> `enqueue(orderId)`  (line where the order is created)
* `POST /api/admin` with `status = "Preparing"` -> `dequeue()` (or `removeFromQueue`)
* `GET /api/queue` -> reads `front...rear` and returns the waiting list to the admin page and token board
* The server console prints the queue after every change with `displayQueue()`

So the **same queue** that you see on the admin page is the array inside `server.c`.

---

## 6. How an order travels from the website to the C program

1. The customer presses **Place Order** in `script.js`.
2. `script.js` builds a JSON object:

```json
{
  "customerName": "Rohith Nishal",
  "studentId": "21CS045",
  "department": "CSE",
  "mobile": "9876543210",
  "orderType": "Dine-in",
  "tableNumber": "12",
  "specialInstructions": "less spicy",
  "itemIds": [1, 5, 11],
  "quantities": [1, 1, 1]
}
```

3. It is sent with `fetch("/api/order", { method: "POST", body: ... })`.
4. `server.c` receives the raw text in `recvRequest()`.
5. `handleRequest()` sees `POST /api/order` and calls `handlePlaceOrder()`.
6. `getJsonString()` and `getJsonIntArray()` pick the values out of the text (very simple string search, no library).
7. `validateCustomer()` checks the mobile number, name, department, table number, etc.
8. The order is stored in `orders[]`, `enqueue()` adds it to the queue, `saveOrders()` writes `orders.txt`.
9. The answer goes back to the browser:

```json
{ "ok": true, "orderId": 1, "token": 1, "status": "Placed",
  "total": 225, "comboSaving": 15, "queuePosition": 1,
  "ordersAhead": 0, "estimatedWait": 20, "items": [ ... ] }
```

10. `script.js` prints the receipt with the token number and waiting time.

---

## 7. How customer and order details are stored

Simple method (no SQL, no extra library): **one text line per order** in `orders.txt`.

```
orderId|token|name|studentId|dept|mobile|orderType|table|status|total|time|items|instructions
```

Example:

```
1|1|Rohith Nishal|21CS045|CSE|9876543210|Dine-in|12|Placed|225|10:32|Masala Dosa:1;Chicken Biryani:1;Fresh Lime Juice:1|less spicy
```

* `saveOrders()` writes the file after **every** change (new order or status change).
* `loadOrders()` reads the file when the server starts, restores all orders, and puts every order
  that is still `Placed` back into the Queue. So restarting the server does not lose orders.
* Empty fields are written as `-` so that `strtok()` (which splits the line) never skips a field.
* The characters `| " \` and newlines are removed from customer text by `cleanText()` so the file and the JSON stay safe.

---

## 8. Local development and Render deployment

### 8.1 Run locally on Linux / WSL

Open a terminal in the `SmartServe` folder and run:

```bash
gcc -std=c11 -O2 -pthread server.c -o server
./server
```

If `PORT` is not set, the server uses port `8080`. Open `http://localhost:8080` in the browser. To stop it, press `Ctrl+C` in the terminal.

### 8.2 Run locally on Windows

The original Windows version of this project used Winsock and Windows threads. The included Render-ready `server.c` is adapted for Linux/POSIX sockets, which is what Render's Docker container uses. For the easiest Windows workflow, use WSL with GCC installed, or use the original Windows-only version of `server.c` in a separate local copy.

### 8.3 Deploy to Render using Docker

The included `Dockerfile` compiles the C backend inside a Linux build image and copies the HTML, CSS, JavaScript, and `images/` directory into the runtime image.

1. Keep `Dockerfile`, `server.c`, `index.html`, `style.css`, `script.js`, and the `images/` folder together in the same project root.
2. Push these files to the GitHub repository connected to Render.
3. In Render, create or open a **Web Service** for that repository and select **Docker** as the runtime. If the project files are inside a subfolder named `SmartServe`, set Render's **Root Directory** to `SmartServe`; otherwise, leave the root directory blank.
4. Let Render build and deploy the Dockerfile. The C server reads the `PORT` environment variable provided by Render and binds to `0.0.0.0`.
5. Open the generated `onrender.com` URL. The same C server serves both the frontend files and `/api/...` endpoints, so `script.js` uses same-origin relative paths such as `/api/menu` and does not need a separate backend URL.

**Order storage note:** this project saves orders in `orders.txt`. Render's default filesystem is ephemeral, so orders can be lost after restarts or redeploys unless persistent storage or a managed database is configured.

## 9. API reference (what `script.js` calls)

| Method | URL | Purpose | Body / Query |
|--------|-----|---------|--------------|
| GET | `/api/menu` | all food items | - |
| POST | `/api/order` | place an order (enqueue) | JSON with customer details + `itemIds` + `quantities` |
| GET | `/api/order?id=3` | status of one order | `?id=<orderId>` |
| GET | `/api/queue` | waiting queue + active orders + counts | - |
| POST | `/api/admin` | change status (dequeue) or availability | `{"action":"status","orderId":3,"status":"Preparing"}` or `{"action":"availability","itemId":9,"available":0}` |

Order status flow: `Placed -> Preparing -> Ready -> Completed`.
When the status changes to `Preparing`, the order leaves the queue.

Estimated waiting time formula used in the project:

```
waitingMinutes = (orders ahead of you * 5) + longest preparation time in your order
```

Combo rule (pure `if` condition, **not AI**): if the cart contains a Main Course or Snacks item **and** Fresh Lime Juice, the total gets ₹15 off. The same rule exists in `comboSaving()` (C) and `comboDiscount()` (JavaScript).

---

## 10. Troubleshooting (common beginner errors)

| Error / symptom | Reason | Fix |
|---|---|---|
| `'gcc' is not recognized` | GCC not installed, or terminal opened before install | install GCC (8.1), close and reopen VS Code, check `gcc --version` |
| `undefined reference to 'WSAStartup'` | forgot the socket library | use exactly `gcc server.c -o server.exe -lws2_32` |
| `Permission denied` / `cannot open output file server.exe` | `server.exe` is still running | press Ctrl+C in the server window (or `taskkill /IM server.exe /F`), then rebuild |
| `ERROR: Port 8080 is already in use` | old server still running | `taskkill /IM server.exe /F`, start again |
| Website shows red banner "Cannot reach the C server" | `server.exe` is not running | start `.\server.exe` and press Refresh |
| Page opens but **no images / no CSS** | server started from another folder | always run `.\server.exe` inside the `SmartServe` folder |
| Order button shows an error | validation failed (e.g. mobile number) | the exact message is printed under the form field; fix and retry |
| `orders.txt` looks wrong after you edited it by hand | broken line | delete `orders.txt` to start fresh (only during development) |
| Windows firewall popup | first run of `server.exe` | click **Allow** (needed for localhost access in some setups) |
| Changes in `script.js`/`style.css` not visible | browser cache | press **Ctrl + F5** (hard refresh) |

---

## 11. How to explain the project in your viva

**Q1. What is this project?**
"SmartServe is a canteen ordering website. The front end is HTML/CSS/JavaScript and the backend is a C program that runs a small HTTP server. The DSA concept used is a Queue."

**Q2. Which data structure did you use and why?**
"A Queue, because a canteen serves customers in the same order they arrive - First In, First Out. So the first order received is the first order prepared."

**Q3. What do `front` and `rear` mean?**
"`front` is the index of the order that will be cooked next. `rear` is the index where the newest order is inserted."

**Q4. Explain `enqueue()` and `dequeue()` with complexity.**
"`enqueue` inserts at `(rear+1)%MAX_QUEUE` and `dequeue` removes from `front`. Both are O(1) because we never shift the array. Finding the position of a particular order is O(n) because it is a loop."

**Q5. How does the website talk to your C program?**
"`script.js` uses the `fetch()` API to send HTTP requests to `/api/menu`, `/api/order`, `/api/queue` and `/api/admin`. `server.c` reads the request, sends the response as JSON, and closes the connection."

**Q6. Where is the data stored?**
"In a text file `orders.txt`. One line is one order, fields separated by `|`. `saveOrders()` writes it and `loadOrders()` reads it at startup."

**Q7. Is it really FIFO? Can you show?**
"Yes - open the Admin section. Place three orders; the queue shows positions 1, 2, 3. Press 'Start Preparing' on order 1; it is removed from the front. The server console also prints the queue after every change using `displayQueue()`."

**Q8. How do you handle an empty or full queue?**
"`isEmpty()` returns 1 when `queueSize == 0`; `dequeue()` then returns -1. `isFull()` blocks new orders when `queueSize == MAX_QUEUE` (50)."

**Q9. How do you handle many users at the same time?**
"Each browser connection is handled by its own thread, and a `CRITICAL_SECTION` lock protects the queue so two threads never change it at the same time."

**Q10. What is the combo suggestion - is it AI?**
"No. It is a simple rule: if the cart has a snack or main course and also Fresh Lime Juice, ₹15 is reduced. The same `if` condition is written in both C and JavaScript."

**Q11. What is the time complexity of the whole order flow?**
"Validation is O(k) for k items, `enqueue` is O(1), saving the file is O(n) for n stored orders, and showing the queue is O(q) for q waiting orders."

---

## 12. Demo script (2 minutes)

1. Start the server and open `http://localhost:8080`.
2. Search "dosa", add it, then add Chicken Biryani and Fresh Lime Juice (the combo banner suggests the drink).
3. Show the combo discount ₹15 in the cart.
4. Checkout with valid details -> show the receipt with token, queue position and waiting time.
5. Place a second order from another tab -> the admin queue shows position 1 and 2.
6. Open **Admin** -> press **Start Preparing** on order 1 -> it leaves the queue, the token board shows `#1` as "Now Preparing".
7. Show the server console window: the FIFO queue is printed there.
8. Track the order from the customer page: `Placed -> Preparing -> Ready -> Completed`.

---

## 13. Notes

* **French Fries is intentionally marked "Sold Out"** in the menu so the sold-out label can be demonstrated. In the Admin section press the button to make it available again (or change `available` in `server.c` from `0` to `1` and rebuild).
* Food photographs are from **Wikimedia Commons** (free to use) - they are stored inside `images/`.
* Food availability changes made in the Admin page are kept in memory (a restart resets them). Orders, however, are always saved in `orders.txt`.
* There is no AI in this project - every feature is a simple function or `if` condition.


### 8.6 Run locally on Linux / WSL / Linux-based terminal

This Render-ready version uses POSIX sockets. In the `SmartServe` folder:

```bash
gcc -std=c11 -O2 -pthread server.c -o server
./server
```

Open `http://localhost:8080` if `PORT` is not set.

### 8.7 Deploy to Render using Docker

The `Dockerfile` in the project root compiles `server.c` and copies the HTML, CSS, JavaScript, and `images/` directory into the runtime image. In Render, create a **Web Service** linked to this repository and choose **Docker** as the runtime. Keep the Dockerfile at the repository root (next to `server.c`), then deploy. The C server reads Render's `PORT` environment variable and serves the frontend and `/api/...` endpoints from the same origin, so `script.js` can keep using relative URLs such as `/api/menu`.

**Storage note:** this project saves orders to `orders.txt`. Render's default filesystem is ephemeral, so orders may be lost after restarts or redeploys unless you configure persistent storage or move the order data to a managed database.
