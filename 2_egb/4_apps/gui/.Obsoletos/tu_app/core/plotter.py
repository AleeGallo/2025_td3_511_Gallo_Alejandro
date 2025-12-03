import pyqtgraph as pg

class RealTimePlot:
    def __init__(self, container):
        self.plot = pg.PlotWidget()
        container.addWidget(self.plot)
        self.data = []
        self.curve = self.plot.plot(self.data)
        self.plot.setTitle("Corriente en tiempo real")
        self.plot.setLabel("left", "Corriente (A)")
        self.plot.setLabel("bottom", "Muestras")

    def update(self, value):
        self.data.append(value)
        if len(self.data) > 500:
            self.data.pop(0)
        self.curve.setData(self.data)
