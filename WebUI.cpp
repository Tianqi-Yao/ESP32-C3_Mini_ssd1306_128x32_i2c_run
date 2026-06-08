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
                        .then(() => alert('✅ 已发送退出指令'))
                        .catch(() => alert('❌ 发送失败'));
                }
                function powerOn() {
                    fetch('/power-on').then(() => alert('⚡ Power ON'));
                }
                function powerOff() {
                    fetch('/power-off').then(() => alert('⚡ Power OFF'));
                }
                function syncRTCFromBrowser() {
                    const now = new Date();
                    const localTime = now.toLocaleString('sv-SE').replace(' ', 'T'); // 例如 "2025-05-13T21:30:45.123Z"

                    fetch('/rtc-sync-browser', {
                        method: 'POST',
                        headers: { 'Content-Type': 'application/json' },
                        body: JSON.stringify({ time: localTime })
                    })
                    .then(res => res.text())
                    .then(msg => alert(msg))
                    .catch(() => alert("❌ 同步失败"));
                }
                function loadFileTree() {
                    fetch('/files')
                        .then(res => res.json())
                        .then(data => renderFileTree(data))
                        .catch(() => alert("❌ 加载文件列表失败"));
                }

                function renderFileTree(tree, container = document.getElementById("fileTree")) {
                    container.innerHTML = '';
                    const ul = document.createElement("ul");

                    tree.forEach(item => {
                        const li = document.createElement("li");
                        if (item.type === "file") {
                            li.innerHTML = `
                                ${item.name}
                                <button onclick="downloadFile('${item.path}')" style="margin-left:10px">⬇️</button>
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
                                alert("⚠️ 没有找到任何日志文件");
                                return;
                            }

                            // 顺序下载所有日志文件
                            logFiles.forEach(path => {
                                const link = document.createElement('a');
                                link.href = `/download?path=${encodeURIComponent(path)}`;
                                link.download = path.split('/').pop();
                                document.body.appendChild(link);
                                link.click();
                                document.body.removeChild(link);
                            });
                        })
                        .catch(() => alert("❌ 无法获取文件列表"));
                }

                function loadFileContent(path) {
                    fetch(`/file?path=${encodeURIComponent(path)}`)
                        .then(res => res.text())
                        .then(text => {
                            document.getElementById("fileContent").textContent = text;
                        });
                }
                function formatSDCard() {
                    if (confirm("⚠️ 确定要清空 SD 卡日志吗？此操作不可恢复")) {
                        fetch('/format-sd')
                            .then(res => res.text())
                            .then(msg => alert(msg))
                            .catch(() => alert("❌ 操作失败"));
                    }
                }
            </script>
        </head>
        <body>
            <h1>📊 ESP32 状态面板</h1>
            <p><strong>温度：</strong> )rawliteral" + String(temp) + R"rawliteral( °C</p>
            <p><strong>湿度：</strong> )rawliteral" + String(hum) + R"rawliteral( %</p>
            <p><strong>电池电压：</strong> )rawliteral" + String(batteryVoltage, 2) + R"rawliteral( V（)rawliteral" + String(batteryPercent) + R"rawliteral( %）</p>
            <p><strong>当前时间：</strong> )rawliteral" + timeStr + R"rawliteral(</p>

            <button onclick="refreshPage()">🔄 刷新</button>
            <button onclick="downloadAllLogs()">⬇️ 下载全部日志文件</button>
            <button onclick="powerOn()">⚡ Power ON</button>
            <button onclick="powerOff()">🛑 Power OFF</button>
            <button onclick="formatSDCard()">🗑️ 清空 SD 卡日志</button>
            <button onclick="syncRTCFromBrowser()">🕒 用手机时间校准 RTC</button>
            <button onclick="exitWiFiMode()">🔌 退出 WiFi 模式</button>

            <h2>📁 SD 卡文件浏览器</h2>
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
