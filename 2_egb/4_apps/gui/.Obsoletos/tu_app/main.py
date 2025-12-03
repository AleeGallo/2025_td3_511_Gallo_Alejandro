import sys
from PyQt6.QtWidgets import QApplication, QWidget
from PyQt6 import uic
from core.api import send_params
from core.plotter import RealTimePlot
from core.updater import Updater

class MainApp(QWidget):
    def __init__(self):
        super().__init__()
        uic.loadUi("gui/main_window.ui", self)
        self.btnSend.clicked.connect(self.send_params)
        self.btnStart.clicked.connect(self.start_read)
        self.plotter = RealTimePlot(self.graphLayout)
        self.updater = Updater(self.plotter)

    def send_params(self):
        values = {
            "Kp": float(self.inputKp.text()),
            "Ki": float(self.inputKi.text()),
            "Kd": float(self.inputKd.text()),
            "R":  float(self.inputR.text()),
            "T":  float(self.inputT.text()),
            "SP": float(self.inputSP.text())
        }
        send_params(values)

    def start_read(self):
        self.updater.start(100)

if __name__ == "__main__":
    app = QApplication(sys.argv)
    window = MainApp()
    window.show()
    sys.exit(app.exec())
