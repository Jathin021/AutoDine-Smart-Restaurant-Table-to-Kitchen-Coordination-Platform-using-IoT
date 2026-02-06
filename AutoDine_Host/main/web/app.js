// Global state
let tables = [];
let refreshInterval = null;

// Initialize on page load
document.addEventListener('DOMContentLoaded', function() {
    console.log('AutoDine Dashboard Initialized');
    updateTime();
    setInterval(updateTime, 1000);
    
    fetchTables();
    refreshInterval = setInterval(fetchTables, 2000);
    
    setupModal();
});

// Update current time
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

// Render all table cards
function renderTables() {
    const container = document.getElementById('tables-container');
    
    if (tables.length === 0) {
        container.innerHTML = '<div class="loading">Loading tables</div>';
        return;
    }
    
    container.innerHTML = '';
    
    tables.forEach(table => {
        const card = createTableCard(table);
        container.appendChild(card);
    });
}

// Create individual table card
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
    if (table.status === 'idle') {
        return '<div class="no-order">No active orders</div>';
    }
    
    let content = '';
    
    // Show order items if available
    if (table.items && table.items.length > 0) {
        content += '<div class="order-info">';
        content += `<h3>Order #${table.order_id}</h3>`;
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
    
    if (table.status === 'payment') {
        buttons += `<button class="btn btn-verify" onclick="verifyPayment(${table.table_id})">Verify Payment</button>`;
    }
    
    return buttons;
}

// API Actions
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

async function declineOrder(orderId) {
    if (!confirm('Are you sure you want to decline this order?')) return;
    
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

async function markPrepared(orderId) {
    try {
        const response = await fetch('/api/chef/food_prepared', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ order_id: orderId })
        });
        
        if (response.ok) {
            console.log('Order marked as prepared');
            fetchTables();
        }
    } catch (error) {
        console.error('Failed to mark prepared:', error);
        alert('Failed to mark as prepared');
    }
}

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

// Modal Setup
function setupModal() {
    const modal = document.getElementById('order-modal');
    const closeBtn = document.querySelector('.close');
    
    closeBtn.onclick = function() {
        modal.style.display = 'none';
    };
    
    window.onclick = function(event) {
        if (event.target == modal) {
            modal.style.display = 'none';
        }
    };
}
