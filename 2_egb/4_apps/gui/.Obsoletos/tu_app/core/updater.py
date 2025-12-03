from PyQt6.QtCore import QTimer
from core.api import get_current_value

class Updater:
    def __init__(self, plotter):
        self.plotter = plotter
        self.timer = QTimer()
        self.timer.timeout.connect(self._tick)

    def start(self, interval_ms=100):
        self.timer.start(interval_ms)

    def _tick(self):
        try:
            v = get_current_value()
            self.plotter.update(v)
        except Exception:
            pass
