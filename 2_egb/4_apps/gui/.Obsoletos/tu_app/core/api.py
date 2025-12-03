import requests

URL_LOG = "http://localhost:8000/log"
URL_SET = "http://localhost:8000/params"

def send_params(payload):
    return requests.post(URL_SET, json=payload, timeout=1)

def get_current_value():
    resp = requests.get(URL_LOG, timeout=1)
    return float(resp.text)
