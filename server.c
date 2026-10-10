/*
 * ============================================================
 *  SmartServe - Canteen Order Management System
 *  Backend server written in portable C (POSIX sockets for Linux/Render)
 *  Only DSA concept used: Queue (FIFO) - array based
 *
 *  Build : gcc -std=c11 -O2 -pthread server.c -o server
 *  Run   : ./server
 *  Port  : PORT environment variable (default 8080)
 *
 *  How a request flows:
 *     Browser  --HTTP-->  server.c  -->  Queue  -->  orders.txt
 * ============================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <unistd.h>
#include <stdint.h>
#include <signal.h>

/* Small compatibility names keep the HTTP handling code readable. */
typedef int SOCKET;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR   (-1)
#define closesocket    close

/* ------------------------------------------------------------------
 * 1. SETTINGS  (how big our program's storage is)
 * ------------------------------------------------------------------ */
#define DEFAULT_PORT  8080    /* used locally when PORT is not configured */
#define MAX_QUEUE     50      /* queue can hold 50 waiting orders */
#define MAX_ORDERS    500     /* orders stored in memory and in the file */
#define MAX_MENU      11      /* number of food items in the menu */
#define MAX_PER_ORDER 8       /* max different food items in one order */
#define MAX_TEXT      120     /* max length of special instructions */
#define RECV_SIZE     16384   /* buffer size for one HTTP request */

/* ------------------------------------------------------------------
 * 2. STRUCTURES  (the data we store)
 * ------------------------------------------------------------------ */

/* One food item of the menu */
struct MenuItem {
    int id;
    char name[40];
    int price;              /* in rupees */
    char category[25];      /* Breakfast / South Indian / Main Course / ... */
    char description[70];
    int prepTime;           /* minutes needed to prepare */
    int available;          /* 1 = available, 0 = sold out */
    int popular;            /* 1 = show a "Popular" label */
    char image[40];         /* image file path */
};

/* One customer order */
struct Order {
    int orderId;                 /* unique order id, e.g. 1, 2, 3 ... */
    int token;                   /* token shown on the token board */
    char customerName[50];
    char studentId[30];
    char department[30];
    char mobile[15];
    char orderType[15];          /* "Dine-in" or "Takeaway" */
    char tableNumber[10];        /* only used for Dine-in */
    char specialInstructions[MAX_TEXT];
    char itemNames[MAX_PER_ORDER][40];  /* food names in this order */
    int  itemQty[MAX_PER_ORDER];        /* quantity of each food */
    int  itemCount;                     /* how many different foods */
    int  total;                         /* final amount in rupees */
    char status[15];                    /* Placed/Preparing/Ready/Completed */
    char placedTime[10];                /* time like "10:32" */
};

/* ------------------------------------------------------------------
 * 3. GLOBAL DATA (shared by all client threads)
 * ------------------------------------------------------------------ */

/* The menu is fixed. Every entry is:
   { id, name, price, category, description, prepTime, available, popular, image } */
struct MenuItem menu[MAX_MENU] = {
    { 1, "Masala Dosa",        60, "South Indian", "Crispy dosa with spiced potato filling",   10, 1, 1, "images/masala-dosa.jpg" },
    { 2, "Idli & Sambar",      40, "Breakfast",    "Soft idlis served with hot sambar",         8, 1, 0, "images/idli-sambar.jpg" },
    { 3, "Ven Pongal",         55, "Breakfast",    "Pepper pongal with ghee and cashews",      10, 1, 0, "images/pongal.jpg" },
    { 4, "South Indian Meals", 90, "South Indian", "Full meals: rice, sambar, rasam, veggies", 12, 1, 1, "images/meals.jpg" },
    { 5, "Chicken Biryani",   140, "Main Course",  "Spicy dum biryani served with raita",      20, 1, 1, "images/chicken-biryani.jpg" },
    { 6, "Veg Fried Rice",     80, "Main Course",  "Wok tossed rice with fresh vegetables",    12, 1, 0, "images/veg-fried-rice.jpg" },
    { 7, "Burger",             70, "Snacks",       "Veg patty burger with lettuce and cheese", 10, 1, 1, "images/burger.jpg" },
    { 8, "Pizza",             120, "Snacks",       "Margherita pizza with extra cheese",       15, 1, 0, "images/pizza.jpg" },
    { 9, "French Fries",       50, "Snacks",       "Golden crispy salted fries",                8, 0, 0, "images/french-fries.jpg" },
    {10, "Cold Coffee",        60, "Beverages",    "Chilled blended coffee with milk",          5, 1, 1, "images/cold-coffee.jpg" },
    {11, "Fresh Lime Juice",   40, "Beverages",    "Refreshing sweet lime juice",               4, 1, 0, "images/lime-juice.jpg" }
};

struct Order orders[MAX_ORDERS];   /* every order ever placed */
int orderCount = 0;                /* how many orders are stored */
int nextOrderId = 1;               /* next order id to give */
int nextToken = 1;                 /* next token number to give */

/* ---------------- THE QUEUE (the only DSA concept) ----------------
 * It is a normal array. "front" points to the order that will be
 * served NEXT, "rear" points to the LAST order that was added.
 * The array is circular: after index MAX_QUEUE-1 we go back to 0,
 * so we can reuse empty places without shifting any element.
 * ------------------------------------------------------------ */
int queue[MAX_QUEUE];   /* the queue stores ORDER IDs (not the whole order) */
int front = 0;          /* index of the next order to be prepared */
int rear = -1;          /* index of the last order that was added */
int queueSize = 0;      /* how many orders are waiting right now */

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER; /* protects shared order/queue data */

/* ------------------------------------------------------------------
 * 4. SMALL USEFUL HELPERS
 * ------------------------------------------------------------------ */

/* Removes characters that would break our text file or JSON output
   and also removes < and > so nobody can store harmful HTML text. */
void cleanText(char *s)
{
    int write = 0;
    int read;
    for (read = 0; s[read] != '\0'; read++) {
        char c = s[read];
        if (c == '|' || c == '"' || c == '\\' || c == '<' || c == '>' ||
            c == '\n' || c == '\r')
            continue;
        s[write++] = c;
    }
    s[write] = '\0';
}

/* Finds a menu item by its id. Returns NULL when not found. */
struct MenuItem *findMenuItem(int id)
{
    int i;
    for (i = 0; i < MAX_MENU; i++) {
        if (menu[i].id == id)
            return &menu[i];
    }
    return NULL;
}

/* Finds a menu item by its name (used when reading the file back). */
struct MenuItem *findMenuItemByName(const char *name)
{
    int i;
    for (i = 0; i < MAX_MENU; i++) {
        if (strcmp(menu[i].name, name) == 0)
            return &menu[i];
    }
    return NULL;
}

/* Finds an order by its id. Returns NULL when not found. */
struct Order *findOrder(int orderId)
{
    int i;
    for (i = 0; i < orderCount; i++) {
        if (orders[i].orderId == orderId)
            return &orders[i];
    }
    return NULL;
}

/* ------------------------------------------------------------------
 * 5. QUEUE FUNCTIONS  (enqueue, dequeue, displayQueue, isEmpty)
 *    Each of them works in O(1) or O(n) as explained in README.
 * ------------------------------------------------------------------ */

/* isEmpty() - returns 1 if no order is waiting, else 0. O(1) */
int isEmpty(void)
{
    return (queueSize == 0);
}

/* isFull() - returns 1 if the queue cannot take more orders. O(1) */
int isFull(void)
{
    return (queueSize == MAX_QUEUE);
}

/* enqueue(orderId) - add a new order at the REAR (back) of the queue.
   Returns 1 on success, 0 when the queue is full. O(1) */
int enqueue(int orderId)
{
    if (isFull())
        return 0;
    rear = (rear + 1) % MAX_QUEUE;   /* move rear forward, wrap around at the end */
    queue[rear] = orderId;
    queueSize++;
    return 1;
}

/* dequeue() - remove the order at the FRONT (next to prepare).
   Returns the removed order id, or -1 when the queue is empty. O(1) */
int dequeue(void)
{
    int orderId;
    if (isEmpty())
        return -1;
    orderId = queue[front];
    front = (front + 1) % MAX_QUEUE;   /* move front forward, wrap around */
    queueSize--;
    return orderId;
}

/* queuePosition(orderId) - tells the waiting position 1, 2, 3 ...
   Returns 0 when the order is not inside the queue. O(n) */
int queuePosition(int orderId)
{
    int i;
    for (i = 0; i < queueSize; i++) {
        int index = (front + i) % MAX_QUEUE;
        if (queue[index] == orderId)
            return i + 1;
    }
    return 0;
}

/* removeFromQueue(orderId) - normally we only remove the FRONT order
   with dequeue(). If the admin starts preparing an order that is
   somewhere in the middle, we shift the orders behind it one step
   forward so the FIFO order stays correct. O(n) */
void removeFromQueue(int orderId)
{
    int pos = queuePosition(orderId);
    int start, i;
    if (pos == 0)          /* not in the queue - nothing to do */
        return;
    start = (front + pos - 1) % MAX_QUEUE;   /* index of this order */
    for (i = 0; i < queueSize - pos; i++) {
        int from = (start + 1 + i) % MAX_QUEUE;
        int to   = (start + i) % MAX_QUEUE;
        queue[to] = queue[from];
    }
    rear = (rear - 1 + MAX_QUEUE) % MAX_QUEUE;   /* one empty place at the back */
    queueSize--;
}

/* displayQueue() - prints the full waiting queue on the server console.
   Open the server window during the viva to show FIFO live. */
void displayQueue(void)
{
    int i;
    printf("\n+---------------- WAITING QUEUE (FIFO) ----------------+\n");
    if (isEmpty()) {
        printf("|  Queue is empty - no orders waiting.                |\n");
    } else {
        for (i = 0; i < queueSize; i++) {
            int index = (front + i) % MAX_QUEUE;
            struct Order *o = findOrder(queue[index]);
            if (o != NULL) {
                printf("|  Position %2d : Token #%-3d  %-20s  %s |\n",
                       i + 1, o->token, o->customerName, o->status);
            }
        }
    }
    printf("+-----------------------------------------------------+\n");
    fflush(stdout);   /* show the print immediately on the console */
}

/* ------------------------------------------------------------------
 * 6. ORDER CALCULATIONS
 * ------------------------------------------------------------------ */

/* Rule based combo: a drink + a hot snack/main costs 15 rupees less.
   This is a simple rule written in code, not AI. */
int comboSaving(struct Order *o)
{
    int i;
    int hasHotFood = 0;
    int hasLimeJuice = 0;
    for (i = 0; i < o->itemCount; i++) {
        struct MenuItem *m = findMenuItemByName(o->itemNames[i]);
        if (m == NULL)
            continue;
        if (strcmp(m->category, "Main Course") == 0 || strcmp(m->category, "Snacks") == 0)
            hasHotFood = 1;
        if (strcmp(m->name, "Fresh Lime Juice") == 0)
            hasLimeJuice = 1;
    }
    if (hasHotFood && hasLimeJuice)
        return 15;
    return 0;
}

/* Approximate waiting time in minutes.
   Each order ahead of you takes about 5 minutes in this canteen,
   plus the longest preparation time inside your own order. */
int waitingMinutes(struct Order *o)
{
    int i;
    int ownPrep = 5;
    int pos;

    for (i = 0; i < o->itemCount; i++) {
        struct MenuItem *m = findMenuItemByName(o->itemNames[i]);
        if (m != NULL && m->prepTime > ownPrep)
            ownPrep = m->prepTime;
    }

    if (strcmp(o->status, "Placed") != 0) {
        if (strcmp(o->status, "Preparing") == 0)
            return ownPrep;      /* already being cooked */
        return 0;                /* Ready or Completed */
    }

    pos = queuePosition(o->orderId);
    if (pos <= 0)
        return 0;
    return (pos - 1) * 5 + ownPrep;
}

/* ------------------------------------------------------------------
 * 7. SAVE / LOAD ORDERS  (simple text file = our "database")
 *    Every order is one line:
 *    id|token|name|studentId|dept|mobile|type|table|status|total|time|items|instructions
 * ------------------------------------------------------------------ */
void saveOrders(void)
{
    FILE *f;
    int i, j;
    char itemsText[400];

    f = fopen("orders.txt", "w");
    if (f == NULL) {
        printf("Warning: could not open orders.txt for writing\n");
        return;
    }

    for (i = 0; i < orderCount; i++) {
        struct Order *o = &orders[i];
        itemsText[0] = '\0';
        for (j = 0; j < o->itemCount; j++) {
            char one[80];
            snprintf(one, sizeof(one), "%s:%d%s",
                     o->itemNames[j], o->itemQty[j],
                     (j < o->itemCount - 1) ? ";" : "");
            strncat(itemsText, one, sizeof(itemsText) - strlen(itemsText) - 1);
        }
        /* empty fields are saved as "-", otherwise strtok would skip them */
        fprintf(f, "%d|%d|%s|%s|%s|%s|%s|%s|%s|%d|%s|%s|%s\n",
                o->orderId, o->token, o->customerName, o->studentId,
                o->department, o->mobile, o->orderType,
                o->tableNumber[0] ? o->tableNumber : "-",
                o->status, o->total, o->placedTime, itemsText,
                o->specialInstructions[0] ? o->specialInstructions : "-");
    }
    fclose(f);
}

/* Returns the next "|" separated field of the line we are reading.
   If the line is broken we get an empty text instead of NULL. */
char *nextField(void)
{
    char *part = strtok(NULL, "|");
    if (part == NULL)
        return "";
    return part;
}

/* Reads orders.txt (if it exists) when the server starts.
   Waiting orders are put back into the queue. */
void loadOrders(void)
{
    FILE *f;
    char line[1024];

    f = fopen("orders.txt", "r");
    if (f == NULL)
        return;   /* first run - no file yet, that is fine */

    while (fgets(line, sizeof(line), f) != NULL) {
        char *part;
        struct Order *o;
        char itemsText[400];

        /* remove the enter key at the end of the line */
        line[strcspn(line, "\n")] = '\0';
        if (line[0] == '\0')
            continue;
        if (orderCount >= MAX_ORDERS)
            break;

        o = &orders[orderCount];
        memset(o, 0, sizeof(struct Order));

        /* split the line using the | character (one statement per field,
           because two strtok() calls must never share one line) */
        part = strtok(line, "|");
        if (part == NULL)
            continue;                    /* broken line - skip it */
        o->orderId = atoi(part);

        part = nextField();  o->token = atoi(part);
        part = nextField();  snprintf(o->customerName, 50, "%s", part);
        part = nextField();  snprintf(o->studentId, 30, "%s", part);
        part = nextField();  snprintf(o->department, 30, "%s", part);
        part = nextField();  snprintf(o->mobile, 15, "%s", part);
        part = nextField();  snprintf(o->orderType, 15, "%s", part);
        part = nextField();  snprintf(o->tableNumber, 10, "%s", part);
        if (strcmp(o->tableNumber, "-") == 0) o->tableNumber[0] = '\0';
        part = nextField();  snprintf(o->status, 15, "%s", part);
        part = nextField();  o->total = atoi(part);
        part = nextField();  snprintf(o->placedTime, 10, "%s", part);
        part = nextField();  snprintf(itemsText, 400, "%s", part);
        part = nextField();  snprintf(o->specialInstructions, MAX_TEXT, "%s", part);
        if (strcmp(o->specialInstructions, "-") == 0) o->specialInstructions[0] = '\0';

        /* read the items back:  Masala Dosa:2;Burger:1 */
        {
            char *food = strtok(itemsText, ";");
            while (food != NULL && o->itemCount < MAX_PER_ORDER) {
                char *colon = strrchr(food, ':');
                if (colon != NULL) {
                    *colon = '\0';
                    snprintf(o->itemNames[o->itemCount], 40, "%s", food);
                    o->itemQty[o->itemCount] = atoi(colon + 1);
                    o->itemCount++;
                }
                food = strtok(NULL, ";");
            }
        }

        /* the next id and token continue after the biggest old one */
        if (o->orderId >= nextOrderId) nextOrderId = o->orderId + 1;
        if (o->token >= nextToken) nextToken = o->token + 1;

        /* only waiting orders go back into the queue */
        if (strcmp(o->status, "Placed") == 0)
            enqueue(o->orderId);

        orderCount++;
    }
    fclose(f);
    printf("Loaded %d orders from orders.txt\n", orderCount);
}

/* ------------------------------------------------------------------
 * 8. VERY SIMPLE JSON READING
 *    The website sends small JSON objects like:
 *    {"customerName":"Rohith","itemIds":[1,5],"quantities":[2,1]}
 *    These three functions pick values out of that text.
 * ------------------------------------------------------------------ */

/* Finds  "key":  then reads the text after it:  "value"
   NOTE: the colon is part of the pattern, otherwise the search could
   wrongly match a VALUE that is equal to the key (for example the
   value "status" inside  {"action":"status", "status":"Preparing"}). */
int getJsonString(const char *json, const char *key, char *out, int outSize)
{
    char pattern[80];
    const char *p;
    int i = 0;

    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    p = strstr(json, pattern);
    if (p == NULL)
        return 0;
    p += strlen(pattern);

    /* skip spaces and the opening quote */
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ':')
        p++;
    if (*p != '"')
        return 0;
    p++;

    while (*p != '\0' && *p != '"' && i < outSize - 1) {
        if (*p == '\\' && *(p + 1) != '\0')   /* skip escape characters */
            p++;
        out[i++] = *p;
        p++;
    }
    out[i] = '\0';
    return 1;
}

/* Finds  "key": 123  and returns the number 123 */
int getJsonInt(const char *json, const char *key, int *out)
{
    char pattern[80];
    const char *p;

    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    p = strstr(json, pattern);
    if (p == NULL)
        return 0;
    p += strlen(pattern);
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ':')
        p++;
    if (*p < '0' || *p > '9')
        return 0;
    *out = atoi(p);
    return 1;
}

/* Finds  "key": [1, 5, 7]  and stores the numbers in the array */
int getJsonIntArray(const char *json, const char *key, int *values, int maxSize)
{
    char pattern[80];
    const char *p;
    int count = 0;

    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    p = strstr(json, pattern);
    if (p == NULL)
        return 0;
    p = strchr(p, '[');
    if (p == NULL)
        return 0;
    p++;

    while (*p != '\0' && *p != ']' && count < maxSize) {
        if (*p >= '0' && *p <= '9') {
            int number = 0;
            while (*p >= '0' && *p <= '9') {
                number = number * 10 + (*p - '0');
                p++;
            }
            values[count++] = number;
        } else {
            p++;
        }
    }
    return count;
}

/* ------------------------------------------------------------------
 * 9. BUILD JSON RESPONSES (text sent back to the browser)
 * ------------------------------------------------------------------ */

/* Writes the food list of one order: [{"name":"Pizza","qty":1,"price":120}, ...] */
void itemsToJson(struct Order *o, char *out, int outSize)
{
    int i;
    int used = 0;
    out[0] = '\0';
    for (i = 0; i < o->itemCount && used < outSize - 1; i++) {
        struct MenuItem *m = findMenuItemByName(o->itemNames[i]);
        int price = (m != NULL) ? m->price : 0;
        int n = snprintf(out + used, outSize - used,
                         "%s{\"name\":\"%s\",\"qty\":%d,\"price\":%d}",
                         (i == 0) ? "" : ",", o->itemNames[i], o->itemQty[i], price);
        if (n < 0 || n >= outSize - used)
            break;
        used += n;
    }
}

/* ------------------------------------------------------------------
 * 10. HTTP HELPERS  (how we talk to the browser)
 * ------------------------------------------------------------------ */

void sendJson(SOCKET client, int code, const char *statusText, const char *json)
{
    char header[1024];
    int headerLen, bodyLen;

    bodyLen = (int)strlen(json);
    headerLen = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: application/json; charset=UTF-8\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "Cache-Control: no-store\r\n"
        "\r\n",
        code, statusText, bodyLen);

    send(client, header, headerLen, 0);
    send(client, json, bodyLen, 0);
}

/* Sends a JSON error like {"ok":false,"error":"Mobile number must be 10 digits"} */
void sendError(SOCKET client, int code, const char *statusText, const char *message)
{
    char body[512];
    snprintf(body, sizeof(body), "{\"ok\":false,\"error\":\"%s\"}", message);
    sendJson(client, code, statusText, body);
}

int endsWith(const char *text, const char *ending)
{
    int t = (int)strlen(text);
    int e = (int)strlen(ending);
    if (t < e)
        return 0;
    return strcmp(text + t - e, ending) == 0;
}

const char *contentTypeFor(const char *path)
{
    if (endsWith(path, ".html")) return "text/html; charset=UTF-8";
    if (endsWith(path, ".css"))  return "text/css; charset=UTF-8";
    if (endsWith(path, ".js"))   return "application/javascript; charset=UTF-8";
    if (endsWith(path, ".jpg"))  return "image/jpeg";
    if (endsWith(path, ".jpeg")) return "image/jpeg";
    if (endsWith(path, ".png"))  return "image/png";
    if (endsWith(path, ".txt"))  return "text/plain; charset=UTF-8";
    return "application/octet-stream";
}

/* Sends a website file (index.html, style.css, script.js, images). */
void sendFile(SOCKET client, const char *path)
{
    const char *filePath = path;
    FILE *f;
    long size;
    char header[1024];
    char buffer[4096];
    int headerLen;
    size_t readBytes;

    /* the browser automatically asks for the tab icon */
    if (strcmp(path, "/favicon.ico") == 0) {
        const char *noContent = "HTTP/1.1 204 No Content\r\nConnection: close\r\n\r\n";
        send(client, noContent, (int)strlen(noContent), 0);
        return;
    }

    if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0)
        filePath = "index.html";
    else if (strcmp(path, "/style.css") == 0)
        filePath = "style.css";
    else if (strcmp(path, "/script.js") == 0)
        filePath = "script.js";
    else if (strncmp(path, "/images/", 8) == 0)
        filePath = path + 1;          /* remove the leading slash */
    else if (strcmp(path, "/orders.txt") == 0) {
        sendError(client, 404, "Not Found", "File not found");
        return;
    }

    /* stop people from opening files outside this folder */
    if (strstr(filePath, "..") != NULL) {
        sendError(client, 403, "Forbidden", "Not allowed");
        return;
    }

    f = fopen(filePath, "rb");
    if (f == NULL) {
        sendError(client, 404, "Not Found", "File not found");
        return;
    }

    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);

    headerLen = snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %ld\r\n"
        "Connection: close\r\n"
        "\r\n",
        contentTypeFor(filePath), size);
    send(client, header, headerLen, 0);

    /* send the file in small pieces */
    while ((readBytes = fread(buffer, 1, sizeof(buffer), f)) > 0) {
        send(client, buffer, (int)readBytes, 0);
    }
    fclose(f);
}

/* Looks for a header (any letter case), e.g. Content-Length:  */
char *findHeader(char *request, const char *name)
{
    int n = (int)strlen(name);
    int i;
    for (i = 0; request[i] != '\0'; i++) {
        int j;
        for (j = 0; j < n; j++) {
            char a = request[i + j];
            char b = name[j];
            if (a >= 'a' && a <= 'z') a = a - 32;   /* to upper case */
            if (b >= 'a' && b <= 'z') b = b - 32;
            if (a != b)
                break;
        }
        if (j == n)
            return request + i + n;
    }
    return NULL;
}

/* Reads one full HTTP request (headers + body) from the browser. */
int recvRequest(SOCKET client, char *buffer, int bufferSize)
{
    int total = 0;
    int headerEnd = -1;
    int contentLength = 0;

    while (total < bufferSize - 1) {
        int got = recv(client, buffer + total, bufferSize - 1 - total, 0);
        if (got <= 0)
            break;
        total += got;
        buffer[total] = '\0';

        if (headerEnd < 0) {
            char *marker = strstr(buffer, "\r\n\r\n");
            char *lengthText;
            if (marker != NULL) {
                headerEnd = (int)(marker - buffer) + 4;
                lengthText = findHeader(buffer, "Content-Length:");
                if (lengthText != NULL)
                    contentLength = atoi(lengthText);
            }
        }
        if (headerEnd >= 0 && total >= headerEnd + contentLength)
            break;
    }
    return total;
}

/* ------------------------------------------------------------------
 * 11. API HANDLERS (the 5 endpoints the website uses)
 * ------------------------------------------------------------------ */

/* GET /api/menu  -> list of all food items */
void handleMenu(SOCKET client)
{
    char body[4096];
    int used = 0;
    int i;

    used = snprintf(body, sizeof(body), "{\"ok\":true,\"menu\":[");
    for (i = 0; i < MAX_MENU && used < (int)sizeof(body) - 300; i++) {
        int n = snprintf(body + used, sizeof(body) - used,
            "%s{\"id\":%d,\"name\":\"%s\",\"price\":%d,\"category\":\"%s\","
            "\"description\":\"%s\",\"prepTime\":%d,"
            "\"available\":%s,\"popular\":%s,\"image\":\"%s\"}",
            (i == 0) ? "" : ",",
            menu[i].id, menu[i].name, menu[i].price, menu[i].category,
            menu[i].description, menu[i].prepTime,
            menu[i].available ? "true" : "false",
            menu[i].popular ? "true" : "false",
            menu[i].image);
        if (n < 0 || n >= (int)sizeof(body) - used)
            break;
        used += n;
    }
    snprintf(body + used, sizeof(body) - used, "]}");
    sendJson(client, 200, "OK", body);
}

/* Validates the customer details. Returns NULL if everything is fine,
   otherwise returns the error message to show to the customer. */
const char *validateCustomer(const char *name, const char *studentId,
                             const char *dept, const char *mobile,
                             const char *type, const char *table)
{
    int i;
    int digits = 0;

    if (strlen(name) < 3 || strlen(name) > 49)
        return "Please enter your full name (3 to 49 letters)";
    if (strlen(studentId) < 3 || strlen(studentId) > 29)
        return "Please enter a valid Student/Staff ID";
    if (strlen(dept) < 2 || strlen(dept) > 29)
        return "Please select your department";
    if (strlen(mobile) != 10)
        return "Mobile number must be exactly 10 digits";

    /* mobile must be 10 digits and start with 6, 7, 8 or 9 */
    for (i = 0; mobile[i] != '\0'; i++) {
        if (mobile[i] < '0' || mobile[i] > '9')
            return "Mobile number must contain only digits";
        digits++;
    }
    if (digits != 10)
        return "Mobile number must be exactly 10 digits";
    if (mobile[0] < '6' || mobile[0] > '9')
        return "Mobile number must start with 6, 7, 8 or 9";

    if (strcmp(type, "Dine-in") != 0 && strcmp(type, "Takeaway") != 0)
        return "Please select Dine-in or Takeaway";
    if (strcmp(type, "Dine-in") == 0) {
        if (strlen(table) < 1 || strlen(table) > 9)
            return "Please enter your table number for Dine-in";
    }
    return NULL;   /* all good */
}

/* POST /api/order  -> validates, stores and enqueues a new order */
void handlePlaceOrder(SOCKET client, char *bodyText)
{
    char name[50] = "", studentId[30] = "", dept[30] = "", mobile[15] = "";
    char type[15] = "", table[10] = "", instructions[MAX_TEXT] = "";
    int itemIds[MAX_PER_ORDER];
    int quantities[MAX_PER_ORDER];
    int idCount, qtyCount, i, j;
    int total = 0;
    const char *error;
    struct Order *o;
    char itemsJson[1024];
    char response[4096];
    int queuePos, wait;

    /* 1. read the data sent by the website */
    getJsonString(bodyText, "customerName", name, sizeof(name));
    getJsonString(bodyText, "studentId", studentId, sizeof(studentId));
    getJsonString(bodyText, "department", dept, sizeof(dept));
    getJsonString(bodyText, "mobile", mobile, sizeof(mobile));
    getJsonString(bodyText, "orderType", type, sizeof(type));
    getJsonString(bodyText, "tableNumber", table, sizeof(table));
    getJsonString(bodyText, "specialInstructions", instructions, sizeof(instructions));
    idCount = getJsonIntArray(bodyText, "itemIds", itemIds, MAX_PER_ORDER);
    qtyCount = getJsonIntArray(bodyText, "quantities", quantities, MAX_PER_ORDER);

    cleanText(name);
    cleanText(studentId);
    cleanText(dept);
    cleanText(mobile);
    cleanText(type);
    cleanText(table);
    cleanText(instructions);

    /* 2. validate the customer details in the backend too */
    error = validateCustomer(name, studentId, dept, mobile, type, table);
    if (error != NULL) {
        sendError(client, 400, "Bad Request", error);
        return;
    }

    /* 3. validate the food items */
    if (idCount == 0 || idCount != qtyCount) {
        sendError(client, 400, "Bad Request", "Your cart is empty");
        return;
    }
    if (idCount > MAX_PER_ORDER) {
        sendError(client, 400, "Bad Request", "You can order only 8 different items");
        return;
    }
    for (i = 0; i < idCount; i++) {
        struct MenuItem *m = findMenuItem(itemIds[i]);
        if (m == NULL) {
            sendError(client, 400, "Bad Request", "One food item does not exist");
            return;
        }
        if (m->available == 0) {
            char msg[120];
            snprintf(msg, sizeof(msg), "%s is sold out right now", m->name);
            sendError(client, 400, "Bad Request", msg);
            return;
        }
        if (quantities[i] < 1 || quantities[i] > 10) {
            sendError(client, 400, "Bad Request", "Quantity must be between 1 and 10");
            return;
        }
        total += m->price * quantities[i];
    }

    /* 4. check if the queue has space */
    if (isFull()) {
        sendError(client, 400, "Bad Request", "Queue is full, please try after some time");
        return;
    }

    /* 5. create the order */
    pthread_mutex_lock(&lock);

    if (orderCount >= MAX_ORDERS) {
        pthread_mutex_unlock(&lock);
        sendError(client, 400, "Bad Request", "Order limit reached, please restart the server");
        return;
    }

    o = &orders[orderCount];
    memset(o, 0, sizeof(struct Order));
    o->orderId = nextOrderId++;
    o->token = nextToken++;
    snprintf(o->customerName, 50, "%s", name);
    snprintf(o->studentId, 30, "%s", studentId);
    snprintf(o->department, 30, "%s", dept);
    snprintf(o->mobile, 15, "%s", mobile);
    snprintf(o->orderType, 15, "%s", type);
    snprintf(o->tableNumber, 10, "%s", table);
    snprintf(o->specialInstructions, MAX_TEXT, "%s", instructions);
    strcpy(o->status, "Placed");
    o->itemCount = idCount;
    for (j = 0; j < idCount; j++) {
        struct MenuItem *m = findMenuItem(itemIds[j]);
        snprintf(o->itemNames[j], 40, "%s", m->name);
        o->itemQty[j] = quantities[j];
    }
    o->total = total - comboSaving(o);

    /* local time like 10:32 */
    {
        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        strftime(o->placedTime, sizeof(o->placedTime), "%H:%M", t);
    }

    /* 6. FIFO: the first customer who orders is the first to be cooked */
    if (!enqueue(o->orderId)) {
        nextOrderId--;
        nextToken--;
        pthread_mutex_unlock(&lock);
        sendError(client, 400, "Bad Request", "Queue is full, please try after some time");
        return;
    }
    orderCount++;
    saveOrders();

    /* build the answer while we still hold the lock (queue can change) */
    itemsToJson(o, itemsJson, sizeof(itemsJson));
    queuePos = queuePosition(o->orderId);
    wait = waitingMinutes(o);

    snprintf(response, sizeof(response),
        "{\"ok\":true,\"orderId\":%d,\"token\":%d,\"status\":\"%s\","
        "\"total\":%d,\"comboSaving\":%d,"
        "\"queuePosition\":%d,\"ordersAhead\":%d,\"estimatedWait\":%d,"
        "\"items\":[%s]}",
        o->orderId, o->token, o->status,
        o->total, comboSaving(o),
        queuePos, queuePos - 1, wait, itemsJson);

    displayQueue();
    pthread_mutex_unlock(&lock);

    sendJson(client, 200, "OK", response);
}

/* GET /api/order?id=3  -> one order + live status */
void handleOrderStatus(SOCKET client, char *path)
{
    char *query = strchr(path, '?');
    int orderId = 0;
    struct Order *o;
    char itemsJson[1024];
    char response[4096];

    if (query == NULL || sscanf(query, "?id=%d", &orderId) != 1) {
        sendError(client, 400, "Bad Request", "Missing order id");
        return;
    }

    pthread_mutex_lock(&lock);
    o = findOrder(orderId);
    if (o == NULL) {
        pthread_mutex_unlock(&lock);
        sendError(client, 404, "Not Found", "Order not found");
        return;
    }
    itemsToJson(o, itemsJson, sizeof(itemsJson));

    snprintf(response, sizeof(response),
        "{\"ok\":true,\"order\":{"
        "\"orderId\":%d,\"token\":%d,\"customerName\":\"%s\","
        "\"studentId\":\"%s\",\"department\":\"%s\",\"mobile\":\"%s\","
        "\"orderType\":\"%s\",\"tableNumber\":\"%s\","
        "\"specialInstructions\":\"%s\",\"status\":\"%s\","
        "\"placedTime\":\"%s\",\"total\":%d,\"comboSaving\":%d,"
        "\"queuePosition\":%d,\"estimatedWait\":%d,\"items\":[%s]}}",
        o->orderId, o->token, o->customerName,
        o->studentId, o->department, o->mobile,
        o->orderType, o->tableNumber,
        o->specialInstructions, o->status,
        o->placedTime, o->total, comboSaving(o),
        queuePosition(o->orderId), waitingMinutes(o), itemsJson);
    pthread_mutex_unlock(&lock);

    sendJson(client, 200, "OK", response);
}

/* GET /api/queue  -> waiting orders + counts (used by admin + token board) */
void handleQueue(SOCKET client)
{
    char body[32768];
    int used = 0;
    int i;
    int waiting = 0, preparing = 0, ready = 0, completed = 0;

    pthread_mutex_lock(&lock);

    used = snprintf(body, sizeof(body), "{\"ok\":true,\"queue\":[");

    /* the queue in FIFO order (position 1 is served first) */
    {
        int first = 1;
        for (i = 0; i < queueSize; i++) {
            int index = (front + i) % MAX_QUEUE;
            struct Order *o = findOrder(queue[index]);
            char summary[200] = "";
            int n, k;

            if (o == NULL)
                continue;

            for (k = 0; k < o->itemCount; k++) {
                char one[80];
                snprintf(one, sizeof(one), "%s x%d%s", o->itemNames[k], o->itemQty[k],
                         (k < o->itemCount - 1) ? ", " : "");
                strncat(summary, one, sizeof(summary) - strlen(summary) - 1);
            }

            n = snprintf(body + used, sizeof(body) - used,
                "%s{\"position\":%d,\"orderId\":%d,\"token\":%d,"
                "\"customerName\":\"%s\",\"orderType\":\"%s\",\"tableNumber\":\"%s\","
                "\"status\":\"%s\",\"placedTime\":\"%s\",\"total\":%d,"
                "\"estimatedWait\":%d,\"itemsSummary\":\"%s\"}",
                first ? "" : ",",
                i + 1, o->orderId, o->token,
                o->customerName, o->orderType, o->tableNumber,
                o->status, o->placedTime, o->total,
                waitingMinutes(o), summary);
            if (n < 0 || n >= (int)sizeof(body) - used)
                break;
            used += n;
            first = 0;
        }
    }

    /* orders that are in the kitchen (Preparing) or waiting for pickup (Ready) */
    used += snprintf(body + used, sizeof(body) - used, "],\"active\":[");
    if (used >= (int)sizeof(body))
        used = (int)sizeof(body) - 1;   /* safety: never go past the buffer */
    {
        int first = 1;
        for (i = 0; i < orderCount; i++) {
            int n;
            if (strcmp(orders[i].status, "Preparing") != 0 &&
                strcmp(orders[i].status, "Ready") != 0)
                continue;
            n = snprintf(body + used, sizeof(body) - used,
                "%s{\"orderId\":%d,\"token\":%d,\"customerName\":\"%s\",\"placedTime\":\"%s\",\"status\":\"%s\"}",
                first ? "" : ",",
                orders[i].orderId, orders[i].token, orders[i].customerName,
                orders[i].placedTime, orders[i].status);
            if (n < 0 || n >= (int)sizeof(body) - used)
                break;
            used += n;
            first = 0;
        }
    }

    /* counts for the admin dashboard cards */
    waiting = queueSize;
    for (i = 0; i < orderCount; i++) {
        if (strcmp(orders[i].status, "Preparing") == 0) preparing++;
        else if (strcmp(orders[i].status, "Ready") == 0) ready++;
        else if (strcmp(orders[i].status, "Completed") == 0) completed++;
    }

    snprintf(body + used, sizeof(body) - used,
             "],\"counts\":{\"waiting\":%d,\"preparing\":%d,\"ready\":%d,\"completed\":%d}}",
             waiting, preparing, ready, completed);

    pthread_mutex_unlock(&lock);
    sendJson(client, 200, "OK", body);
}

/* POST /api/admin  -> change an order status or food availability */
void handleAdmin(SOCKET client, char *bodyText)
{
    char action[30] = "";
    char status[20] = "";
    int orderId = 0, itemId = 0, available = 1;
    char response[512];

    getJsonString(bodyText, "action", action, sizeof(action));

    /* ---- change order status: Placed -> Preparing -> Ready -> Completed ---- */
    if (strcmp(action, "status") == 0) {
        struct Order *o;
        getJsonInt(bodyText, "orderId", &orderId);
        getJsonString(bodyText, "status", status, sizeof(status));

        if (strcmp(status, "Placed") != 0 && strcmp(status, "Preparing") != 0 &&
            strcmp(status, "Ready") != 0 && strcmp(status, "Completed") != 0) {
            sendError(client, 400, "Bad Request", "Wrong status value");
            return;
        }

        pthread_mutex_lock(&lock);
        o = findOrder(orderId);
        if (o == NULL) {
            pthread_mutex_unlock(&lock);
            sendError(client, 404, "Not Found", "Order not found");
            return;
        }
        snprintf(o->status, 15, "%s", status);

        /* once cooking starts (or later) the order leaves the waiting queue */
        if (strcmp(status, "Placed") != 0 && queuePosition(orderId) > 0) {
            if (queue[front] == orderId)
                dequeue();                  /* the normal FIFO removal */
            else
                removeFromQueue(orderId);   /* admin started a later order */
        }

        saveOrders();
        displayQueue();
        pthread_mutex_unlock(&lock);

        snprintf(response, sizeof(response),
                 "{\"ok\":true,\"orderId\":%d,\"status\":\"%s\",\"waiting\":%d}",
                 orderId, status, queueSize);
        sendJson(client, 200, "OK", response);
        return;
    }

    /* ---- turn a food item available / sold out ---- */
    if (strcmp(action, "availability") == 0) {
        int i, found = 0;
        getJsonInt(bodyText, "itemId", &itemId);
        getJsonInt(bodyText, "available", &available);

        pthread_mutex_lock(&lock);
        for (i = 0; i < MAX_MENU; i++) {
            if (menu[i].id == itemId) {
                menu[i].available = (available != 0) ? 1 : 0;
                found = 1;
                break;
            }
        }
        pthread_mutex_unlock(&lock);

        if (!found) {
            sendError(client, 404, "Not Found", "Food item not found");
            return;
        }
        snprintf(response, sizeof(response),
                 "{\"ok\":true,\"itemId\":%d,\"available\":%s}",
                 itemId, available ? "true" : "false");
        sendJson(client, 200, "OK", response);
        return;
    }

    sendError(client, 400, "Bad Request", "Unknown action");
}

/* ------------------------------------------------------------------
 * 12. ROUTING  (decide which handler runs for each URL)
 * ------------------------------------------------------------------ */
void handleRequest(SOCKET client, char *request)
{
    char method[8] = "";
    char path[300] = "";
    char *bodyText;

    sscanf(request, "%7s %299s", method, path);
    if (method[0] == '\0') {
        sendError(client, 400, "Bad Request", "Bad request");
        return;
    }

    /* the body starts after the empty line of the headers */
    bodyText = strstr(request, "\r\n\r\n");
    if (bodyText != NULL)
        bodyText += 4;
    else
        bodyText = "";

    /* --- API addresses used by script.js --- */
    if (strcmp(method, "GET") == 0 && strncmp(path, "/api/menu", 9) == 0) {
        handleMenu(client);
        return;
    }
    if (strncmp(path, "/api/order", 10) == 0) {
        if (strcmp(method, "POST") == 0)
            handlePlaceOrder(client, bodyText);
        else if (strcmp(method, "GET") == 0)
            handleOrderStatus(client, path);
        else
            sendError(client, 405, "Method Not Allowed", "Wrong method");
        return;
    }
    if (strcmp(method, "GET") == 0 && strncmp(path, "/api/queue", 10) == 0) {
        handleQueue(client);
        return;
    }
    if (strcmp(method, "POST") == 0 && strncmp(path, "/api/admin", 10) == 0) {
        handleAdmin(client, bodyText);
        return;
    }

    /* --- everything else is a website file --- */
    if (strcmp(method, "GET") == 0) {
        char clean[300];
        char *question;
        snprintf(clean, sizeof(clean), "%s", path);
        question = strchr(clean, '?');      /* remove ?id=3 from the path */
        if (question != NULL)
            *question = '\0';
        sendFile(client, clean);
        return;
    }

    sendError(client, 405, "Method Not Allowed", "Wrong method");
}

/* Every browser connection runs inside its own thread. */
void *clientThread(void *argument)
{
    SOCKET client = (SOCKET)(intptr_t)argument;
    char *request = (char *)malloc(RECV_SIZE);

    if (request != NULL) {
        int size = recvRequest(client, request, RECV_SIZE);
        if (size > 0)
            handleRequest(client, request);
        free(request);
    }
    closesocket(client);
    return NULL;
}

/* ------------------------------------------------------------------
 * 13. MAIN - start the server
 * ------------------------------------------------------------------ */
int main(void)
{
    SOCKET serverSocket;
    int reuse = 1;
    int port = DEFAULT_PORT;
    const char *portEnv = getenv("PORT");
    struct sockaddr_in serverAddress;

    if (portEnv != NULL && atoi(portEnv) > 0 && atoi(portEnv) <= 65535)
        port = atoi(portEnv);

    /* A browser may close a connection while we are sending a response. */
    signal(SIGPIPE, SIG_IGN);

    printf("========================================================\n");
    printf("  SmartServe - Canteen Order Management System\n");
    printf("========================================================\n");
    printf("Loading menu (%d items)...\n", MAX_MENU);
    loadOrders();
    displayQueue();

    serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == INVALID_SOCKET) {
        perror("ERROR: could not create socket");
        return 1;
    }

    setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse));

    memset(&serverAddress, 0, sizeof(serverAddress));
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = htonl(INADDR_ANY);
    serverAddress.sin_port = htons((unsigned short)port);

    if (bind(serverSocket, (struct sockaddr *)&serverAddress, sizeof(serverAddress)) == SOCKET_ERROR) {
        printf("ERROR: could not bind port %d.\n", port);
        perror("bind");
        closesocket(serverSocket);
        return 1;
    }

    if (listen(serverSocket, 10) == SOCKET_ERROR) {
        perror("ERROR: listen failed");
        closesocket(serverSocket);
        return 1;
    }

    printf("\nSmartServe is running!\n");
    printf("Listening on 0.0.0.0:%d\n", port);
    printf("For local testing, open: http://localhost:%d\n", port);
    printf("Press Ctrl+C to stop the server.\n\n");
    fflush(stdout);

    while (1) {
        struct sockaddr_in clientAddress;
        socklen_t addressLength = sizeof(clientAddress);
        SOCKET clientSocket;
        pthread_t thread;

        clientSocket = accept(serverSocket, (struct sockaddr *)&clientAddress, &addressLength);
        if (clientSocket == INVALID_SOCKET)
            continue;

        if (pthread_create(&thread, NULL, clientThread, (void *)(intptr_t)clientSocket) == 0)
            pthread_detach(thread);
        else
            closesocket(clientSocket);
    }

    closesocket(serverSocket);
    return 0;
}
