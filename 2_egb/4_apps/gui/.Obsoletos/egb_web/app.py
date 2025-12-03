from flask import Flask, render_template, request, jsonify
from serial_backend import backend

app = Flask(__name__)

@app.route('/')
def index():
    return render_template('index.html')

@app.route('/set/<param>/<value>')
def set_param(param, value):
    # Validar parámetros
    valid_params = ['KP', 'KI', 'KD', 'R', 'T', 'SP']
    if param.upper() not in valid_params:
        return jsonify({'response': 'ERR_INVALID_PARAM'})
    try:
        float(value)  # Validar que sea numérico
    except ValueError:
        return jsonify({'response': 'ERR_INVALID_VALUE'})
    
    command = f"SET {param.upper()} {value}"
    backend.send_command(command)
    response = backend.read_response()
    return jsonify({'response': response, 'command': command})

@app.route('/get/<param>')
def get_param(param):
    valid_params = ['KP', 'KI', 'KD', 'SP', 'R', 'VI', 'I']
    if param.upper() not in valid_params:
        return jsonify({'response': 'ERR_INVALID_PARAM'})
    
    command = f"GET {param.upper()}"
    backend.send_command(command)
    response = backend.read_response()
    return jsonify({'response': response, 'command': command})

@app.route('/get_log')
def get_log():
    command = "GET LOG"
    backend.send_command(command)
    response = backend.read_response()
    return jsonify({'response': response, 'command': command})

if __name__ == '__main__':
    app.run(debug=True, host='0.0.0.0', port=5000)