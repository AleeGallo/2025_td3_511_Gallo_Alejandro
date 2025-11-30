#!/usr/bin/env python3
import os
import time
import select
import threading
from flask import Flask, render_template, request

app = Flask(__name__)

DEV_PATH = "/dev/egb"

# --- LOG BUFFER ---
logs = []
logs_lock = threading.Lock()
MAX_LOGS = 5000

def add_log(text: str):
    ts = time.strftime("%H:%M:%S")
    line = f"{ts} {text}"
    with logs_lock:
        logs.append(line)
        if len(logs) > MAX_LOGS:
            logs.pop(0)


# --- HILO LECTOR UART ---
def lector_uart():
    """Hilo que mantiene /dev/egb abierto en modo lectura."""
    while True:
        try:
            f = os.open(DEV_PATH, os.O_RDONLY)
            add_log("[lector] /dev/egb abierto para lectura")

            while True:
                r, _, _ = select.select([f], [], [], 0.5)
                if r:
                    data = os.read(f, 256)
                    if not data:
                        break
                    text = data.decode("utf-8", errors="ignore").strip()
                    if text:
                        add_log(f"[RX] {text}")

        except Exception as e:
            add_log(f"[ERROR lector] {repr(e)}")
            time.sleep(1)
        finally:
            try:
                os.close(f)
            except:
                pass


thr = threading.Thread(target=lector_uart, daemon=True)
thr.start()


# --- ENVÍO DE COMANDOS ---
def send_and_wait(cmd: str, expect: str | None = None, timeout: float = 5.0) -> str:
    """Envia comando a /dev/egb. No espera respuesta especial (los logs lo muestran)."""
    try:
        with open(DEV_PATH, "w") as f:
            f.write(cmd)
            f.flush()
        add_log(f"[TX] {cmd.strip()}")
        return "Comando enviado."
    except Exception as e:
        msg = f"ERROR escribiendo en {DEV_PATH}: {repr(e)}"
        add_log(f"[TX ERROR] {msg}")
        return msg


# --- ENDPOINTS ---
@app.route("/")
def index():
    return render_template("index.html", status="")

@app.route("/logs")
def get_logs():
    with logs_lock:
        return "\n".join(logs)

@app.route("/clear_logs", methods=["POST"])
def clear_logs():
    with logs_lock:
        logs.clear()
    return "OK"


# ----- BOTONES -----
@app.route("/ping", methods=["POST"])
def ping():
    msg = send_and_wait("PING#\n")
    return render_template("index.html", status=msg)

@app.route("/stop", methods=["POST"])
def stop():
    msg = send_and_wait("STOP#\n")
    return render_template("index.html", status=msg)

@app.route("/run", methods=["POST"])
def run():
    v = request.form.get("v", "0")
    t = request.form.get("t", "0")
    msg = send_and_wait(f"RUN {v} {t}#\n")
    return render_template("index.html", status=msg)

# TUNE COMPLETO
@app.route("/tune", methods=["POST"])
def tune():
    params = [
        request.form.get("KpL", "0"),
        request.form.get("KiL", "0"),
        request.form.get("KdL", "0"),
        request.form.get("KpR", "0"),
        request.form.get("KiR", "0"),
        request.form.get("KdR", "0"),
    ]
    msg = send_and_wait("TUNE " + " ".join(params) + "#\n")
    return render_template("index.html", status=msg)

# TUNE LEFT
@app.route("/tune_l", methods=["POST"])
def tune_l():
    params = [
        request.form.get("KpL", "0"),
        request.form.get("KiL", "0"),
        request.form.get("KdL", "0"),
    ]
    msg = send_and_wait("TUNE_L " + " ".join(params) + "#\n")
    return render_template("index.html", status=msg)

# TUNE RIGHT
@app.route("/tune_r", methods=["POST"])
def tune_r():
    params = [
        request.form.get("KpR", "0"),
        request.form.get("KiR", "0"),
        request.form.get("KdR", "0"),
    ]
    msg = send_and_wait("TUNE_R " + " ".join(params) + "#\n")
    return render_template("index.html", status=msg)


if __name__ == "__main__":
    app.run(host="0.0.0.0", port=5000, debug=False)
