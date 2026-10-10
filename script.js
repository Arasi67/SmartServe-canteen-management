/* =========================================================
   SmartServe - script.js
   Everything the visitor does in the browser:
   1. show the menu (fetched from the C server)
   2. cart + quantity buttons
   3. checkout form validation
   4. send the order to the C server (HTTP request)
   5. track order, token board and admin dashboard
   ========================================================= */

/* ---------- 1. STATE (memory of the page) ---------- */
let menuItems = [];            // food list received from the server
let cart = [];                 // { id, name, price, qty, category }
let activeCategory = "All";    // selected category filter
let activeBudget = 0;          // selected budget filter (0 = any)
let searchText = "";           // text inside the search bar

const COMBO_ID = 11;              // Fresh Lime Juice
const COMBO_DISCOUNT = 15;        // rupees saved with the combo rule
const HOT_CATEGORIES = ["Main Course", "Snacks"];
const STATUS_STEPS = ["Placed", "Preparing", "Ready", "Completed"];

/* short way to write document.getElementById(...) */
function $(id) {
  return document.getElementById(id);
}

/* rupee sign + number, e.g. money(60) -> ₹60 */
function money(value) {
  return "\u20B9" + value;
}

/* ---------- 2. SMALL SERVER HELPERS ---------- */

/* Shows the red banner when server.exe is not running */
function showServerError() {
  $("serverError").hidden = false;
}

/* GET request to the C server */
async function getData(url) {
  try {
    const response = await fetch(url);
    const data = await response.json();
    if (data.ok === false) {
      throw new Error(data.error);
    }
    return data;
  } catch (error) {
    if (error instanceof TypeError) {
      showServerError();   // fetch failed = server is down
      throw error;
    }
    throw error;
  }
}

/* POST request to the C server (body is JSON text) */
async function postData(url, body) {
  const response = await fetch(url, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body)
  });
  const data = await response.json();
  if (data.ok === false) {
    throw new Error(data.error);
  }
  return data;
}

/* ---------- 3. MENU ---------- */

/* Load all food items once when the page opens */
async function loadMenu() {
  try {
    const data = await getData("/api/menu");
    menuItems = data.menu;
    restoreCart();
    renderMenu();
    renderAvailability();
  } catch (error) {
    console.error("Menu could not be loaded:", error.message);
  }
}

/* Returns one food item by its id */
function getFood(id) {
  return menuItems.find(function (item) { return item.id === id; });
}

/* Draw the food cards after applying search + category + budget filters */
function renderMenu() {
  const grid = $("foodGrid");
  let html = "";

  menuItems.forEach(function (item) {
    const inCategory = activeCategory === "All" || item.category === activeCategory;
    const inBudget = activeBudget === 0 || item.price <= activeBudget;
    const inSearch = searchText === "" ||
      item.name.toLowerCase().includes(searchText) ||
      item.description.toLowerCase().includes(searchText);

    if (!inCategory || !inBudget || !inSearch) return;

    html += `
      <div class="food-card">
        <div class="food-img">
          <img src="${item.image}" alt="${item.name}">
          <div class="badges">
            ${item.popular ? '<span class="badge popular">Popular</span>' : ""}
            ${item.available ? "" : '<span class="badge sold-out">Sold Out</span>'}
          </div>
        </div>
        <div class="food-body">
          <div class="food-top">
            <span class="food-name">${item.name}</span>
            <span class="food-price">${money(item.price)}</span>
          </div>
          <p class="food-desc">${item.description}</p>
          <div class="food-meta">
            <span class="prep-time">${item.prepTime} min prep &middot; ${item.category}</span>
            <button class="add-btn" data-add="${item.id}" ${item.available ? "" : "disabled"}>
              ${item.available ? "Add to Cart" : "Sold Out"}
            </button>
          </div>
        </div>
      </div>`;
  });

  grid.innerHTML = html;
  $("emptyMsg").hidden = html !== "";
}

/* One click on "Add to Cart" */
function addToCart(id) {
  const food = getFood(id);
  if (!food || !food.available) return;

  const found = cart.find(function (item) { return item.id === id; });
  if (found) {
    if (found.qty < 10) found.qty += 1;
  } else {
    cart.push({ id: food.id, name: food.name, price: food.price, qty: 1, category: food.category });
  }
  saveCart();
  renderCart();
}

/* + and - buttons inside the cart */
function changeQty(id, change) {
  const item = cart.find(function (c) { return c.id === id; });
  if (!item) return;
  item.qty += change;
  if (item.qty <= 0) {
    cart = cart.filter(function (c) { return c.id !== id; });
  }
  saveCart();
  renderCart();
}

/* rule based combo: hot snack + lime juice = ₹15 off.
   Same rule exists in server.c (comboSaving function). */
function comboDiscount() {
  const hasHotFood = cart.some(function (item) {
    return HOT_CATEGORIES.includes(item.category);
  });
  const hasLimeJuice = cart.some(function (item) { return item.id === COMBO_ID; });
  return (hasHotFood && hasLimeJuice) ? COMBO_DISCOUNT : 0;
}

/* Cart numbers: subtotal, combo discount, total, longest prep time */
function cartTotals() {
  let subtotal = 0;
  let maxPrep = 0;
  cart.forEach(function (item) {
    subtotal += item.price * item.qty;
    const food = getFood(item.id);
    if (food && food.prepTime > maxPrep) maxPrep = food.prepTime;
  });
  const discount = comboDiscount();
  return { subtotal: subtotal, discount: discount, total: subtotal - discount, maxPrep: maxPrep };
}

/* Redraw the cart panel and the cart button counter */
function renderCart() {
  const box = $("cartItems");
  const count = cart.reduce(function (sum, item) { return sum + item.qty; }, 0);
  $("cartCount").textContent = count;

  if (cart.length === 0) {
    box.innerHTML = '<p class="empty-msg">Your cart is empty.</p>';
  } else {
    box.innerHTML = cart.map(function (item) {
      return `
        <div class="cart-item">
          <div class="ci-info">
            <h4>${item.name}</h4>
            <p>${money(item.price * item.qty)}</p>
          </div>
          <div class="qty-box">
            <button class="qty-btn" data-qty="${item.id}" data-change="-1">&minus;</button>
            <span class="qty-value">${item.qty}</span>
            <button class="qty-btn" data-qty="${item.id}" data-change="1">+</button>
          </div>
        </div>`;
    }).join("");
  }

  const totals = cartTotals();
  $("subtotal").textContent = money(totals.subtotal);
  $("comboRow").hidden = totals.discount === 0;
  $("cartTotal").textContent = money(totals.total);
  updateWaitHint(totals.maxPrep);
  showComboBanner();
}

/* Approximate waiting time = orders already in queue x 5 min + your prep time */
function updateWaitHint(maxPrep) {
  const hint = $("waitHint");
  if (cart.length === 0) {
    hint.textContent = "Add items to see waiting time.";
    return;
  }
  const ahead = lastQueueLength;
  const minutes = ahead * 5 + (maxPrep || 5);
  hint.innerHTML = "About <b>" + minutes + " min</b> &middot; " +
    ahead + " order" + (ahead === 1 ? "" : "s") + " ahead in the queue";
}

/* rule based combo suggestion banner (no AI involved) */
function showComboBanner() {
  const banner = $("comboBanner");
  const hasHot = cart.some(function (item) { return HOT_CATEGORIES.includes(item.category); });
  const hasLime = cart.some(function (item) { return item.id === COMBO_ID; });
  banner.hidden = !(hasHot && !hasLime);
}

/* keep the cart in localStorage so a refresh does not lose it */
function saveCart() {
  try {
    localStorage.setItem("smartserve_cart", JSON.stringify(cart));
  } catch (error) { /* private mode - ignore */ }
}

function restoreCart() {
  try {
    const saved = localStorage.getItem("smartserve_cart");
    if (saved) {
      const parsed = JSON.parse(saved);
      cart = parsed.filter(function (item) { return getFood(item.id); });
    }
  } catch (error) {
    cart = [];
  }
}

/* ---------- 4. CHECKOUT ---------- */

/* Open the checkout window with the current order summary */
function openCheckout() {
  if (cart.length === 0) {
    openCart();
    return;
  }
  const totals = cartTotals();
  $("checkoutSummary").innerHTML = cart.map(function (item) {
    return `<div class="r-item"><span>${item.qty} x ${item.name}</span><span>${money(item.price * item.qty)}</span></div>`;
  }).join("") +
  (totals.discount ? `<div class="r-item"><span>Combo saving</span><span>&minus;${money(totals.discount)}</span></div>` : "");
  $("checkoutTotal").textContent = money(totals.total);

  $("cartPanel").classList.remove("open");
  $("overlay").hidden = true;
  $("checkoutModal").hidden = false;
}

function closeCheckout() {
  $("checkoutModal").hidden = true;
}

/* Show the table number box only for Dine-in */
function updateTableField() {
  const type = document.querySelector('input[name="orderType"]:checked').value;
  $("tableField").hidden = type !== "Dine-in";
}

/* Check the form in the browser (the C server checks it again) */
function validateForm() {
  const name = $("custName").value.trim();
  const studentId = $("custId").value.trim();
  const dept = $("custDept").value;
  const mobile = $("custMobile").value.trim();
  const type = document.querySelector('input[name="orderType"]:checked').value;
  const table = $("custTable").value.trim();

  let ok = true;
  $("errName").textContent = "";
  $("errId").textContent = "";
  $("errDept").textContent = "";
  $("errMobile").textContent = "";
  $("errTable").textContent = "";

  if (name.length < 3) {
    $("errName").textContent = "Please enter your full name";
    ok = false;
  }
  if (studentId.length < 3) {
    $("errId").textContent = "Please enter a valid ID";
    ok = false;
  }
  if (dept === "") {
    $("errDept").textContent = "Please select your department";
    ok = false;
  }
  if (!/^[6-9][0-9]{9}$/.test(mobile)) {
    $("errMobile").textContent = "Enter a valid 10 digit mobile number";
    ok = false;
  }
  if (type === "Dine-in" && table === "") {
    $("errTable").textContent = "Table number is required";
    ok = false;
  }
  return ok;
}

/* Send the order to the C backend (Queue = enqueue happens there) */
async function placeOrder(event) {
  event.preventDefault();
  $("errForm").textContent = "";

  if (!validateForm()) return;

  const type = document.querySelector('input[name="orderType"]:checked').value;
  const payload = {
    customerName: $("custName").value.trim(),
    studentId: $("custId").value.trim(),
    department: $("custDept").value,
    mobile: $("custMobile").value.trim(),
    orderType: type,
    tableNumber: type === "Dine-in" ? $("custTable").value.trim() : "",
    specialInstructions: $("custNotes").value.trim(),
    itemIds: cart.map(function (item) { return item.id; }),
    quantities: cart.map(function (item) { return item.qty; })
  };

  const button = $("placeOrderBtn");
  button.disabled = true;
  button.textContent = "Placing...";

  try {
    const data = await postData("/api/order", payload);

    /* the server answer becomes our order receipt */
    const order = {
      orderId: data.orderId,
      token: data.token,
      status: data.status,
      total: data.total,
      comboSaving: data.comboSaving,
      queuePosition: data.queuePosition,
      ordersAhead: data.ordersAhead,
      estimatedWait: data.estimatedWait,
      items: data.items,
      customerName: payload.customerName,
      studentId: payload.studentId,
      department: payload.department,
      mobile: payload.mobile,
      orderType: payload.orderType,
      tableNumber: payload.tableNumber,
      specialInstructions: payload.specialInstructions,
      placedTime: new Date().toTimeString().slice(0, 5)
    };

    /* the cart is empty again after a successful order */
    cart = [];
    saveCart();
    renderCart();
    closeCheckout();

    $("confirmReceipt").innerHTML = buildReceipt(order);
    $("confirmModal").hidden = false;
    refreshQueue();
  } catch (error) {
    $("errForm").textContent = error.message || "Could not place the order";
  } finally {
    button.disabled = false;
    button.textContent = "Place Order";
  }
}

/* ---------- 5. RECEIPT (used after ordering and in Track Order) ---------- */

function stepsHtml(status) {
  const current = STATUS_STEPS.indexOf(status);
  let html = '<div class="steps">';
  STATUS_STEPS.forEach(function (step, index) {
    let cls = "";
    if (index < current) cls = "done";
    else if (index === current) cls = "current";
    html += `<div class="step ${cls}"><div class="dot">${index + 1}</div>${step}</div>`;
  });
  return html + "</div>";
}

function buildReceipt(order) {
  const itemsHtml = order.items.map(function (item) {
    return `<div class="r-item"><span>${item.qty} x ${item.name}</span><span>${money(item.price * item.qty)}</span></div>`;
  }).join("");

  const comboRow = order.comboSaving
    ? `<div class="r-item"><span>Combo saving</span><span>&minus;${money(order.comboSaving)}</span></div>`
    : "";

  const waitText = order.status === "Placed"
    ? `Estimated waiting time: <b>${order.estimatedWait} minutes</b> &middot; ${order.ordersAhead !== undefined ? order.ordersAhead : (order.queuePosition - 1)} order(s) ahead in the FIFO queue`
    : (order.status === "Preparing"
      ? "Your order is being prepared in the kitchen."
      : (order.status === "Ready" ? "Your order is ready! Collect it from the counter." : "Order completed. Thank you!"));

  return `
    <div class="receipt-head">
      <div>
        <h4>Order #${order.orderId}</h4>
        <p>Token <b>#${order.token}</b> &middot; ${order.placedTime}</p>
      </div>
      <span class="status-chip status-${order.status.toLowerCase()}">${order.status}</span>
    </div>

    ${stepsHtml(order.status)}

    <div class="receipt-items">${itemsHtml}${comboRow}</div>
    <div class="receipt-total"><span>Total</span><span>${money(order.total)}</span></div>

    <div class="receipt-info">
      <p><span>Name</span><b>${order.customerName}</b></p>
      <p><span>ID</span><b>${order.studentId}</b></p>
      <p><span>Department</span><b>${order.department}</b></p>
      <p><span>Mobile</span><b>${order.mobile}</b></p>
      <p><span>Type</span><b>${order.orderType}${order.tableNumber ? " - Table " + order.tableNumber : ""}</b></p>
      ${order.specialInstructions ? `<p><span>Note</span><b>${order.specialInstructions}</b></p>` : ""}
    </div>

    <div class="wait-box">${waitText}</div>`;
}

/* Track Order section */
async function trackOrder() {
  const id = $("trackIdInput").value.trim();
  const result = $("trackResult");
  if (id === "") {
    result.hidden = false;
    result.innerHTML = '<p class="empty-msg">Please enter your order ID.</p>';
    return;
  }
  try {
    const data = await getData("/api/order?id=" + encodeURIComponent(id));
    result.hidden = false;
    result.innerHTML = buildReceipt(data.order);
  } catch (error) {
    result.hidden = false;
    result.innerHTML = '<p class="empty-msg">' + (error.message || "Order not found") + "</p>";
  }
}

/* ---------- 6. TOKEN BOARD + ADMIN (one refresh for both) ---------- */
let lastQueueLength = 0;

async function refreshQueue() {
  let data;
  try {
    data = await getData("/api/queue");
  } catch (error) {
    return;
  }

  lastQueueLength = data.queue.length;

  /* --- token board --- */
  const preparing = data.active.filter(function (o) { return o.status === "Preparing"; });
  if (preparing.length === 0) {
    $("boardPreparing").innerHTML = '<span class="token-chip">----</span>';
  } else {
    $("boardPreparing").innerHTML = preparing.map(function (o) {
      return `<span class="token-big">#${o.token}</span>`;
    }).join("");
  }

  if (data.queue.length === 0) {
    $("boardQueue").innerHTML = '<span class="token-chip">Queue empty</span>';
  } else {
    $("boardQueue").innerHTML = data.queue.slice(0, 10).map(function (o) {
      return `<span class="token-chip">#${o.token}<small>${o.customerName}</small></span>`;
    }).join("");
  }
  $("boardMeta").innerHTML = "Waiting orders: <b>" + data.queue.length +
    "</b> &middot; Avg. 5 minutes per order";

  /* --- admin cards --- */
  $("statWaiting").textContent = data.counts.waiting;
  $("statPreparing").textContent = data.counts.preparing;
  $("statReady").textContent = data.counts.ready;
  $("statCompleted").textContent = data.counts.completed;

  /* --- admin: waiting queue (FIFO order) --- */
  const queueBox = $("queueList");
  if (data.queue.length === 0) {
    queueBox.innerHTML = '<p class="empty-msg">No orders waiting right now.</p>';
  } else {
    queueBox.innerHTML = data.queue.map(function (o) {
      return `
        <div class="queue-item">
          <div class="q-pos">${o.position}</div>
          <div class="q-info">
            <h4>#${o.token} &middot; ${o.customerName} <span class="status-chip status-placed">${o.status}</span></h4>
            <p>Order #${o.orderId} &middot; ${o.itemsSummary} &middot; ${money(o.total)} &middot; ${o.orderType}${o.tableNumber ? " T" + o.tableNumber : ""} &middot; ${o.placedTime}</p>
          </div>
          <div class="q-actions">
            <button class="btn-mini go" data-status="Preparing" data-order="${o.orderId}">Start Preparing</button>
          </div>
        </div>`;
    }).join("");
  }

  /* --- admin: orders in kitchen / ready --- */
  const activeBox = $("activeList");
  if (data.active.length === 0) {
    activeBox.innerHTML = '<p class="empty-msg">No order is being prepared right now.</p>';
  } else {
    activeBox.innerHTML = data.active.map(function (o) {
      const next = o.status === "Preparing" ? "Ready" : "Completed";
      const label = o.status === "Preparing" ? "Mark Ready" : "Mark Completed";
      return `
        <div class="queue-item">
          <div class="q-pos">${o.token}</div>
          <div class="q-info">
            <h4>#${o.token} &middot; ${o.customerName} <span class="status-chip status-${o.status.toLowerCase()}">${o.status}</span></h4>
            <p>Order #${o.orderId} &middot; placed at ${o.placedTime}</p>
          </div>
          <div class="q-actions">
            <button class="btn-mini go" data-status="${next}" data-order="${o.orderId}">${label}</button>
          </div>
        </div>`;
    }).join("");
  }

  renderCart();   /* updates the waiting time hint inside the cart */
}

/* Admin button: change the status of one order */
async function changeOrderStatus(orderId, status) {
  try {
    await postData("/api/admin", { action: "status", orderId: orderId, status: status });
    refreshQueue();
  } catch (error) {
    alert("Could not update the order: " + error.message);
  }
}

/* Admin button: available / sold out for one food item */
async function toggleAvailability(itemId, available) {
  try {
    await postData("/api/admin", {
      action: "availability",
      itemId: itemId,
      available: available ? 1 : 0
    });
    const food = getFood(itemId);
    if (food) food.available = available ? 1 : 0;
    renderMenu();
    renderAvailability();
  } catch (error) {
    alert("Could not update: " + error.message);
  }
}

/* Admin: list of food items with an available / sold out button */
function renderAvailability() {
  const box = $("availabilityList");
  if (!box) return;
  box.innerHTML = menuItems.map(function (item) {
    const isOn = item.available === 1 || item.available === true;
    return `
      <div class="avail-item">
        <div>
          <p>${item.name}</p>
          <small>${money(item.price)} &middot; ${item.category}</small>
        </div>
        <button class="btn-mini ${isOn ? "on" : "off"}" data-avail="${item.id}" data-value="${isOn ? 0 : 1}">
          ${isOn ? "Available" : "Sold Out"}
        </button>
      </div>`;
  }).join("");
}

/* ---------- 7. PAGE EVENTS ---------- */

/* Cart panel open / close */
function openCart() {
  $("cartPanel").classList.add("open");
  $("overlay").hidden = false;
}

function closeCart() {
  $("cartPanel").classList.remove("open");
  $("overlay").hidden = true;
}

function initEvents() {
  $("cartBtn").addEventListener("click", openCart);
  $("closeCartBtn").addEventListener("click", closeCart);
  $("overlay").addEventListener("click", closeCart);
  $("checkoutBtn").addEventListener("click", openCheckout);
  $("closeCheckoutBtn").addEventListener("click", closeCheckout);
  /* the form "submit" event covers both the button and the Enter key */
  $("checkoutForm").addEventListener("submit", placeOrder);
  $("trackBtn").addEventListener("click", trackOrder);
  $("hamburgerBtn").addEventListener("click", function () {
    $("navLinks").classList.toggle("open");
  });

  /* close the confirmation window and go to the menu */
  $("continueBtn").addEventListener("click", function () {
    $("confirmModal").hidden = true;
  });
  $("goTrackBtn").addEventListener("click", function () {
    $("confirmModal").hidden = true;
    document.getElementById("track").scrollIntoView({ behavior: "smooth" });
  });

  /* Dine-in / Takeaway radio buttons */
  document.querySelectorAll('input[name="orderType"]').forEach(function (radio) {
    radio.addEventListener("change", updateTableField);
  });

  /* search bar */
  $("searchBtn").addEventListener("click", function () {
    searchText = $("searchInput").value.trim().toLowerCase();
    renderMenu();
    document.getElementById("menu").scrollIntoView({ behavior: "smooth" });
  });
  $("searchInput").addEventListener("input", function () {
    searchText = this.value.trim().toLowerCase();
    renderMenu();
  });
  $("searchInput").addEventListener("keydown", function (event) {
    if (event.key === "Enter") {
      document.getElementById("menu").scrollIntoView({ behavior: "smooth" });
    }
  });

  /* category filter pills */
  $("categoryFilters").addEventListener("click", function (event) {
    const button = event.target.closest("[data-category]");
    if (!button) return;
    activeCategory = button.dataset.category;
    document.querySelectorAll("#categoryFilters .pill").forEach(function (p) {
      p.classList.remove("active");
    });
    button.classList.add("active");
    renderMenu();
  });

  /* budget filter pills */
  $("budgetFilters").addEventListener("click", function (event) {
    const button = event.target.closest("[data-budget]");
    if (!button) return;
    activeBudget = parseInt(button.dataset.budget, 10);
    document.querySelectorAll("#budgetFilters .pill").forEach(function (p) {
      p.classList.remove("active");
    });
    button.classList.add("active");
    renderMenu();
  });

  /* buttons inside the food grid (Add to Cart) */
  $("foodGrid").addEventListener("click", function (event) {
    const button = event.target.closest("[data-add]");
    if (!button) return;
    addToCart(parseInt(button.dataset.add, 10));
    openCart();
  });

  /* + and - buttons inside the cart */
  $("cartItems").addEventListener("click", function (event) {
    const button = event.target.closest("[data-qty]");
    if (!button) return;
    changeQty(parseInt(button.dataset.qty, 10), parseInt(button.dataset.change, 10));
  });

  /* combo suggestion button */
  $("comboAddBtn").addEventListener("click", function () {
    addToCart(COMBO_ID);
  });

  /* admin buttons (status change + availability) */
  $("admin").addEventListener("click", function (event) {
    const statusBtn = event.target.closest("[data-status]");
    if (statusBtn) {
      changeOrderStatus(parseInt(statusBtn.dataset.order, 10), statusBtn.dataset.status);
      return;
    }
    const availBtn = event.target.closest("[data-avail]");
    if (availBtn) {
      toggleAvailability(parseInt(availBtn.dataset.avail, 10), availBtn.dataset.value === "1");
    }
  });

  /* Track Order: Enter key also searches */
  $("trackIdInput").addEventListener("keydown", function (event) {
    if (event.key === "Enter") trackOrder();
  });
}

/* ---------- 8. START ---------- */
function init() {
  initEvents();
  loadMenu();
  renderCart();
  updateTableField();
  refreshQueue();          /* first load of the token board + admin */
  setInterval(refreshQueue, 3000);   /* live refresh every 3 seconds */
}

document.addEventListener("DOMContentLoaded", init);
