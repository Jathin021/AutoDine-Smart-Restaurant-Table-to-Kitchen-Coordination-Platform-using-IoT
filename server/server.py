"""
AutoDine V4.0 — Flask Server (Firebase Edition)
Run: python server.py
Dashboard: http://localhost:5000

Firebase Setup:
  1. Go to https://console.firebase.google.com
  2. Create project → Project Settings → Service accounts → Generate new private key
  3. Save the JSON as  server/firebase_credentials.json
  4. Enable Firestore Database in your Firebase project (Native mode)
"""

import os, time, json, hashlib, base64, threading, urllib.request, queue
from datetime import datetime, date
from functools import wraps

# Load .env file (Razorpay keys, HOST_UNIT_IP, etc.)
try:
    from dotenv import load_dotenv
    load_dotenv(os.path.join(os.path.dirname(__file__), '.env'))
except ImportError:
    pass  # python-dotenv not installed; keys must be set as real env vars

from flask import Flask, request, jsonify, render_template, session, redirect, url_for, Response, stream_with_context
from flask_cors import CORS

# ── Firebase Admin SDK ────────────────────────────────────────────────
import firebase_admin
from firebase_admin import credentials, firestore

# ── Optional QR code ──────────────────────────────────────────────────
try:
    import qrcode, io
    QR_AVAILABLE = True
except ImportError:
    QR_AVAILABLE = False

def _get_qr_matrix(data):
    """Generate a bitmask string (0/1) for the QR code."""
    try:
        qr = qrcode.QRCode(version=1, box_size=1, border=1)
        qr.add_data(data)
        qr.make(fit=True)
        matrix = qr.get_matrix()
        flat = ""
        for row in matrix:
            for module in row:
                flat += "1" if module else "0"
        return {"matrix": flat, "size": len(matrix)}
    except Exception as e:
        print(f"QR Matrix Error: {e}")
        return {"matrix": "0", "size": 1}

# ── Optional Razorpay (graceful if not installed / not configured) ────
try:
    import razorpay
    RAZORPAY_AVAILABLE = True
except ImportError:
    RAZORPAY_AVAILABLE = False

# ── SSE Dashboard Notification System ──
dashboard_queues = []

def notify_dashboard(event_type, data):
    """Sends real-time updates to all connected chef dashboard clients."""
    payload = {"type": event_type, "data": data}
    msg = json.dumps(payload)
    for q in dashboard_queues[:]:
        try:
            q.put_nowait(msg)
        except Exception:
            if q in dashboard_queues: dashboard_queues.remove(q)

# ═════════════════════════════════════════════════════════════════════
#  CONFIG  — fill in your values
# ═════════════════════════════════════════════════════════════════════
# ── Set these via env vars in production ──────────────────────────
RAZORPAY_KEY_ID     = os.environ.get("RAZORPAY_KEY_ID",     "YOUR_KEY_ID")
RAZORPAY_KEY_SECRET = os.environ.get("RAZORPAY_KEY_SECRET", "YOUR_KEY_SECRET")
HOST_UNIT_IP        = os.environ.get("HOST_UNIT_IP",        "172.20.10.3")   # ESP32 Host Unit IP

CHEF_USERNAME  = "chef"
CHEF_PASSWORD  = "autodine"
OWNER_USERNAME = "jathin"
OWNER_PASSWORD = "autodine"
OWNER_NAME     = "Jathin Pusuluri"

# UPI details for static QR code shown on table device
UPI_ID          = "p.jathin021@okaxis"
UPI_NAME        = "AutoDine Restaurant"
STATIC_QR_PATH  = os.path.join(os.path.dirname(__file__), "static_qr.png")

# ── SSE Dashboard Notification System ──
dashboard_queues = []

def notify_dashboard(event_type, data):
    """Sends real-time updates to all connected chef dashboard clients."""
    payload = {"type": event_type, "data": data}
    msg = json.dumps(payload)
    for q in dashboard_queues[:]:
        try:
            q.put_nowait(msg)
        except Exception:
            if q in dashboard_queues: dashboard_queues.remove(q)

# Firebase credentials file (download from Firebase console)
FIREBASE_CRED_PATH = os.path.join(os.path.dirname(__file__), "firebase_credentials.json")

# ═════════════════════════════════════════════════════════════════════
#  FIREBASE INIT
# ═════════════════════════════════════════════════════════════════════
_fb_app = None
_db     = None

def _init_firebase():
    global _fb_app, _db
    if not os.path.exists(FIREBASE_CRED_PATH):
        print(f"❌ ERROR: Firebase credentials missing at {FIREBASE_CRED_PATH}")
        return
    try:
        cred    = credentials.Certificate(FIREBASE_CRED_PATH)
        _fb_app = firebase_admin.initialize_app(cred)
        _db     = firestore.client()
        # Proactive connection test: Ping the database
        list(_db.collection("menu").limit(1).stream())
        print("✓ Firebase Firestore: Connection Verified!")
    except Exception as e:
        print(f"❌ Firebase Connection Failed: {e}")
        _db = None

def get_db():
    """Return Firestore client (already initialised at startup)."""
    return _db

# ── Firestore collection helpers ──────────────────────────────────────
def _col(name):     return get_db().collection(name)
def _doc(col, did): return get_db().collection(col).document(str(did))

# auto-increment counters stored in 'counters' collection
def _next_id(name: str) -> int:
    ref = get_db().collection("counters").document(name)
    @firestore.transactional
    def _txn(transaction):
        snap = ref.get(transaction=transaction)
        val  = (snap.to_dict() or {}).get("val", 0) + 1
        transaction.set(ref, {"val": val})
        return val
    return _txn(get_db().transaction())

# ═════════════════════════════════════════════════════════════════════
#  SEED MENU (runs once if menu collection is empty)
# ═════════════════════════════════════════════════════════════════════
SEED_MENU = [
    ("Paneer Tikka",    "Chargrilled cottage cheese",    180, "Starters",       True),
    ("Chicken 65",      "Spicy fried chicken",           160, "Starters",       False),
    ("Veg Biryani",     "Fragrant basmati with veggies", 220, "Biryani & Rice", True),
    ("Chicken Biryani", "Lucknowi dum biryani",          280, "Biryani & Rice", False),
    ("Dal Makhani",     "Creamy black lentils",          160, "Main Course",    True),
    ("Butter Chicken",  "Classic murgh makhani",         260, "Main Course",    False),
    ("Mango Lassi",     "Chilled mango yogurt drink",     80, "Drinks",         True),
    ("Cold Coffee",     "Blended iced coffee",            90, "Drinks",         True),
    ("Gulab Jamun",     "Soft milk-solid dumplings",      60, "Desserts",       True),
    ("Ice Cream",       "3 scoops assorted flavours",     80, "Desserts",       True),
    ("Masala Dosa",     "Crispy dosa with potato fill",  140, "Starters",       True),
    ("Naan",            "Tandoor-baked bread",            40, "Main Course",    True),
]

def _seed_menu():
    docs = list(_col("menu").limit(1).stream())
    if docs:
        return
    print("Seeding menu...")
    for (name, desc, price, cat, is_veg) in SEED_MENU:
        mid = _next_id("menu")
        _doc("menu", mid).set({
            "id": mid, "name": name, "description": desc,
            "price": price, "category": cat, "is_veg": is_veg,
            "available": True, "image_url": ""
        })
    print(f"  Seeded {len(SEED_MENU)} menu items")

# ═════════════════════════════════════════════════════════════════════
#  STATIC QR GENERATION (UPI)
# ═════════════════════════════════════════════════════════════════════
def _generate_static_qr():
    """Generate a static UPI QR code (without amount) saved to static folder."""
    if not QR_AVAILABLE:
        return
    qpath = os.path.join(STATIC_DIR, "static_qr.png")
    upi_str = f"upi://pay?pa={UPI_ID}&pn={UPI_NAME}&cu=INR"
    qr = qrcode.QRCode(version=1, error_correction=qrcode.constants.ERROR_CORRECT_M,
                        box_size=10, border=4)
    qr.add_data(upi_str)
    qr.make(fit=True)
    img = qr.make_image(fill_color="black", back_color="white")
    img.save(qpath)
    print(f"✓ Static UPI QR regenerated → {qpath}")

# ═════════════════════════════════════════════════════════════════════
#  FLASK APP
# ═════════════════════════════════════════════════════════════════════
app = Flask(__name__)
app.secret_key = "autodine_v4_secret_2024"
app.config['JSONIFY_PRETTYPRINT_REGULAR'] = False
CORS(app, supports_credentials=True)

# ── Ensure /static folder exists for serving static_qr.png ───────────
STATIC_DIR = os.path.join(os.path.dirname(__file__), "static")
os.makedirs(STATIC_DIR, exist_ok=True)

# ═════════════════════════════════════════════════════════════════════
#  AUTH HELPERS
# ═════════════════════════════════════════════════════════════════════
def login_required(role="any"):
    def decorator(f):
        @wraps(f)
        def wrapped(*args, **kwargs):
            if "role" not in session:
                return redirect(url_for("login_page"))
            if role != "any" and session["role"] != role:
                return jsonify({"error": "Forbidden"}), 403
            return f(*args, **kwargs)
        return wrapped
    return decorator

def api_chef_required(f):
    @wraps(f)
    def wrapped(*args, **kwargs):
        if session.get("role") not in ("chef", "owner"):
            return jsonify({"error": "Unauthorized"}), 401
        return f(*args, **kwargs)
    return wrapped

def api_owner_required(f):
    @wraps(f)
    def wrapped(*args, **kwargs):
        if session.get("role") != "owner":
            return jsonify({"error": "Unauthorized"}), 401
        return f(*args, **kwargs)
    return wrapped

# ═════════════════════════════════════════════════════════════════════
#  PAGES
# ═════════════════════════════════════════════════════════════════════
@app.route("/")
def root():
    if "role" not in session:
        return redirect(url_for("login_page"))
    return redirect(url_for("chef_page") if session["role"] == "chef" else url_for("owner_page"))

@app.route("/login", methods=["GET"])
def login_page():
    return render_template("login.html", owner_name=OWNER_NAME)

@app.route("/login", methods=["POST"])
def do_login():
    username = request.form.get("username", "").strip()
    password = request.form.get("password", "").strip()
    if username == CHEF_USERNAME and password == CHEF_PASSWORD:
        session["role"] = "chef";  session["user"] = username
        return redirect(url_for("chef_page"))
    if username == OWNER_USERNAME and password == OWNER_PASSWORD:
        session["role"] = "owner"; session["user"] = username
        return redirect(url_for("owner_page"))
    return render_template("login.html", owner_name=OWNER_NAME, error="Invalid credentials")

@app.route("/logout")
def logout():
    session.clear()
    return redirect(url_for("login_page"))

@app.route("/chef")
@login_required("chef")
def chef_page():
    return render_template("chef.html")

@app.route("/owner")
@login_required("owner")
def owner_page():
    return render_template("owner.html", owner_name=OWNER_NAME,
                           razorpay_key=RAZORPAY_KEY_ID)

@app.route("/api/qr/static")
def api_static_qr():
    """Serve the static UPI QR code image."""
    from flask import send_from_directory
    return send_from_directory(STATIC_DIR, "static_qr.png")

# ═════════════════════════════════════════════════════════════════════
#  MENU APIs
# ═════════════════════════════════════════════════════════════════════
@app.route("/api/menu", methods=["GET"])
def api_menu():
    import traceback
    try:
        docs = _col("menu").stream()
        items = [d.to_dict() for d in docs]
        # Sort in memory: first by category, then by id
        items.sort(key=lambda x: (x.get("category", "Main Course"), x.get("id", 0)))
        return jsonify(items)
    except Exception as e:
        print("❌ CRITICAL: api_menu failed!")
        traceback.print_exc()
        return jsonify({"error": str(e)}), 500

@app.route("/api/menu/availability", methods=["GET"])
def api_menu_availability():
    docs = _col("menu").stream()
    return jsonify({str(d.id): d.to_dict().get("available", True) for d in docs})


@app.route("/api/order/item-status", methods=["POST"])
@api_chef_required
def api_item_status():
    data = request.get_json(force=True)
    _doc("menu", data["item_id"]).update({"available": bool(data.get("available", True))})
    return jsonify({"ok": True})

# ═════════════════════════════════════════════════════════════════════
#  ORDER APIs
# ═════════════════════════════════════════════════════════════════════
@app.route("/api/order", methods=["POST"])
def api_place_order():
    try:
        data  = request.get_json(force=True)
        table = data.get("table", 1)
        items = data.get("items", [])

        now = datetime.now().isoformat()
        oid = _next_id("orders")

        _doc("orders", oid).set({
            "id":        oid,
            "table_num": table,
            "status":    "pending",
            "stage":     "ordered",   # customer journey stage
            "created_at": now,
            "updated_at": now
        })

        # Write items individually — do NOT use batch when _next_id() is called
        # inside the loop (each _next_id() runs its own Firestore transaction,
        # which conflicts with an open batch)
        for item in items:
            iid = _next_id("order_items")
            _doc("order_items", iid).set({
                "id":        iid,
                "order_id":  oid,
                "item_id":   item.get("id"),
                "item_name": item.get("name", ""),
                "qty":       item.get("qty", 1),
                "price":     item.get("price", 0)
            })

        _buzz_host(1)
        app.logger.info(f"Order #{oid} placed — table {table}, {len(items)} items")
        notify_dashboard("new_order", {"order_id": oid, "table_num": table, "items_count": len(items)})
        return jsonify({"ok": True, "order_id": oid})
    except Exception as e:
        app.logger.error(f"api_place_order error: {e}")
        return jsonify({"error": str(e)}), 500

@app.route("/api/order/status", methods=["GET"])
def api_order_status():
    """BUG 7: returns removed_items list so table unit can show removal toast."""
    oid = request.args.get("order_id", type=int)
    if not oid:
        return jsonify({"error": "missing order_id"}), 400
    snap = _doc("orders", oid).get()
    if not snap.exists:
        return jsonify({"error": "not found"}), 404
    d = snap.to_dict()
    # Fetch removed_items sub-collection if it exists
    removed = [r.to_dict() for r in _col("order_removed").where("order_id", "==", oid).stream()]
    return jsonify({"order_id": oid, "status": d["status"], "removed_items": removed})

@app.route("/api/order/food-ready", methods=["POST"])
@api_chef_required
def api_food_ready():
    """Mark order status as 'ready'. Table unit polls order_status and detects 'ready'
    to show the food-ready screen automatically."""
    try:
        oid = request.get_json(force=True).get("order_id")
        if not oid:
            return jsonify({"error": "missing order_id"}), 400
        _doc("orders", oid).update({
            "status": "ready",
            "stage":  "ready",
            "updated_at": datetime.now().isoformat()
        })
        _buzz_host(2)  # 2 buzzes = food ready signal
        
        ord_snap = _doc("orders", oid).get()
        table_num = ord_snap.to_dict().get("table_num", 1) if ord_snap.exists else 1
        notify_dashboard("order_update", {"order_id": oid, "status": "ready", "table_num": table_num})
        
        app.logger.info(f"Order #{oid} marked ready")
        return jsonify({"ok": True, "status": "ready"})
    except Exception as e:
        app.logger.error(f"api_food_ready error: {e}")
        return jsonify({"error": str(e)}), 500

@app.route("/api/order/food-served", methods=["POST"])
def api_food_served():
    """BUG 2 FIX: guarantee DB update and return status in response."""
    oid = request.get_json(force=True).get("order_id")
    if not oid:
        return jsonify({"error": "missing order_id"}), 400
    try:
        now = datetime.now().isoformat()
        _doc("orders", oid).update({
            "status": "served", "stage": "bill",
            "updated_at": now
        })
        app.logger.info(f"Order #{oid} marked served at {now}")
        
        ord_snap = _doc("orders", oid).get()
        table_num = ord_snap.to_dict().get("table_num", 1) if ord_snap.exists else 1
        notify_dashboard("order_update", {"order_id": oid, "status": "served", "table_num": table_num})
        
        return jsonify({"ok": True, "status": "served"})
    except Exception as e:
        app.logger.error(f"api_food_served error: {e}")
        return jsonify({"error": str(e)}), 500

# BUG 1 FIX: Append items to existing order (Add More Items flow)
@app.route("/api/order/append", methods=["POST"])
def api_append_order():
    """Append new items to an existing order (append_mode=true on table).
    Returns the SAME order_id, not a new one."""
    try:
        data     = request.get_json(force=True)
        oid      = data.get("order_id")
        items    = data.get("items", [])
        if not oid:
            return jsonify({"error": "missing order_id"}), 400
        snap = _doc("orders", oid).get()
        if not snap.exists:
            return jsonify({"error": "order not found"}), 404
        # Append new order_items
        for item in items:
            iid = _next_id("order_items")
            _doc("order_items", iid).set({
                "id":        iid,
                "order_id":  oid,
                "item_id":   item.get("id"),
                "item_name": item.get("name", ""),
                "qty":       item.get("qty", 1),
                "price":     item.get("price", 0)
            })
        # Set status back to "preparing" so kitchen sees it again
        _doc("orders", oid).update({
            "status": "pending",
            "stage":  "ordered",
            "updated_at": datetime.now().isoformat()
        })
        _buzz_host(1)
        app.logger.info(f"Appended {len(items)} items to order #{oid}")
        return jsonify({"ok": True, "order_id": oid})
    except Exception as e:
        app.logger.error(f"api_append_order error: {e}")
        return jsonify({"error": str(e)}), 500



# ═════════════════════════════════════════════════════════════════════
#  CHEF ACTIVE ORDER — live customer journey
# ═════════════════════════════════════════════════════════════════════
@app.route("/api/order/chef-active", methods=["GET"])
@api_chef_required
def api_chef_active():
    """Return ALL active/served orders for the chef board using indexed query."""
    try:
        # Optimized: filter by status directly in Firestore
        ACTIVE_STATUSES = ["pending", "ready", "served", "billing", "timeout"]
        orders = []
        docs = _col("orders").where("status", "in", ACTIVE_STATUSES).stream()
        for d in docs:
            o = d.to_dict()
            # Attach items
            items = _col("order_items").where("order_id", "==", o["id"]).stream()
            o["items"] = [i.to_dict() for i in items]
            # Attach payment info
            psnap = get_db().collection("payments").document(f"ord_{o['id']}").get()
            o["payment"] = psnap.to_dict() if psnap.exists else None
            orders.append(o)
        
        # Sort by status priority (billing/timeout first)
        STATUS_PRIORITY = {"billing": 0, "timeout": 0, "pending": 1, "ready": 2, "served": 3}
        orders.sort(key=lambda x: (STATUS_PRIORITY.get(x["status"], 9), x["id"]))
        
        return jsonify({"orders": orders})
    except Exception as e:
        app.logger.error(f"api_chef_active error: {e}")
        return jsonify({"orders": [], "error": str(e)}), 500

# ═════════════════════════════════════════════════════════════════════
#  CHEF STATS & HISTORY — today only
# ═════════════════════════════════════════════════════════════════════
@app.route("/api/chef/orders/today", methods=["GET"])
@api_chef_required
def api_chef_orders_today():
    today  = date.today().isoformat()
    docs   = _col("orders").where("status", "==", "paid").stream()
    result = []
    for d in docs:
        o = d.to_dict()
        if not o.get("created_at", "").startswith(today):
            continue
        items = _col("order_items").where("order_id", "==", o["id"]).stream()
        o["items"] = [i.to_dict() for i in items]
        result.append(o)
    result.sort(key=lambda x: x["id"], reverse=True)
    return jsonify(result)

@app.route("/api/chef/stats/today", methods=["GET"])
@api_chef_required
def api_chef_stats_today():
    today = date.today().isoformat()
    return _stats_for_date(today)

# ═════════════════════════════════════════════════════════════════════
#  BILL
# ═════════════════════════════════════════════════════════════════════
@app.route("/api/order/bill", methods=["POST"])
def api_generate_bill():
    oid   = request.get_json(force=True).get("order_id")
    items = list(_col("order_items").where("order_id", "==", oid).stream())
    subtotal = sum(i.to_dict()["qty"] * i.to_dict()["price"] for i in items)
    gst      = subtotal * 5 // 100
    total    = subtotal + gst
    _doc("orders", oid).update({"status": "billing", "updated_at": datetime.now().isoformat()})
    
    # Pre-generate QR bit-matrix for the table
    upi_link = f"upi://pay?pa={UPI_ID}&pn=AutoDine&am={total}&tr=ord_{oid}&mc=0000"
    qr_data  = _get_qr_matrix(upi_link)
    
    _buzz_host(1)
    notify_dashboard("order_update", {"order_id": oid, "status": "billing"})
    return jsonify({
        "order_id": oid,
        "items":    [i.to_dict() for i in items],
        "subtotal": subtotal,
        "gst":      gst,
        "total":    total,
        "qr_matrix": qr_data["matrix"],
        "qr_size":   qr_data["size"]
    })

# ═════════════════════════════════════════════════════════════════════
#  PAYMENT APIs
# ═════════════════════════════════════════════════════════════════════
@app.route("/api/payment/method", methods=["POST"])
def api_payment_method():
    data   = request.get_json(force=True)
    oid    = data.get("order_id")
    method = data.get("method", "cash")

    items    = list(_col("order_items").where("order_id", "==", oid).stream())
    subtotal = sum(i.to_dict()["qty"] * i.to_dict()["price"] for i in items)
    total    = subtotal + subtotal * 5 // 100

    # Upsert payment document (use order_id as document id for easy lookup)
    pref = get_db().collection("payments").document(f"ord_{oid}")
    if pref.get().exists:
        pref.update({"method": method, "amount": total})
    else:
        pref.set({
            "order_id": oid, "method": method, "amount": total,
            "status": "pending", "razorpay_order": "",
            "razorpay_pid": "", "created_at": datetime.now().isoformat()
        })

    _buzz_host(2)
    notify_dashboard("payment_initiated", {"order_id": oid, "method": method})
    return jsonify({"ok": True, "total": total})

@app.route("/api/payment/status", methods=["GET"])
def api_payment_status():
    """Live status check for table unit + Razorpay bridge."""
    try:
        oid = request.args.get("order_id", type=int)
        if not oid:
            return jsonify({"status": "pending", "method": None})
        snap = get_db().collection("payments").document(f"ord_{oid}").get()
        if not snap.exists:
            return jsonify({"status": "pending", "method": None})
        d = snap.to_dict()

        # Dynamic Razorpay check for real links
        plink_id = d.get("razorpay_link_id", "")
        if (plink_id and not plink_id.startswith("plink_demo")
                and RAZORPAY_AVAILABLE and RAZORPAY_KEY_ID != "YOUR_KEY_ID"):
            try:
                import requests as _req, base64 as _b64
                creds = _b64.b64encode(f"{RAZORPAY_KEY_ID}:{RAZORPAY_KEY_SECRET}".encode()).decode()
                r     = _req.get(f"https://api.razorpay.com/v1/payment_links/{plink_id}",
                                 headers={"Authorization": f"Basic {creds}"}, timeout=5)
                rd    = r.json()
                # Statuses: created, partial, paid, cancelled, expired
                if rd.get("status") == "paid":
                    get_db().collection("payments").document(f"ord_{oid}").update({"status": "paid"})
                    _doc("orders", oid).update({"status": "paid", "payment_method": "upi",
                                                 "updated_at": datetime.now().isoformat()})
                    return jsonify({"order_id": oid, "status": "paid", "method": "upi"})
            except Exception as exc:
                app.logger.warning(f"Razorpay status check failed: {exc}")

        return jsonify({"order_id": oid,
                        "status": d.get("status", "pending"),
                        "method": d.get("method", "cash")})
    except Exception as e:
        app.logger.error(f"api_payment_status error: {e}")
        return jsonify({"status": "pending", "error": str(e)}), 500

@app.route("/api/payment/static-qr", methods=["GET"])
@api_chef_required
def api_payment_static_qr():
    """Returns the restaurant's static UPI QR image. Serves the pre-generated PNG."""
    # Build a data-URI or serve as /static/static_qr.png path
    qr_url = "/static/static_qr.png"
    return jsonify({
        "qr":     qr_url,
        "upi_id": UPI_ID,
    })

@app.route("/api/payment/verify-manual", methods=["POST"])
@api_chef_required
def api_verify_manual():
    oid  = request.get_json(force=True).get("order_id")
    now  = datetime.now().isoformat()
    # BUG FIX: set payment_status="paid" (matches firmware check)
    get_db().collection("payments").document(f"ord_{oid}").update(
        {"status": "paid"})
    _doc("orders", oid).update({"status": "paid", "payment_method": "manual",
                                 "updated_at": now})
    _buzz_host(3)
    return jsonify({"ok": True})

@app.route("/api/payment/verify-upi", methods=["POST"])
@api_chef_required
def api_verify_upi():
    """Alias for manual verification specifically for UPI flows on chef dashboard."""
    return api_verify_manual()

# ═════════════════════════════════════════════════════════════════════
#  RAZORPAY — Payment Link Integration (Real Money)
# ═════════════════════════════════════════════════════════════════════


# ═════════════════════════════════════════════════════════════════════
#  BUZZ
# ═════════════════════════════════════════════════════════════════════
def _buzz_host(pattern: int):
    def _do():
        try:
            body = json.dumps({"pattern": pattern}).encode()
            req  = urllib.request.Request(
                f"http://{HOST_UNIT_IP}/buzz", data=body,
                headers={"Content-Type": "application/json"}, method="POST")
            urllib.request.urlopen(req, timeout=2)
        except Exception:
            pass
    threading.Thread(target=_do, daemon=True).start()

@app.route("/api/buzz", methods=["POST"])
def api_buzz():
    _buzz_host(request.get_json(force=True).get("pattern", 1))
    return jsonify({"ok": True})

# ═════════════════════════════════════════════════════════════════════
#  FEEDBACK
# ═════════════════════════════════════════════════════════════════════
@app.route("/api/feedback", methods=["POST"])
def api_feedback():
    data = request.get_json(force=True)
    fid  = _next_id("feedback")
    _doc("feedback", fid).set({
        "id": fid,
        "order_id": data.get("order_id"),
        "stars": data.get("stars", 5),
        "comment": data.get("comment", ""),
        "created_at": datetime.now().isoformat()
    })
    return jsonify({"ok": True})

# ═════════════════════════════════════════════════════════════════════
#  OWNER STATS
# ═════════════════════════════════════════════════════════════════════
@app.route("/api/owner/stats/today", methods=["GET"])
@api_owner_required
def api_stats_today():
    return _stats_for_date(date.today().isoformat())

@app.route("/api/owner/stats/date/<day>", methods=["GET"])
@api_owner_required
def api_stats_date(day):
    return _stats_for_date(day)

def _stats_for_date(day):
    # day is "YYYY-MM-DD"
    # ISO strings: "2024-03-28" <= X < "2024-03-29"
    next_day = day[:-2] + str(int(day[-2:]) + 1).zfill(2) # simple day inc
    # A bit risky at month end, better:
    from datetime import datetime, timedelta
    d_obj = datetime.strptime(day, "%Y-%m-%d")
    next_day = (d_obj + timedelta(days=1)).strftime("%Y-%m-%d")

    try:
        docs = _col("orders").where("status", "==", "paid") \
               .where("created_at", ">=", day).where("created_at", "<", next_day).stream()
        paid_orders = [d.to_dict() for d in docs]
    except Exception as e:
        # Fallback to in-memory filtering if index is missing/building
        print(f"⚠️ Index check failed, falling back: {e}")
        all_day_orders = _col("orders").where("created_at", ">=", day).where("created_at", "<", next_day).stream()
        paid_orders = [d.to_dict() for d in all_day_orders if d.to_dict().get("status") == "paid"]

    ids    = [o["id"] for o in paid_orders]
    count  = len(ids)
    revenue = 0
    if ids:
        for pid in ids:
            snap = get_db().collection("payments").document(f"ord_{pid}").get()
            if snap.exists:
                p = snap.to_dict()
                if p.get("status") == "paid":
                    revenue += p.get("amount", 0)

    # avg rating for the day
    fbs = [
        d.to_dict() for d in _col("feedback").stream()
        if d.to_dict().get("created_at", "").startswith(day)
    ]
    avg_rating = round(sum(f.get("stars", 0) for f in fbs) / len(fbs), 1) if fbs else 0.0

    avg_order = round(revenue / count, 2) if count else 0.0
    print(f"[stats] date={day} orders={count} revenue={revenue} avg_order={avg_order} avg_rating={avg_rating}")
    return {
        "date": day,
        "orders": count,
        "revenue": revenue,
        "avg_order": avg_order,
        "avg_rating": avg_rating
    }

@app.route("/api/owner/stats/today", methods=["GET"])
@api_owner_required
def api_owner_stats_today():
    today = date.today().isoformat()
    return jsonify(_stats_for_date(today))

@app.route("/api/owner/orders/<day>", methods=["GET"])
@api_owner_required
def api_owner_orders(day):
    from datetime import datetime, timedelta
    d_obj = datetime.strptime(day, "%Y-%m-%d")
    next_day = (d_obj + timedelta(days=1)).strftime("%Y-%m-%d")

    docs = _col("orders").where("created_at", ">=", day).where("created_at", "<", next_day).stream()
    result = []
    for d in docs:
        o = d.to_dict()
        items = list(_col("order_items").where("order_id", "==", o["id"]).stream())
        o["items"] = [i.to_dict() for i in items]
        
        subtotal = sum(i.get("qty", 1) * i.get("price", 0) for i in o["items"])
        o["grand_total"] = subtotal + (subtotal * 5 // 100)
        
        fbs = list(_col("feedback").where("order_id", "==", o["id"]).stream())
        o["feedback"] = fbs[0].to_dict() if fbs else None
        result.append(o)
    result.sort(key=lambda x: x["id"], reverse=True)
    return jsonify(result)

@app.route("/api/owner/analytics/revenue", methods=["GET"])
@api_owner_required
def api_analytics_revenue():
    from datetime import timedelta
    startDate = (date.today() - timedelta(days=6)).isoformat()
    rev7 = {}
    for i in range(6, -1, -1):
        d = (date.today() - timedelta(days=i)).isoformat()
        rev7[d] = 0
    
    try:
        docs = _col("orders").where("status", "==", "paid") \
               .where("created_at", ">=", startDate).stream()
        all_paid_docs = list(docs)
    except Exception as e:
        print(f"⚠️ Index check failed (revenue), falling back: {e}")
        all_recent = _col("orders").where("created_at", ">=", startDate).stream()
        all_paid_docs = [d for d in all_recent if d.to_dict().get("status") == "paid"]

    for d in all_paid_docs:
        o = d.to_dict()
        day = o.get("created_at", "")[:10]
        if day in rev7:
            # Amount is also in the order document now (via api_create_razorpay_order update)
            # but to be safe we use payments
            snap = get_db().collection("payments").document(f"ord_{o['id']}").get()
            if snap.exists and snap.to_dict().get("status") == "paid":
                rev7[day] += snap.to_dict().get("amount", 0)
                
    labels = list(rev7.keys())
    values = [rev7[k] for k in labels]
    return jsonify({"labels": labels, "values": values})

@app.route("/api/owner/analytics/feedback", methods=["GET"])
@api_owner_required
def api_analytics_feedback():
    from datetime import timedelta
    startDate = (date.today() - timedelta(days=6)).isoformat()
    fb7 = {}
    fb_counts = {}
    for i in range(6, -1, -1):
        d = (date.today() - timedelta(days=i)).isoformat()
        fb7[d] = 0
        fb_counts[d] = 0
        
    docs = _col("feedback").where("created_at", ">=", startDate).stream()
    for d in docs:
        f = d.to_dict()
        day = f.get("created_at", "")[:10]
        if day in fb7:
            fb7[day] += f.get("stars", 0)
            fb_counts[day] += 1
            
    labels = list(fb7.keys())
    values = [round(fb7[k]/fb_counts[k], 1) if fb_counts[k] > 0 else 0 for k in labels]
    return jsonify({"labels": labels, "values": values})

@app.route("/api/owner/stats/alltime", methods=["GET"])
@api_owner_required
def api_stats_alltime():
    try:
        # Use aggregation for count if available in SDK, else stream
        # Fallback to stream but keep it efficient
        orders = list(_col("orders").where("status", "==", "paid").stream())
        count = len(orders)
        rev = 0
        for o in orders:
            snap = get_db().collection("payments").document(f"ord_{o.to_dict()['id']}").get()
            if snap.exists and snap.to_dict().get("status") == "paid":
                rev += snap.to_dict().get("amount", 0)
                
        feedbacks = list(_col("feedback").stream())
        fcount = len(feedbacks)
        avg = sum(f.to_dict().get("stars",0) for f in feedbacks) / fcount if fcount > 0 else 0
        return jsonify({
            "orders": count,
            "revenue": rev,
            "avg_rating": round(avg, 1),
            "feedbacks": fcount
        })
    except Exception as e:
        return jsonify({"error": str(e)}), 500

# ═════════════════════════════════════════════════════════════════════
#  STAFF
# ═════════════════════════════════════════════════════════════════════
@app.route("/api/owner/staff", methods=["GET"])
@api_owner_required
def api_get_staff():
    return jsonify([d.to_dict() for d in _col("staff").stream()])


@app.route("/api/owner/staff/<int:sid>", methods=["PUT"])
@api_owner_required
def api_edit_staff(sid):
    d = request.get_json(force=True)
    _doc("staff", sid).update({
        "name": d["name"],
        "role": d.get("role", "Waiter"),
        "salary": d.get("salary", 0)
    })
    return jsonify({"ok": True})

@app.route("/api/owner/staff/<int:sid>", methods=["DELETE"])
@api_owner_required
def api_del_staff(sid):
    _doc("staff", sid).delete()
    return jsonify({"ok": True})

@app.route("/api/owner/salary/<int:sid>", methods=["POST"])
@api_owner_required
def api_salary(sid):
    d     = request.get_json(force=True)
    month = d.get("month", date.today().strftime("%Y-%m"))
    ref   = get_db().collection("salary_records").document(f"{sid}_{month}")
    now   = datetime.now().isoformat()
    if ref.get().exists:
        ref.update({"paid": True, "paid_at": now})
    else:
        ref.set({"staff_id": sid, "month": month, "paid": True, "paid_at": now})
    return jsonify({"ok": True})

@app.route("/api/owner/salary-records/<int:sid>", methods=["GET"])
@api_owner_required
def api_salary_records(sid):
    docs = _col("salary_records").where("staff_id", "==", sid).stream()
    rows = sorted([d.to_dict() for d in docs], key=lambda x: x["month"], reverse=True)
    return jsonify(rows)

# ═════════════════════════════════════════════════════════════════════
#  MENU MGMT (Owner)
# ═════════════════════════════════════════════════════════════════════
@app.route("/api/owner/menu", methods=["POST"])
@api_owner_required
def api_owner_add_menu():
    d = request.get_json(force=True)
    iid = _next_id("menu")
    _doc("menu", iid).set({
        "id": iid, "name": d["name"], "price": d["price"],
        "description": d.get("description", ""),
        "category": d.get("category", "Uncategorized"),
        "is_veg": bool(d.get("is_veg", True)),
        "available": True,
        "image": d.get("image", "")
    })
    return jsonify({"ok": True})

@app.route("/api/owner/menu/<int:iid>", methods=["PUT"])
@api_owner_required
def api_owner_update_menu(iid):
    d = request.get_json(force=True)
    _doc("menu", iid).update({
        "name": d["name"], "price": d["price"],
        "description": d.get("description", ""),
        "category": d.get("category", "Uncategorized"),
        "is_veg": bool(d.get("is_veg", True))
    })
    return jsonify({"ok": True})

@app.route("/api/owner/menu/<int:iid>", methods=["DELETE"])
@api_owner_required
def api_owner_delete_menu(iid):
    _doc("menu", iid).delete()
    return jsonify({"ok": True})

# ═════════════════════════════════════════════════════════════════════
#  MAIN
# ═════════════════════════════════════════════════════════════════════
# ═════════════════════════════════════════════════════════════════════
#  OWNER FEEDBACKS  (BUG 4)
# ═════════════════════════════════════════════════════════════════════
@app.route("/api/owner/feedbacks", methods=["GET"])
@api_owner_required
def api_owner_feedbacks():
    """Return last 20 feedbacks with order/table info."""
    fbs = sorted(
        [d.to_dict() for d in _col("feedback").stream()],
        key=lambda x: x.get("id", 0), reverse=True
    )[:20]
    # Attach table_num from the parent order
    for fb in fbs:
        oid = fb.get("order_id")
        if oid:
            osnap = _doc("orders", oid).get()
            fb["table_num"] = osnap.to_dict().get("table_num", "?") if osnap.exists else "?"
        else:
            fb["table_num"] = "?"
    return jsonify(fbs)

@app.route("/api/owner/feedbacks/export", methods=["GET"])
@api_owner_required
def api_owner_feedbacks_export():
    import csv, io
    fbs = sorted([d.to_dict() for d in _col("feedback").stream()],
                 key=lambda x: x.get("id", 0), reverse=True)
    out = io.StringIO()
    w = csv.writer(out)
    w.writerow(["ID", "Order", "Table", "Stars", "Comment", "Date"])
    for fb in fbs:
        oid = fb.get("order_id", "")
        osnap = _doc("orders", oid).get() if oid else None
        tbl = osnap.to_dict().get("table_num", "?") if (osnap and osnap.exists) else "?"
        w.writerow([fb.get("id",""), oid, tbl, fb.get("stars",""),
                    fb.get("comment",""), fb.get("created_at","")[:10]])
    from flask import Response
    return Response(out.getvalue(), mimetype="text/csv",
                    headers={"Content-Disposition": "attachment;filename=autodine_feedbacks.csv"})

# ═════════════════════════════════════════════════════════════════════
#  REAL-TIME DASHBOARD (SSE)
# ═════════════════════════════════════════════════════════════════════
@app.route("/api/chef/stream")
def api_chef_stream():
    """Server-Sent Events stream for the Chef Dashboard."""
    q = queue.Queue(maxsize=100)
    dashboard_queues.append(q)
    def event_stream():
        try:
            # Initial verification ping
            yield f"data: {json.dumps({'type':'ping','time':time.time()})}\n\n"
            while True:
                msg = q.get()
                yield f"data: {msg}\n\n"
        except GeneratorExit:
            pass
        finally:
            if q in dashboard_queues: dashboard_queues.remove(q)
    return Response(stream_with_context(event_stream()), mimetype="text/event-stream")
@app.route("/api/razorpay/create-order", methods=["POST"])
def api_razorpay_create_order():
    """Consolidated: Calculates order total and creates a Razorpay Payment Link.
    Ensures browser opens for test mode auto-verification."""
    try:
        data = request.get_json(force=True)
        oid = data.get("order_id")
        if not oid: return jsonify({"error": "Missing order_id"}), 400

        # Calculate exact total from DB order_items
        items_ref = _col("order_items").where("order_id", "==", oid).stream()
        subtotal = 0
        for it in items_ref:
            d = it.to_dict()
            subtotal += d.get("price", 0) * d.get("qty", 1)
        
        if subtotal <= 0: return jsonify({"error": "Empty order"}), 400
        total_paise = int(subtotal * 1.05 * 100) # 5% GST
        
        ord_snap = _doc("orders", oid).get()
        table_num = ord_snap.to_dict().get("table_num", 1) if ord_snap.exists else 1

        # Default fallback values
        plink_id = f"fallback_{int(time.time())}"
        qr_url = f"https://rzp.io/i/demo_pay_{oid}" # Local demo placeholder

        # ── RAZORPAY API CALL ──
        if RAZORPAY_AVAILABLE and RAZORPAY_KEY_ID not in ("YOUR_KEY_ID", ""):
            try:
                # Automatic cleanup of keys (strips quotes/spaces)
                kid = RAZORPAY_KEY_ID.strip().strip('"').strip("'")
                ksec = RAZORPAY_KEY_SECRET.strip().strip('"').strip("'")

                print(f"--- Razorpay Attempt (Key: {kid[:8]}...) ---")
                client = razorpay.Client(auth=(kid, ksec))
                pl_data = {
                    "amount": total_paise,
                    "currency": "INR",
                    "accept_partial": False,
                    "description": f"AutoDine Order #{oid} (Table {table_num})",
                    "customer": { "name": f"Table {table_num}", "contact": "+919000000001", "email": "customer@autodine.com" },
                    "notify": { "sms": False, "email": False },
                    "reminder_enable": False,
                    "notes": { "order_id": str(oid) }
                }
                pl = client.payment_link.create(pl_data)
                plink_id = pl.get("id")
                qr_url = pl.get("short_url")
                print(f"✅ Success! URL: {qr_url}")
            except Exception as e:
                print(f"❌ RAZORPAY ERROR: {e}")
                qr_url = "https://bit.ly/AD-Payment-Error"

        # Update payments record in DB
        _doc("payments", f"ord_{oid}").set({
            "order_id": oid,
            "amount_paise": total_paise,
            "status": "pending",
            "method": "upi",
            "razorpay_link_id": plink_id,
            "created_at": datetime.now().isoformat()
        }, merge=True)
        
        resp_data = {
            "ok": True, 
            "qr_url": qr_url, 
            "amount_paise": total_paise, 
            "plink_id": plink_id
        }
        print(f"📡 RESPONSE TO ESP32: {resp_data}")
        notify_dashboard("payment_initiated", {"order_id": oid, "method": "upi", "table_num": table_num})
        return jsonify(resp_data)

    except Exception as e:
        app.logger.error(f"api_razorpay_create_order fatal error: {e}")
        return jsonify({"error": str(e)}), 500

# ═════════════════════════════════════════════════════════════════════
#  RAZORPAY STATUS POLL (called by ESP32 every 3s during UPI payment)
# ═════════════════════════════════════════════════════════════════════
@app.route("/api/razorpay/status/<int:oid>", methods=["GET"])
def api_razorpay_status(oid):
    """ESP32 polls this to check if Razorpay payment link has been paid.
    Uses razorpay SDK payment_link.fetch() if available, otherwise DB."""
    try:
        snap = get_db().collection("payments").document(f"ord_{oid}").get()
        if not snap.exists:
            return jsonify({"status": "pending"})
        d = snap.to_dict()

        # Already paid in DB?
        if d.get("status") == "paid":
            return jsonify({"status": "paid"})

        # Try live Razorpay check
        plink_id = d.get("razorpay_link_id", "")
        if (plink_id
                and not plink_id.startswith("fallback_")
                and RAZORPAY_AVAILABLE
                and RAZORPAY_KEY_ID != "YOUR_KEY_ID"):
            try:
                client = razorpay.Client(auth=(RAZORPAY_KEY_ID, RAZORPAY_KEY_SECRET))
                pl = client.payment_link.fetch(plink_id)
                rz_status = pl.get("status", "created")  # created | paid | cancelled | expired
                if rz_status == "paid":
                    # Fetch table num for notification
                    ord_snap = _doc("orders", oid).get()
                    table_num = ord_snap.to_dict().get("table_num", 1) if ord_snap.exists else 1

                    # Auto-confirm in DB
                    now = datetime.now().isoformat()
                    get_db().collection("payments").document(f"ord_{oid}").update({"status": "paid"})
                    _doc("orders", oid).update({
                        "status": "paid", "payment_method": "upi", "updated_at": now
                    })
                    _buzz_host(3)
                    app.logger.info(f"Razorpay auto-confirm: Order #{oid} paid")
                    notify_dashboard("payment_update", {"order_id": oid, "status": "paid", "table_num": table_num})
                    return jsonify({"status": "paid"})
                elif rz_status in ("cancelled", "expired"):
                    return jsonify({"status": "failed"})
                else:
                    return jsonify({"status": "pending"})
            except Exception as exc:
                app.logger.warning(f"Razorpay fetch error: {exc}")

        # DB fallback
        return jsonify({"status": d.get("status", "pending")})
    except Exception as e:
        app.logger.error(f"api_razorpay_status error: {e}")
        return jsonify({"status": "error", "detail": str(e)}), 500

@app.route("/api/payment/status/<int:oid>", methods=["GET"])
def api_payment_status_by_id(oid):
    """Check Razorpay Payment Link status. Falls back to DB status."""
    snap = get_db().collection("payments").document(f"ord_{oid}").get()
    if not snap.exists:
        return jsonify({"status": "pending", "method": None})
    d = snap.to_dict()
    # If we have a real plink_id and keys, check Razorpay live
    plink_id = d.get("razorpay_link_id", "")
    if (plink_id and not plink_id.startswith("plink_demo")
            and RAZORPAY_AVAILABLE and RAZORPAY_KEY_ID != "YOUR_KEY_ID"):
        try:
            import requests as _req, base64 as _b64
            creds = _b64.b64encode(f"{RAZORPAY_KEY_ID}:{RAZORPAY_KEY_SECRET}".encode()).decode()
            r     = _req.get(f"https://api.razorpay.com/v1/payment_links/{plink_id}",
                             headers={"Authorization": f"Basic {creds}"}, timeout=5)
            rd    = r.json()
            payments = rd.get("payments") or []
            if payments and payments[0].get("payment", {}).get("status") in ("captured", "authorized"):
                # Auto-confirm in DB
                get_db().collection("payments").document(f"ord_{oid}").update({"status": "paid"})
                _doc("orders", oid).update({"status": "paid", "payment_method": "upi",
                                             "updated_at": datetime.now().isoformat()})
                return jsonify({"status": "paid", "method": "upi"})
        except Exception as exc:
            app.logger.warning(f"Razorpay status check error: {exc}")
    # DB fallback
    return jsonify({"status": d.get("status", "pending"), "method": d.get("method")})


@app.route("/api/payment/confirm", methods=["POST"])
def api_payment_confirm():
    """Table unit calls this on successful QR payment detection."""
    oid = request.get_json(force=True).get("order_id")
    now = datetime.now().isoformat()
    get_db().collection("payments").document(f"ord_{oid}").update(
        {"status": "paid", "method": "upi"})
    _doc("orders", oid).update({"status": "paid", "payment_method": "upi",
                                  "updated_at": now})
    _buzz_host(3)
    return jsonify({"ok": True})


@app.route("/api/payment/timeout", methods=["POST"])
def api_payment_timeout():
    """Called when 5-minute UPI QR countdown expires without payment."""
    oid = request.get_json(force=True).get("order_id")
    _doc("orders", oid).update({"payment_status": "timeout",
                                  "updated_at": datetime.now().isoformat()})
    get_db().collection("payments").document(f"ord_{oid}").update(
        {"status": "timeout"})
    return jsonify({"ok": True})


@app.route("/api/chef/verify_payment", methods=["POST"])
@api_chef_required
def api_chef_verify_payment():
    """Chef manually confirms payment after timeout."""
    oid = request.get_json(force=True).get("order_id")
    now = datetime.now().isoformat()
    get_db().collection("payments").document(f"ord_{oid}").update({"status": "paid"})
    _doc("orders", oid).update({"status": "paid", "payment_method": "manual", "updated_at": now})
    
    ord_snap = _doc("orders", oid).get()
    table_num = ord_snap.to_dict().get("table_num", 1) if ord_snap.exists else 1
    _buzz_host(3)
    notify_dashboard("payment_update", {"order_id": oid, "status": "paid", "table_num": table_num})
    return jsonify({"ok": True})

# BUG 7: Store removed items so table unit can display toast
@app.route("/api/order/remove-item", methods=["POST"])
@api_chef_required
def api_remove_order_item():
    """Chef removes item — persists to order_removed for table unit polling."""
    try:
        data    = request.get_json(force=True)
        oid     = data.get("order_id")
        item_id = data.get("item_id")
        if not oid or not item_id:
            return jsonify({"error": "missing order_id or item_id"}), 400
        # Get item name before deleting
        item_name = ""
        # Composite query requires index
        try:
            docs = (_col("order_items")
                    .where("order_id", "==", oid)
                    .where("item_id",  "==", item_id)
                    .stream())
            all_matching = list(docs)
        except Exception as e:
            print(f"⚠️ Index check failed (remove), falling back: {e}")
            docs = _col("order_items").where("order_id", "==", oid).stream()
            all_matching = [d for d in docs if d.to_dict().get("item_id") == item_id]
        
        removed = 0
        for d in all_matching:
            item_name = d.to_dict().get("item_name", "")
            d.reference.delete()
            removed += 1
        # Mark menu item unavailable
        _doc("menu", item_id).update({"available": False})
        # Write to order_removed so table unit picks it up
        rid = _next_id("order_removed")
        _doc("order_removed", rid).set({
            "id": rid, "order_id": oid, "item_id": item_id,
            "item_name": item_name, "created_at": datetime.now().isoformat()
        })
        app.logger.info(f"Removed item {item_id} ('{item_name}') from order #{oid}")
        return jsonify({"ok": True, "removed": removed, "item_name": item_name})
    except Exception as e:
        app.logger.error(f"api_remove_order_item error: {e}")
        return jsonify({"error": str(e)}), 500


# ═════════════════════════════════════════════════════════════════════
#  RAZORPAY WEBHOOK  — instant auto-confirm on payment.captured
#  Set RAZORPAY_WEBHOOK_SECRET in server/.env  (from Razorpay Dashboard)
# ═════════════════════════════════════════════════════════════════════
RAZORPAY_WEBHOOK_SECRET = os.environ.get("RAZORPAY_WEBHOOK_SECRET", "")

@app.route("/api/razorpay/webhook", methods=["POST"])
def api_razorpay_webhook():
    """Razorpay calls this URL instantly when a payment is captured."""
    import hmac, hashlib

    raw_body = request.get_data()          # raw bytes for HMAC
    sig_header = request.headers.get("X-Razorpay-Signature", "")

    # ── 1. Verify signature (skip only if secret not configured) ──
    if RAZORPAY_WEBHOOK_SECRET:
        expected = hmac.new(
            RAZORPAY_WEBHOOK_SECRET.encode("utf-8"),
            raw_body,
            hashlib.sha256
        ).hexdigest()
        if not hmac.compare_digest(expected, sig_header):
            app.logger.warning("Razorpay webhook: invalid signature — ignoring.")
            return jsonify({"error": "invalid signature"}), 400

    try:
        payload = request.get_json(force=True, silent=True) or {}
        event   = payload.get("event", "")

        # We only care about successful payments
        if event not in ("payment.captured", "payment_link.paid"):
            return jsonify({"ok": True, "event": event, "action": "ignored"})

        # ── 2. Extract order_id from notes ──
        entity = (payload.get("payload", {})
                         .get("payment_link", {})
                         .get("entity", {}))
        if not entity:
            entity = (payload.get("payload", {})
                             .get("payment", {})
                             .get("entity", {}))

        notes     = entity.get("notes", {})
        order_id  = notes.get("order_id")          # we stored this when creating the link
        plink_id  = entity.get("id", "") or entity.get("payment_link_id", "")
        amount    = entity.get("amount", 0)         # paise

        if not order_id:
            app.logger.warning("Razorpay webhook: no order_id in notes, cannot map.")
            return jsonify({"ok": True, "action": "no_order_id"})

        oid = int(order_id)
        now = datetime.now().isoformat()

        # ── 3. Mark paid in DB ──
        get_db().collection("payments").document(f"ord_{oid}").set({
            "order_id": oid,
            "amount_paise": amount,
            "status": "paid",
            "method": "upi",
            "razorpay_link_id": plink_id,
            "paid_at": now,
            "created_at": now
        }, merge=True)

        _doc("orders", oid).update({
            "status": "paid",
            "payment_method": "upi",
            "updated_at": now
        })

        # ── 4. Notify chef dashboard + buzz ──
        ord_snap  = _doc("orders", oid).get()
        table_num = ord_snap.to_dict().get("table_num", "?") if ord_snap.exists else "?"
        _buzz_host(3)
        notify_dashboard("payment_update", {
            "order_id": oid, "status": "paid",
            "table_num": table_num, "source": "webhook"
        })

        app.logger.info(f"Webhook: Order #{oid} (Table {table_num}) auto-confirmed via Razorpay")
        return jsonify({"ok": True, "order_id": oid, "status": "paid"})

    except Exception as e:
        app.logger.error(f"Razorpay webhook error: {e}")
        # Always return 200 to Razorpay so it doesn't keep retrying
        return jsonify({"ok": True, "warning": str(e)})


if __name__ == "__main__":
    _init_firebase()
    
    # Startup tasks — gracefully handle quota errors
    import time
    for attempt in range(3):
        try:
            _seed_menu()
            break
        except Exception as e:
            print(f"⚠ Seed menu attempt {attempt+1} failed: {e}")
            if attempt < 2:
                print("  Retrying in 5 seconds...")
                time.sleep(5)
            else:
                print("  Skipping menu seed (menu may already exist).")

    try:
        _generate_static_qr()
    except Exception as e:
        print(f"⚠ Static QR generation failed: {e}")

    print("=" * 60)
    print("  AutoDine V4.0 Server (Firebase Edition)")
    print("  Dashboard:  http://localhost:5000")
    print(f"  Chef:       {CHEF_USERNAME} / {CHEF_PASSWORD}")
    print(f"  Owner:      {OWNER_USERNAME} / {OWNER_PASSWORD}")
    print(f"  UPI ID:     {UPI_ID}")
    print("=" * 60)
    app.run(host="0.0.0.0", port=5050, debug=True)
