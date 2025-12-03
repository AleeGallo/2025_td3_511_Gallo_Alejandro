from flask import Flask, render_template, jsonify, request
import time

from core.serial_backend import send_command, is_device_available, SIMULATE

app = Flask(__name__, static_folder="static", template_folder="templates")

@app.route("/")
def index():
    return render_template("index.html")

# ===========================
# SET
# ===========================
@app.route("/api/set", methods=["POST"])
def api_set():
    data = request.json or {}
    param = data.get("param")
    value = data.get("value")

    if not param or value is None:
        return jsonify({"ok": False, "err": "missing param/value"}), 400

    cmd = f"SET {param} {value}"
    resp = send_command(cmd)

    return jsonify({"ok": True, "tx": cmd, "rx": resp, "simulate": SIMULATE})

# ===========================
# GET
# ===========================
@app.route("/api/get", methods=["GET"])
def api_get():
    cmd = request.args.get("cmd")
    if not cmd:
        return jsonify({"ok": False, "err": "missing cmd"}), 400

    resp = send_command(cmd)
    return jsonify({"ok": True, "tx": cmd, "rx": resp, "simulate": SIMULATE})

# ===========================
# MANUAL
# ===========================
@app.route("/api/manual", methods=["POST"])
def api_manual():
    data = request.json or {}
    mode = data.get("mode", "").upper()
    param = data.get("param", "").upper()
    value = data.get("value")

    if not mode or not param:
        return jsonify({"ok": False, "err": "missing mode/param"}), 400

    if mode == "SET":
        if value is None:
            return jsonify({"ok": False, "err": "missing value for SET"}), 400
        cmd = f"SET {param} {value}"
    else:
        cmd = f"GET {param}"

    resp = send_command(cmd)
    return jsonify({"ok": True, "tx": cmd, "rx": resp, "simulate": SIMULATE})

# ===========================
# GET I (para el gráfico)
# ===========================
# ===========================
# GET I (para el gráfico)
# ===========================
@app.route('/api/get_i', methods=['GET'])
def api_get_i():
    resp = send_command("GET I", timeout=1.0)
    text = resp.strip() if resp else ''
    
    # Texto original recibido
    clean = text

    # Limpieza básica
    clean = clean.replace("OK", "").replace("ok", "")
    clean = clean.replace("mA", "").replace("A", "").strip()

    val = None

    # Caso 1 → I=72.589
    if "=" in clean:
        try:
            val = float(clean.split("=")[1])
        except:
            pass

    # Caso 2 → I 72.589
    if val is None and " " in clean:
        try:
            val = float(clean.split()[1])
        except:
            pass

    # Caso 3 → solo número
    if val is None:
        try:
            val = float(clean)
        except:
            pass

    return jsonify({
        "ok": True,
        "raw": text,
        "value": val,
        "simulate": SIMULATE
    })

# ===========================
# LOG
# ===========================
@app.route("/api/log", methods=["GET"])
def api_log():
    resp = send_command("GET LOG", timeout=2.0)
    return jsonify({"ok": True, "raw": resp, "simulate": SIMULATE})

# ===========================
# RUN
# ===========================
if __name__ == "__main__":
    app.run(host="0.0.0.0", port=5000, debug=False)
