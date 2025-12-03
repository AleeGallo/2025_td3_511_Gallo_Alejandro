#!/bin/bash

echo "=== Creando entorno virtual ==="
python3 -m venv venv

echo "=== Activando entorno virtual ==="
source venv/bin/activate

echo "=== Instalando dependencias ==="
pip install flask flask-socketio eventlet

echo "=== Ejecutando app.py ==="
python app.py