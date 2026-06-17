#include <Arduino.h>
#include <cmath>

static String fmtFloat(float v, int decimals = 1) {
    if (isnan(v) || isinf(v)) return "N/A";
    return String(v, decimals);
}

String sensorUI(float temp, float hum, float batteryVoltage, int batteryPercent, const String& timeStr, bool sdReady) {
    return R"rawliteral(
        <!DOCTYPE html>
        <html>
        <head>
            <meta charset="UTF-8">
            <meta name="viewport" content="width=device-width, initial-scale=1">
            <title>Status</title>
            <style>
                body { font-family: sans-serif; line-height: 1.6; padding: 20px; }
                button { padding: 10px 20px; font-size: 16px; margin: 10px 10px 10px 0; }
            </style>
            <script>
                function refreshPage() {
                    location.reload();
                }
                function exitWiFiMode() {
                    fetch('/exit', { method: 'POST' })
                        .then(() => alert('Exit command sent'))
                        .catch(() => alert('Failed to send'));
                }
                function powerOn() {
                    fetch('/power-on', { method: 'POST' }).then(() => alert('Power ON'));
                }
                function powerOff() {
                    fetch('/power-off', { method: 'POST' }).then(() => alert('Power OFF'));
                }
                function syncRTCFromBrowser() {
                    const now = new Date();
                    const localTime = now.toLocaleString('sv-SE').replace(' ', 'T'); // e.g. "2025-05-13T21:30:45"

                    fetch('/rtc-sync-browser', {
                        method: 'POST',
                        headers: { 'Content-Type': 'application/json' },
                        body: JSON.stringify({ time: localTime })
                    })
                    .then(res => {
                        if (!res.ok) throw new Error(`HTTP ${res.status}`);
                        return res.text();
                    })
                    .then(msg => alert(msg))
                    .catch(e => alert("Sync failed: " + e.message));
                }
                function loadFileTree() {
                    fetch('/files')
                        .then(res => {
                            if (!res.ok) throw new Error(`HTTP ${res.status}`);
                            return res.json();
                        })
                        .then(data => renderFileTree(data))
                        .catch(e => alert("Failed to load file list: " + e.message));
                }

                function renderFileTree(tree, container = document.getElementById("fileTree")) {
                    container.innerHTML = '';
                    const ul = document.createElement("ul");

                    tree.forEach(item => {
                        const li = document.createElement("li");
                        if (item.type === "file") {
                            li.appendChild(document.createTextNode(item.name));
                            const btn = document.createElement("button");
                            btn.textContent = "Download";
                            btn.style.marginLeft = "10px";
                            btn.onclick = (e) => { e.stopPropagation(); downloadFile(item.path); };
                            li.appendChild(btn);
                            li.style.cursor = "pointer";
                            li.onclick = () => loadFileContent(item.path);
                        } else {
                            li.textContent = item.name;
                            const childDiv = document.createElement("div");
                            renderFileTree(item.children || [], childDiv);
                            li.appendChild(childDiv);
                        }
                        ul.appendChild(li);
                    });

                    container.appendChild(ul);
                }

                function downloadFile(path) {
                    const link = document.createElement('a');
                    link.href = `/download?path=${encodeURIComponent(path)}`;
                    link.download = path.split('/').pop();
                    document.body.appendChild(link);
                    link.click();
                    document.body.removeChild(link);
                }

                async function downloadAllLogs() {
                    try {
                        const res = await fetch('/files');
                        if (!res.ok) throw new Error(`HTTP ${res.status}`);
                        const data = await res.json();

                        const logFiles = [];
                        function collectLogs(tree) {
                            for (const item of tree) {
                                if (item.type === "file") logFiles.push(item.path);
                                else if (item.type === "dir") collectLogs(item.children || []);
                            }
                        }
                        collectLogs(data);

                        if (logFiles.length === 0) { alert("No log files found"); return; }

                        // Trigger downloads sequentially with a delay so browsers don't block them
                        for (const path of logFiles) {
                            const link = document.createElement('a');
                            link.href = `/download?path=${encodeURIComponent(path)}`;
                            link.download = path.split('/').pop();
                            document.body.appendChild(link);
                            link.click();
                            document.body.removeChild(link);
                            await new Promise(r => setTimeout(r, 300));
                        }
                    } catch(e) { alert("Download failed: " + e.message); }
                }

                function loadFileContent(path) {
                    fetch(`/file?path=${encodeURIComponent(path)}`)
                        .then(res => {
                            if (!res.ok) throw new Error(`HTTP ${res.status}`);
                            return res.text();
                        })
                        .then(text => {
                            document.getElementById("fileContent").textContent = text;
                        })
                        .catch(e => alert("Failed to load file content: " + e.message));
                }
                function formatSDCard() {
                    if (confirm("Clear all SD card logs? This cannot be undone.")) {
                        fetch('/format-sd', { method: 'POST' })
                            .then(res => {
                                if (!res.ok) throw new Error(`HTTP ${res.status}`);
                                return res.text();
                            })
                            .then(msg => alert(msg))
                            .catch(e => alert("Operation failed: " + e.message));
                    }
                }
                function remountSD() {
                    fetch('/sd-reinit', { method: 'POST' })
                        .then(res => res.text())
                        .then(msg => { alert(msg); location.reload(); })
                        .catch(() => alert("Remount request failed"));
                }
            </script>
        </head>
        <body>
            <h1>ESP32 Status Panel</h1>
            <p><strong>Temperature:</strong> )rawliteral" + fmtFloat(temp, 1) + R"rawliteral( C</p>
            <p><strong>Humidity:</strong> )rawliteral" + fmtFloat(hum, 1) + R"rawliteral( %</p>
            <p><strong>Battery voltage:</strong> )rawliteral" + fmtFloat(batteryVoltage, 2) + R"rawliteral( V ()rawliteral" + (isnan(batteryVoltage) || isinf(batteryVoltage) ? String("N/A") : String(batteryPercent)) + R"rawliteral( %)</p>
            <p><strong>Current time:</strong> )rawliteral" + timeStr + R"rawliteral(</p>
            <p><strong>SD card:</strong> )rawliteral" + String(sdReady ? "OK" : "ERROR") + R"rawliteral(</p>

            <button onclick="refreshPage()">Refresh</button>
            <button onclick="downloadAllLogs()">Download all logs</button>
            <button onclick="powerOn()">Power ON</button>
            <button onclick="powerOff()">Power OFF</button>
            <button onclick="formatSDCard()">Clear SD logs</button>
            <button onclick="remountSD()">Remount SD</button>
            <button onclick="syncRTCFromBrowser()">Sync RTC with phone time</button>
            <button onclick="exitWiFiMode()">Exit WiFi mode</button>

            <h2>SD Card File Browser</h2>
            <div id="fileTree" style="max-height: 300px; overflow: auto; border: 1px solid #ccc; padding: 10px;"></div>
            <pre id="fileContent" style="white-space: pre-wrap; border: 1px solid #ccc; padding: 10px; margin-top: 10px;"></pre>
            <script>
                window.onload = function () {
                    loadFileTree();
                };
            </script>
        </body>
        </html>
    )rawliteral";
}
