#include <Arduino.h>

String sensorUI(float temp, float hum, float batteryVoltage, int batteryPercent, const String& timeStr) {
    return R"rawliteral(
        <!DOCTYPE html>
        <html>
        <head>
            <meta charset="UTF-8">
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
                    fetch('/exit')
                        .then(() => alert('Exit command sent'))
                        .catch(() => alert('Failed to send'));
                }
                function powerOn() {
                    fetch('/power-on').then(() => alert('Power ON'));
                }
                function powerOff() {
                    fetch('/power-off').then(() => alert('Power OFF'));
                }
                function syncRTCFromBrowser() {
                    const now = new Date();
                    const localTime = now.toLocaleString('sv-SE').replace(' ', 'T'); // e.g. "2025-05-13T21:30:45"

                    fetch('/rtc-sync-browser', {
                        method: 'POST',
                        headers: { 'Content-Type': 'application/json' },
                        body: JSON.stringify({ time: localTime })
                    })
                    .then(res => res.text())
                    .then(msg => alert(msg))
                    .catch(() => alert("Sync failed"));
                }
                function loadFileTree() {
                    fetch('/files')
                        .then(res => res.json())
                        .then(data => renderFileTree(data))
                        .catch(() => alert("Failed to load file list"));
                }

                function renderFileTree(tree, container = document.getElementById("fileTree")) {
                    container.innerHTML = '';
                    const ul = document.createElement("ul");

                    tree.forEach(item => {
                        const li = document.createElement("li");
                        if (item.type === "file") {
                            li.innerHTML = `
                                ${item.name}
                                <button onclick="downloadFile('${item.path}')" style="margin-left:10px">Download</button>
                            `;
                            li.style.cursor = "pointer";
                            li.onclick = () => loadFileContent(item.path);
                        } else {
                            li.textContent = item.name;
                            const childDiv = document.createElement("div");
                            renderFileTree(item.children, childDiv);
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

                function downloadAllLogs() {
                    fetch('/files')
                        .then(res => res.json())
                        .then(data => {
                            const logFiles = [];

                            function collectLogs(tree, prefix = "") {
                                for (const item of tree) {
                                    const fullPath = prefix + "/" + item.name;
                                    if (item.type === "file" && fullPath.startsWith("/logs/")) {
                                        logFiles.push(fullPath);
                                    }
                                    if (item.type === "dir") {
                                        collectLogs(item.children, fullPath);
                                    }
                                }
                            }

                            collectLogs(data);

                            if (logFiles.length === 0) {
                                alert("No log files found");
                                return;
                            }

                            // Download all log files in sequence
                            logFiles.forEach(path => {
                                const link = document.createElement('a');
                                link.href = `/download?path=${encodeURIComponent(path)}`;
                                link.download = path.split('/').pop();
                                document.body.appendChild(link);
                                link.click();
                                document.body.removeChild(link);
                            });
                        })
                        .catch(() => alert("Failed to fetch file list"));
                }

                function loadFileContent(path) {
                    fetch(`/file?path=${encodeURIComponent(path)}`)
                        .then(res => res.text())
                        .then(text => {
                            document.getElementById("fileContent").textContent = text;
                        });
                }
                function formatSDCard() {
                    if (confirm("Clear all SD card logs? This cannot be undone.")) {
                        fetch('/format-sd')
                            .then(res => res.text())
                            .then(msg => alert(msg))
                            .catch(() => alert("Operation failed"));
                    }
                }
            </script>
        </head>
        <body>
            <h1>ESP32 Status Panel</h1>
            <p><strong>Temperature:</strong> )rawliteral" + String(temp) + R"rawliteral( C</p>
            <p><strong>Humidity:</strong> )rawliteral" + String(hum) + R"rawliteral( %</p>
            <p><strong>Battery voltage:</strong> )rawliteral" + String(batteryVoltage, 2) + R"rawliteral( V ()rawliteral" + String(batteryPercent) + R"rawliteral( %)</p>
            <p><strong>Current time:</strong> )rawliteral" + timeStr + R"rawliteral(</p>

            <button onclick="refreshPage()">Refresh</button>
            <button onclick="downloadAllLogs()">Download all logs</button>
            <button onclick="powerOn()">Power ON</button>
            <button onclick="powerOff()">Power OFF</button>
            <button onclick="formatSDCard()">Clear SD logs</button>
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
