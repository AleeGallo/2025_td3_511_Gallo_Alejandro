const term = document.getElementById("terminal");
const appendTerm = s => {
    term.textContent += s + "\n";
    term.scrollTop = term.scrollHeight;
};

// ===========================
// GET LOG
// ===========================
document.getElementById("btnGetLog").addEventListener("click", async () => {
    const res = await fetch("/api/log");
    const j = await res.json();
    appendTerm("TX: GET LOG");
    appendTerm(j.raw.split("\n").slice(0, 15).join("\n"));
});

// ===========================
// COMANDO MANUAL
// ===========================
document.getElementById("btnManualSend").addEventListener("click", async () => {

    const mode = document.getElementById("manualMode").value;
    const param = document.getElementById("manualParam").value.trim();
    const value = document.getElementById("manualValue").value.trim();

    if (param === "") {
        alert("Debes ingresar un parámetro.");
        return;
    }
    if (mode === "SET" && value === "") {
        alert("Debes ingresar valor.");
        return;
    }

    const payload = { mode, param };
    if (mode === "SET") payload.value = value;

    const cmd = (mode === "GET")
        ? `GET ${param}`
        : `SET ${param} ${value}`;

    appendTerm("> " + cmd);

    try {
        const res = await fetch("/api/manual", {
            method: "POST",
            headers: {"Content-Type": "application/json"},
            body: JSON.stringify(payload)
        });

        const j = await res.json();
        appendTerm("< " + j.rx);

    } catch(e) {
        appendTerm("< ERROR COMUNICACIÓN");
    }
});

// ===========================
// CHART GET I
// ===========================
const ctx = document.getElementById("chartI").getContext("2d");

const data = {
    labels: [],
    datasets: [{
        label: "I (A)",
        data: [],
        borderWidth: 2,
        fill: false
    }]
};

const chart = new Chart(ctx, {
    type: "line",
    data,
    options: {
        animation: false,
        scales: {
            x: { display: false },
            y: { beginAtZero: true }
        }
    }
});

let plotInterval = null;

function startPlot() {
    const interval = parseInt(document.getElementById("interval").value) || 200;

    if (plotInterval) clearInterval(plotInterval);

    plotInterval = setInterval(async () => {
        try {
            const res = await fetch("/api/get_i");
            const j = await res.json();

            appendTerm("RX: " + j.raw);

            data.labels.push(new Date().toLocaleTimeString());
            data.datasets[0].data.push(j.value ?? NaN);

            if (data.labels.length > 200) {
                data.labels.shift();
                data.datasets[0].data.shift();
            }

            chart.update();

        } catch (e) {
            appendTerm("ERR fetch: " + e);
        }
    }, interval);
}

document.getElementById("startPlot").addEventListener("click", () => {
    startPlot();
    appendTerm("Plot ON");
});

document.getElementById("stopPlot").addEventListener("click", () => {
    if (plotInterval) clearInterval(plotInterval);
    appendTerm("Plot OFF");
});

// — Ping inicial —
(async function(){
    try {
        const r = await fetch("/api/get?cmd=" + encodeURIComponent("GET KP"));
        await r.json();
        appendTerm("Server OK");
    } catch(e) {
        appendTerm("Server ERROR");
    }
})();
