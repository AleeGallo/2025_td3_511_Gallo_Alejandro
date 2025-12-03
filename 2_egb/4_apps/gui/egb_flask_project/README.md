EGB Flask Web GUI

Cómo usar (en Debian / Raspberry Linux):

1) Copiar carpeta al Raspberry (scp, git clone o pendrive)
2) Instalar dependencias:
   sudo apt update
   sudo apt install -y python3 python3-venv python3-pip
3) (opcional) Crear virtualenv:
   python3 -m venv .venv
   source .venv/bin/activate
4) Instalar Flask:
   pip install -r requirements.txt
5) Ejecutar:
   python3 app.py
6) Abrir en el navegador:
   http://<IP_DE_RPI>:5000

Notas:
- El backend intenta usar /dev/egb; si no existe entra en modo SIMULACIÓN para que puedas probar sin hardware.
- Endpoints REST:
  POST /api/set  JSON {param: "KP", value: "1.2"}
  GET  /api/get?cmd=GET%20KP
  GET  /api/get_i
  GET  /api/log
