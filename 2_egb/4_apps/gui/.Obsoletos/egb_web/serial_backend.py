import os
import threading
import time

class SerialBackend:
    def __init__(self, device_path='/dev/egb'):
        self.device_path = device_path
        self.lock = threading.Lock()
        self.fd = None
        self.open_device()

    def open_device(self):
        try:
            self.fd = os.open(self.device_path, os.O_RDWR | os.O_NONBLOCK)
            print(f"Dispositivo {self.device_path} abierto.")
        except OSError as e:
            print(f"Error abriendo {self.device_path}: {e}")
            self.fd = None

    def close_device(self):
        if self.fd:
            os.close(self.fd)
            self.fd = None

    def send_command(self, command):
        if not self.fd:
            return "ERR_NO_DEVICE"
        with self.lock:
            try:
                # Añadir newline si no existe
                if not command.endswith('\n'):
                    command += '\n'
                os.write(self.fd, command.encode())
                return "OK"
            except OSError as e:
                return f"ERR_WRITE: {e}"

    def read_response(self, timeout=5):
        if not self.fd:
            return "ERR_NO_DEVICE"
        start_time = time.time()
        buffer = ""
        while time.time() - start_time < timeout:
            try:
                data = os.read(self.fd, 1).decode()
                if data in ['\n', '\r']:
                    if buffer:
                        return buffer.strip()
                else:
                    buffer += data
            except OSError:
                time.sleep(0.1)  # Esperar un poco si no hay datos
        return "ERR_NO_RESPONSE" if not buffer else buffer.strip()

# Instancia global
backend = SerialBackend()