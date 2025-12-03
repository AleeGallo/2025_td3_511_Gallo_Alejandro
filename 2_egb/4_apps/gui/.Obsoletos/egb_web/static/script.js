let chart;
let dataPoints = [];
let timeLabels = [];
let startTime = Date.now();

document.addEventListener('DOMContentLoaded', function() {
    const ctx = document.getElementById('currentChart').getContext('2d');
    chart = new Chart(ctx, {
        type: 'line',
        data: {
            labels: timeLabels,
            datasets: [{
                label: 'Corriente (I)',
                data: dataPoints,
                borderColor: 'rgba(75, 192, 192, 1)',
                backgroundColor: 'rgba(75, 192, 192, 0.2)',
                fill: true,
            }]
        },
        options: {
            scales: {
                x: { title: { display: true, text: 'Tiempo (s)' } },
                y: { title: { display: true, text: 'Corriente' } }
            }
        }
    });

    // Polling para gráfico en tiempo real (cada 1s)
    setInterval(updateChart, 1000);
});

function setParam(param) {
    const value = document.getElementById(param).value;
    fetch(`/set/${param}/${value}`)
        .then(response => response.json())
        .then(data => logToTerminal(`${data.command} -> ${data.response}`));
}

function getParam(param) {
    fetch(`/get/${param}`)
        .then(response => response.json())
        .then(data => {
            logToTerminal(`${data.command} -> ${data.response}`);
            if (param === 'I') {
                // Actualizar input si es GET I
                document.getElementById('I').value = data.response;
            }
        });
}

function updateChart() {
    fetch('/get/I')
        .then(response => response.json())
        .then(data => {
            const current = parseFloat(data.response);
            if (!isNaN(current)) {
                const elapsed = (Date.now() - startTime) / 1000;
                timeLabels.push(elapsed.toFixed(1));
                dataPoints.push(current);
                if (timeLabels.length > 50) {  // Mantener solo 50 puntos
                    timeLabels.shift();
                    dataPoints.shift();
                }
                chart.update();
            }
            logToTerminal(`${data.command} -> ${data.response}`);
        });
}

function logToTerminal(message) {
    const terminal = document.getElementById('terminal');
    terminal.value += message + '\n';
    terminal.scrollTop = terminal.scrollHeight;
}

function clearTerminal() {
    document.getElementById('terminal').value = '';
}