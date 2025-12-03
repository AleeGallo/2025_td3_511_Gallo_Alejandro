import os
import time
import select

DEVICE = "/dev/egb"
BUFFER_SIZE = 256
SIMULATE = False

# Detectamos si el device existe
if not os.path.exists(DEVICE):
    print("/dev/egb NO encontrado → MODO SIMULACIÓN")
    SIMULATE = True
else:
    print("/dev/egb detectado → MODO REAL")

def is_device_available():
    return not SIMULATE

def open_device():
    # Modo no bloqueante
    # return os.open(DEVICE, os.O_RDWR | os.O_NONBLOCK)
    return os.open(DEVICE, os.O_RDWR)

def send_command(cmd, timeout=1.0):
    """
    Envía un comando al driver y espera una respuesta.
    Si SIMULATE=True → devuelve respuestas mock.
    """
    # -------------------------------
    # SIMULACIÓN
    # -------------------------------
    if SIMULATE:
        c = cmd.upper()

        if c.startswith("SET "):
            return "OK"

        if c == "GET I":
            import random
            return f"I={random.uniform(0, 2):.0f}"

        if c == "GET LOG":
            import random
            lines = [f"I={random.uniform(0,2):.0f}" for _ in range(20)]
            return "\n".join(lines)

        if c.startswith("GET "):
            return "0.00"

        return "ERR_NO_DEVICE"

    # -------------------------------
    # MODO REAL
    # -------------------------------
    try:
        fd = open_device()
    except Exception as e:
        return f"ERR_OPEN:{e}"

    # Aseguramos newline
    if not cmd.endswith("\n"):
        cmd += "\n"

    try:
        os.write(fd, cmd.encode())
    except Exception as e:
        os.close(fd)
        return f"ERR_WRITE:{e}"

    # --- LECTURA ---
    resp = []
    end = time.time() + timeout

    try:
        while time.time() < end:
            r, _, _ = select.select([fd], [], [], 0.1)

            if fd in r:
                chunk = os.read(fd, BUFFER_SIZE)

                if not chunk:
                    continue

                s = chunk.decode(errors="ignore")
                resp.append(s)

                # El driver SIEMPRE responde con "\n"
                if "\n" in s:
                    break
    finally:
        os.close(fd)

    out = "".join(resp)

    if "\n" in out:
        out = out.split("\n")[0]

    return out.strip()
