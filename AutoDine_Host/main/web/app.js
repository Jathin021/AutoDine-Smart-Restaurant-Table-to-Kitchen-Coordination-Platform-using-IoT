// AutoDine Dashboard JavaScript

let tables = [];
let updateInterval;

// Initialize dashboard
document.addEventListener('DOMContentLoaded', () => {
    fetchTables();
    updateInterval = setInterval(fetchTables, 2000);
    setupModal();
    updateTime();
    setInterval(updateTime, 1000);
});

// Update current time display
function updateTime() {
    const now = new Date();
    const timeStr = now.toLocaleTimeString('en-US', { hour12: false });
    document.getElementById('current-time').textContent = timeStr;
}

// Fetch tables data from server
async function fetchTables() {
    try {
        const response = await fetch('/api/dashboard/tables');
        if (!response.ok) throw new Error('Network error');

        tables = await response.json();
        renderTables();
        updateConnectionStatus(true);
    } catch (error) {
        console.error('Failed to fetch tables:', error);
        updateConnectionStatus(false);
    }
}

// Update connection status indicator
function updateConnectionStatus(connected) {
    const statusElem = document.getElementById('connection-status');
    if (connected) {
        statusElem.textContent = '● Connected';
        statusElem.style.color = '#4ade80';
    } else {
        statusElem.textContent = '● Disconnected';
        statusElem.style.color = '#f87171';
    }
}

// Render all tables
function renderTables() {
    const container = document.getElementById('tables-container');
    if (!container) return;

    container.innerHTML = '';

    tables.forEach(table => {
        const card = createTableCard(table);
        container.appendChild(card);
    });
}

// Create a table card element
function createTableCard(table) {
    const card = document.createElement('div');
    card.className = `table-card status-${table.status}`;

    const statusText = getStatusText(table.status);
    const statusClass = table.status;

    card.innerHTML = `
        <div class="table-header">
            <h2>Table ${table.table_id}</h2>
            <span class="status-badge ${statusClass}">${statusText}</span>
        </div>
        ${renderTableContent(table)}
    `;

    return card;
}

// Get human-readable status text
function getStatusText(status) {
    const statusMap = {
        'idle': 'Available',
        'cooking': 'Cooking',
        'prepared': 'Ready',
        'billing': 'Billing',
        'payment': 'Payment'
    };
    return statusMap[status] || status;
}

// Render table content based on state
function renderTableContent(table) {
    // Show "No active orders" only if truly idle (no pending orders)
    if (table.status === 'idle' && table.order_state === 'none') {
        return '<div class="no-order">No active orders</div>';
    }

    let content = '';

    // Show order items if available
    if (table.items && table.items.length > 0) {
        content += '<div class="order-info">';
        content += `<h3>Order #${table.order_id}</h3>`;

        // Show full restaurant bill when in billing/payment state
        if (table.status === 'billing' || table.status === 'payment') {
            content += '<div class="restaurant-bill">';
            content += '<div class="bill-header">BILL</div>';
            content += '<table class="bill-table">';
            content += '<thead><tr><th>Item</th><th>Qty</th><th>Price</th><th>Amount</th></tr></thead>';
            content += '<tbody>';

            let subtotal = 0;
            table.items.forEach(item => {
                const amount = item.price * item.qty;
                subtotal += amount;
                content += `<tr>
                    <td>${item.name}</td>
                    <td>${item.qty}</td>
                    <td>₹${item.price}</td>
                    <td>₹${amount}</td>
                </tr>`;
            });

            const gst = Math.round(subtotal * 0.18);
            const grandTotal = subtotal + gst;

            content += '</tbody><tfoot>';
            content += `<tr class="subtotal-row"><td colspan="3">Subtotal</td><td>₹${subtotal}</td></tr>`;
            content += `<tr class="gst-row"><td colspan="3">GST (18%)</td><td>₹${gst}</td></tr>`;
            content += `<tr class="total-row"><td colspan="3"><strong>GRAND TOTAL</strong></td><td><strong>₹${grandTotal}</strong></td></tr>`;
            content += '</tfoot></table></div>';
        } else {
            // Regular order list view
            content += '<ul class="order-items">';
            table.items.forEach(item => {
                content += `<li>
                    <span>${item.name} x${item.qty}</span>
                    <span>₹${item.price * item.qty}</span>
                </li>`;
            });
            content += '</ul>';

            if (table.total) {
                content += `<div class="order-total">
                    <span>Total:</span>
                    <span>₹${table.total}</span>
                </div>`;
            }
        }

        content += '</div>';
    }

    // Show payment method if set
    if (table.payment_method) {
        content += `<div class="payment-info">
            Payment Method: ${table.payment_method.toUpperCase()}
        </div>`;
    }

    // Action buttons based on state
    content += '<div class="action-buttons">';
    content += getActionButtons(table);
    content += '</div>';

    return content;
}

// Get action buttons for current state
function getActionButtons(table) {
    let buttons = '';

    if (table.order_state === 'pending') {
        buttons += `<button class="btn btn-accept" onclick="acceptOrder(${table.order_id})">Accept</button>`;
        buttons += `<button class="btn btn-decline" onclick="declineOrder(${table.order_id})">Decline</button>`;
    }

    if (table.order_state === 'accepted' && table.status === 'cooking') {
        buttons += `<button class="btn btn-prepared" onclick="markPrepared(${table.order_id})">Food Prepared</button>`;
    }

    if (table.status === 'prepared') {
        buttons += `<button class="btn btn-generate" onclick="generateBill(${table.table_id})">Generate Bill</button>`;
    }

    if (table.status === 'billing') {
        buttons += `<div class="bill-display">`;
        buttons += `<h4>Bill Generated</h4>`;
        buttons += `<div class="bill-waiting">Waiting for payment method...</div>`;
        buttons += `</div>`;
    }

    if (table.status === 'payment') {
        buttons += `<button class="btn btn-verify" onclick="verifyPayment(${table.table_id})">Verify Payment</button>`;
    }

    return buttons;
}

// Accept order
async function acceptOrder(orderId) {
    try {
        const response = await fetch('/api/chef/accept', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ order_id: orderId })
        });

        if (response.ok) {
            console.log('Order accepted');
            fetchTables();
        }
    } catch (error) {
        console.error('Failed to accept order:', error);
        alert('Failed to accept order');
    }
}

// Decline order
async function declineOrder(orderId) {
    if (!confirm('Decline this order?')) return;

    try {
        const response = await fetch('/api/chef/decline', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ order_id: orderId })
        });

        if (response.ok) {
            console.log('Order declined');
            fetchTables();
        }
    } catch (error) {
        console.error('Failed to decline order:', error);
        alert('Failed to decline order');
    }
}

// Mark food as prepared
async function markPrepared(orderId) {
    try {
        const response = await fetch('/api/chef/food_prepared', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ order_id: orderId })
        });

        if (response.ok) {
            console.log('Marked as prepared');
            fetchTables();
        }
    } catch (error) {
        console.error('Failed to mark prepared:', error);
        alert('Failed to mark as prepared');
    }
}

// Verify payment
async function verifyPayment(tableId) {
    if (!confirm('Confirm payment received?')) return;

    try {
        const response = await fetch('/api/chef/verify_payment', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ table_id: tableId })
        });

        if (response.ok) {
            console.log('Payment verified');
            fetchTables();
        }
    } catch (error) {
        console.error('Failed to verify payment:', error);
        alert('Failed to verify payment');
    }
}

async function generateBill(tableId) {
    try {
        const response = await fetch('/api/request_bill', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ table_id: tableId })
        });

        if (response.ok) {
            console.log('Bill generated');
            fetchTables();
        }
    } catch (error) {
        console.error('Failed to generate bill:', error);
        alert('Failed to generate bill');
    }
}

// Modal Setup
function setupModal() {
    const modal = document.getElementById('order-modal');
    const closeBtn = document.getElementsByClassName('close')[0];

    if (closeBtn) {
        closeBtn.onclick = function () {
            modal.style.display = 'none';
        };
    }

    window.onclick = function (event) {
        if (event.target == modal) {
            modal.style.display = 'none';
        }
    };
}
