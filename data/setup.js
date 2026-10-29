let currentConfig = {};
let isDeviceRotator = false;
let firmwareVersion = "N/A";
let networkCounter = 0; // Counter for unique IDs
// Tope del recorrido del enfocador; mismo valor que MAX_FOCUSER_STEPS en
// Config.h. El limite real lo fija el usuario con max_steps.
const MAX_FOCUSER_STEPS = 999999;

function switchTab(tabId) {
    document.querySelectorAll(".tab-content").forEach(el => el.classList.remove("active"));
    document.querySelectorAll(".nav-btn").forEach(el => el.classList.remove("active"));
    
    document.getElementById("tab-" + tabId).classList.add("active");
    document.getElementById("btn-tab-" + tabId).classList.add("active");
}

function togglePasswordVisibility(inputId, buttonElement) {
    const passwordInput = document.getElementById(inputId);
    if (passwordInput.type === "password") {
        passwordInput.type = "text";
        buttonElement.innerHTML = "🙈"; // Eye with slash
    } else {
        passwordInput.type = "password";
        buttonElement.innerHTML = "👁️"; // Open eye
    }
}

async function setLanguage(lang) {
    localStorage.setItem("language", lang);
    await translatePage();
}

async function translatePage() {
    // Sin preferencia guardada, el idioma del navegador si lo tenemos; si no, inglés.
    const disponibles = ["en", "es"];
    let lang = localStorage.getItem("language") || (navigator.language || "").split("-")[0];
    if (!disponibles.includes(lang)) lang = "en";
    const response = await fetch(`/locales/${lang}.json`);
    const translations = await response.json();

    document.querySelectorAll("[data-i18n]").forEach(element => {
        const key = element.getAttribute("data-i18n");
        element.innerHTML = translations[key] || element.innerHTML;
    });

    // Translate placeholders
    document.querySelectorAll("[data-i18n-placeholder]").forEach(element => {
        const key = element.getAttribute("data-i18n-placeholder");
        if (translations[key]) {
            element.setAttribute("placeholder", translations[key]);
        }
    });
}

async function loadConfig() {
    try {
        await translatePage(); // Translate early to avoid layout jumps
        // Fetch firmware version
        try {
            const fwRes = await fetch("/api/v1/firmwareversion");
            const fwData = await fwRes.json();
            firmwareVersion = fwData.Value || "N/A";
        } catch (e) {
            console.error("Error fetching firmware version:", e);
        }

        // Pequeño delay artificial opcional para que se vea el splash (0.5s)
        await new Promise(r => setTimeout(r, 500));
        
        const res = await fetch("/api/setup/config");
        currentConfig = await res.json();
        
        isDeviceRotator = currentConfig.isRotator === true;
        
        if (isDeviceRotator) {
            document.getElementById("rotator-section").classList.remove("hidden");
            document.getElementById("move-rotator-section").classList.remove("hidden");
            try {
                const pRes = await fetch("/api/v1/rotator/0/position");
                const pData = await pRes.json();
                if(pData.ErrorNumber === 0) document.getElementById("r_target_angle").value = pData.Value;
            } catch(e) {}
        } else {
            document.getElementById("focuser-section").classList.remove("hidden");
            document.getElementById("move-focuser-section").classList.remove("hidden");
            try {
                const pRes = await fetch("/api/v1/focuser/0/position");
                const pData = await pRes.json();
                if(pData.ErrorNumber === 0) document.getElementById("f_target_pos").value = pData.Value;
            } catch(e) {}
        }
        
        // Cargar Enfocador
        if(currentConfig.focuser) {
            document.getElementById("f_current").value = currentConfig.focuser.current_ma || 800;
            document.getElementById("f_hold_current").value =
                currentConfig.focuser.hold_current_ma
                ?? (currentConfig.focuser.hold === false ? 0 : Math.round((currentConfig.focuser.current_ma || 400) / 2));
            document.getElementById("f_microsteps").value = currentConfig.focuser.microsteps || 4;
            document.getElementById("f_reverse").checked = !!currentConfig.focuser.reverse;
            document.getElementById("f_stall").value = currentConfig.focuser.stall_threshold ?? 100;
            document.getElementById("f_max_steps").value = currentConfig.focuser.max_steps || MAX_FOCUSER_STEPS;
            document.getElementById("f_max_speed").value = currentConfig.focuser.max_speed || 2500;
            document.getElementById("f_accel").value = currentConfig.focuser.acceleration || 1000;
            document.getElementById("f_stall_min").value = currentConfig.focuser.stall_min_speed || 400;
            
            // Movement parameters
            document.getElementById("f_fine_step").value = currentConfig.focuser.fine_step || 10;
            document.getElementById("f_coarse_step").value = currentConfig.focuser.coarse_step || 100;
        }

        // Cargar Rotador
        if(currentConfig.rotator) {
            document.getElementById("r_current").value = currentConfig.rotator.current_ma || 800;
            document.getElementById("r_hold_current").value =
                currentConfig.rotator.hold_current_ma
                ?? (currentConfig.rotator.hold === false ? 0 : Math.round((currentConfig.rotator.current_ma || 400) / 2));
            document.getElementById("r_microsteps").value = currentConfig.rotator.microsteps || 8;
            document.getElementById("r_stall").value = currentConfig.rotator.stall_threshold ?? 6;
            document.getElementById("r_steps_deg").value = currentConfig.rotator.steps_per_degree || 37.75;
            document.getElementById("r_max_speed").value = currentConfig.rotator.max_speed || 2000;
            document.getElementById("r_accel").value = currentConfig.rotator.acceleration || 1000;
            document.getElementById("r_stall_min").value = currentConfig.rotator.stall_min_speed || 100;
            document.getElementById("r_reverse").checked = !!currentConfig.rotator.reverse;
            
            // Movement parameters
            document.getElementById("r_fine_step").value = currentConfig.rotator.fine_step || 1;
            document.getElementById("r_coarse_step").value = currentConfig.rotator.coarse_step || 10;
        }

        // Cargar WiFi
        if(currentConfig.wifi) {
            document.getElementById("ap_name").value = currentConfig.wifi.ap_name || "";
            document.getElementById("ap_password").value = currentConfig.wifi.ap_password || "";
            document.getElementById("wifi_timeout").value = currentConfig.wifi.timeout_ms || 1000;
            document.getElementById("wifi_retries").value = currentConfig.wifi.retries || 10;
            const nets = currentConfig.wifi.networks || [];
            document.getElementById("networks-container").innerHTML = "";
            nets.forEach(n => addNetwork(n.ssid, n.password));
        }

        // Ocultar Splash y mostrar contenido
        const splash = document.getElementById('splash-screen');
        splash.style.opacity = '0';
        setTimeout(() => {
            splash.style.display = 'none';
            document.getElementById('navbar').classList.remove('hidden');
            document.getElementById('main-content').classList.remove('hidden');
        }, 1500);

    } catch (e) {
        console.error(e);
        alert("Error cargando configuración. Comprueba la conexión.");
    }
    await translatePage();
}

function addNetwork(ssid = "", pass = "") {
    networkCounter++;
    const newId = "net-pass-" + networkCounter;
    const container = document.getElementById('networks-container');
    const div = document.createElement('div');
    div.className = 'network-row';
    div.draggable = true;
    
    // Drag events
    div.addEventListener('dragstart', handleDragStart, false);
    div.addEventListener('dragenter', handleDragEnter, false);
    div.addEventListener('dragover', handleDragOver, false);
    div.addEventListener('dragleave', handleDragLeave, false);
    div.addEventListener('drop', handleDrop, false);
    div.addEventListener('dragend', handleDragEnd, false);
    
    div.innerHTML = `
        <div class="drag-handle" style="cursor: grab; padding: 5px; font-size: 1.2rem; user-select: none;">☰</div>
        <input type="text" placeholder="Nombre (SSID)" data-i18n-placeholder="ssidPlaceholder" class="net-ssid" name="" autocomplete="off" value="${ssid}">
        <div class="password-input-container">
            <input type="password" placeholder="Contraseña" data-i18n-placeholder="passwordPlaceholder" class="net-pass" id="${newId}" name="" autocomplete="new-password" value="${pass}">
            <button type="button" class="toggle-password" onclick="togglePasswordVisibility('${newId}', this)">
                👁️
            </button>
        </div>
        <button class="btn-danger" data-i18n="delete" onclick="this.closest('.network-row').remove()">Eliminar</button>
    `;
    container.appendChild(div);
    translatePage(); // Ensure new elements are translated
}

let draggedRow = null;

function toggleMenu() {
    const navLinks = document.getElementById("nav-links");
    if(window.innerWidth <= 600) {
        navLinks.classList.toggle("active");
    }
}

function handleDragStart(e) {
    draggedRow = this;
    e.dataTransfer.effectAllowed = 'move';
    e.dataTransfer.setData('text/html', this.innerHTML);
    this.style.opacity = '0.4';
}

function handleDragOver(e) {
    if (e.preventDefault) e.preventDefault();
    e.dataTransfer.dropEffect = 'move';
    return false;
}

function handleDragEnter(e) {
    this.style.border = '2px dashed var(--corp-color)';
}

function handleDragLeave(e) {
    this.style.border = 'none';
}

function handleDrop(e) {
    if (e.stopPropagation) e.stopPropagation();

    if (draggedRow !== this) {
        const container = this.parentNode;
        const allRows = Array.from(container.children);
        const draggedIndex = allRows.indexOf(draggedRow);
        const targetIndex = allRows.indexOf(this);

        if (draggedIndex < targetIndex) {
            container.insertBefore(draggedRow, this.nextSibling);
        } else {
            container.insertBefore(draggedRow, this);
        }
    }
    return false;
}

function handleDragEnd(e) {
    this.style.opacity = '1';
    document.querySelectorAll('#networks-container .network-row').forEach(row => {
        row.style.border = 'none';
    });
}

async function saveConfig() {
    // Recoger WiFi
    const nets = [];
    document.querySelectorAll('#networks-container .network-row').forEach(row => {
        const s = row.querySelector('.net-ssid').value;
        const p = row.querySelector('.net-pass').value;
        if(s) nets.push({ssid: s, password: p});
    });

    const newConfig = {
        wifi: {
            ap_name: document.getElementById('ap_name').value,
            ap_password: document.getElementById('ap_password').value,
            timeout_ms: parseInt(document.getElementById('wifi_timeout').value),
            retries: parseInt(document.getElementById('wifi_retries').value),
            networks: nets
        }
    };

    // Guardar solo lo del modo activo
    newConfig.focuser = currentConfig.focuser || {};
    delete newConfig.focuser.hold; // clave vieja: la sustituye hold_current_ma
    newConfig.focuser.current_ma = parseInt(document.getElementById('f_current').value);
    newConfig.focuser.hold_current_ma = parseInt(document.getElementById('f_hold_current').value);
    newConfig.focuser.microsteps = parseInt(document.getElementById('f_microsteps').value);
    newConfig.focuser.stall_threshold = parseInt(document.getElementById('f_stall').value);
    // Acotado igual que en el firmware (MAX_FOCUSER_STEPS en Config.h).
    const maxSteps = parseInt(document.getElementById('f_max_steps').value);
    newConfig.focuser.max_steps = isNaN(maxSteps) ? MAX_FOCUSER_STEPS
        : Math.min(Math.max(maxSteps, 1), MAX_FOCUSER_STEPS);
    newConfig.focuser.fine_step = parseInt(document.getElementById('f_fine_step').value);
    newConfig.focuser.coarse_step = parseInt(document.getElementById('f_coarse_step').value);
    newConfig.focuser.max_speed = parseInt(document.getElementById('f_max_speed').value);
    newConfig.focuser.acceleration = parseInt(document.getElementById('f_accel').value);
    newConfig.focuser.stall_min_speed = parseInt(document.getElementById('f_stall_min').value);
    newConfig.focuser.reverse = document.getElementById('f_reverse').checked;

    newConfig.rotator = currentConfig.rotator || {};
    delete newConfig.rotator.hold; // clave vieja: la sustituye hold_current_ma
    newConfig.rotator.current_ma = parseInt(document.getElementById('r_current').value);
    newConfig.rotator.hold_current_ma = parseInt(document.getElementById('r_hold_current').value);
    newConfig.rotator.microsteps = parseInt(document.getElementById('r_microsteps').value);
    newConfig.rotator.stall_threshold = parseInt(document.getElementById('r_stall').value);
    newConfig.rotator.steps_per_degree = parseFloat(document.getElementById('r_steps_deg').value);
    newConfig.rotator.fine_step = parseFloat(document.getElementById('r_fine_step').value);
    newConfig.rotator.coarse_step = parseFloat(document.getElementById('r_coarse_step').value);
    newConfig.rotator.max_speed = parseInt(document.getElementById('r_max_speed').value);
    newConfig.rotator.acceleration = parseInt(document.getElementById('r_accel').value);
    newConfig.rotator.stall_min_speed = parseInt(document.getElementById('r_stall_min').value);
    newConfig.rotator.reverse = document.getElementById('r_reverse').checked;

    const btnSetup = document.querySelector('#tab-setup .btn-primary');
    const btnWifi = document.querySelector('#tab-wifi .btn-primary');
    const origText1 = btnSetup.innerText;
    const origText2 = btnWifi.innerText;
    
    btnSetup.innerText = "Guardando...";
    btnWifi.innerText = "Guardando...";
    btnSetup.disabled = true;
    btnWifi.disabled = true;

    try {
        const res = await fetch('/api/setup/config', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(newConfig)
        });
        const data = await res.json();
        const msg = document.getElementById('status-msg');
        msg.style.display = 'block';
        if(res.ok) {
            msg.style.backgroundColor = '#d4edda';
            msg.style.color = '#155724';
            msg.innerText = "¡Guardado con éxito! Reinicia el dispositivo para aplicar los cambios en el motor y WiFi.";
        } else {
            msg.style.backgroundColor = '#f8d7da';
            msg.style.color = '#721c24';
            msg.innerText = "Error al guardar: " + (data.message || "Desconocido");
        }
        
        // Borrar mensaje tras unos segundos
        setTimeout(() => msg.style.display = 'none', 5000);
    } catch(e) {
        alert("Error de conexión al guardar: " + e.message);
    } finally {
        btnSetup.innerText = origText1;
        btnWifi.innerText = origText2;
        btnSetup.disabled = false;
        btnWifi.disabled = false;
    }
}

function openModal() {
    document.getElementById('firmware-version').innerText = firmwareVersion;
    document.getElementById('about-modal').style.display = 'block';
}

function closeModal() {
    document.getElementById('about-modal').style.display = 'none';
}

function openResetModal() {
    document.getElementById('reset-modal').style.display = 'block';
}

function closeResetModal() {
    document.getElementById('reset-modal').style.display = 'none';
}

async function executeFactoryReset() {
    closeResetModal();
    const btnSetup = document.querySelector('#tab-setup .btn-primary');
    if(btnSetup) btnSetup.disabled = true;
    try {
        const res = await fetch('/api/setup/factoryreset', { method: 'POST' });
        const msg = document.getElementById('status-msg');
        msg.style.display = 'block';
        if(res.ok) {
            msg.style.backgroundColor = '#d4edda';
            msg.style.color = '#155724';
            msg.innerText = "Configuración borrada. El equipo se está reiniciando...";
        } else {
            msg.style.backgroundColor = '#f8d7da';
            msg.style.color = '#721c24';
            msg.innerText = "Error al borrar la configuración.";
        }
    } catch(e) {
        alert("Error de conexión al resetear: " + e.message);
    }
}

function closeWifiModal() {
    document.getElementById('wifi-scan-modal').style.display = 'none';
}

async function scanWifi() {
    const modal = document.getElementById('wifi-scan-modal');
    const title = document.getElementById('wifi-scan-title');
    const list = document.getElementById('scanned-networks-list');
    
    modal.style.display = 'block';
    title.setAttribute('data-i18n', 'scanningNetworksTitle');
    list.innerHTML = '<div class="spinner"></div><p data-i18n="scanning">Escaneando redes...</p>';
    translatePage();

    try {
        let scanning = true;
        let rawNetworks = [];
        
        while (scanning) {
            const res = await fetch('/api/setup/wifiscan');
            if (res.status === 202) {
                // Sigue escaneando, esperamos 1.5s y reintentamos
                await new Promise(r => setTimeout(r, 1500));
            } else {
                rawNetworks = await res.json();
                scanning = false;
            }
        }
        
        // Remove duplicates, keep best RSSI
        const uniqueNets = {};
        rawNetworks.forEach(net => {
            if (!uniqueNets[net.ssid] || uniqueNets[net.ssid].rssi < net.rssi) {
                uniqueNets[net.ssid] = net;
            }
        });
        const networks = Object.values(uniqueNets);
        
        list.innerHTML = '';
        title.setAttribute('data-i18n', 'scannedNetworks');
        
        if (networks.length === 0) {
            list.innerHTML = '<p data-i18n="noNetworksFound">No se encontraron redes.</p>';
            translatePage();
            return;
        }

        networks.forEach(net => {
            let signal = 1;
            if (net.rssi > -60) signal = 5;
            else if (net.rssi > -70) signal = 4;
            else if (net.rssi > -80) signal = 3;
            else if (net.rssi > -90) signal = 2;

            const div = document.createElement('div');
            div.className = 'network-row';
            div.style.padding = '10px';
            div.style.border = '1px solid var(--border-color)';
            div.style.borderRadius = '4px';
            div.style.backgroundColor = 'var(--bg-color)';
            div.style.color = 'var(--text-color)';
            
            div.innerHTML = `
                <div style="flex: 1; text-align: left;">
                    <strong>${net.ssid}</strong> <span style="font-size: 0.9em; color: #666;">(<span data-i18n="signal">Señal</span>: ${signal}/5)</span>
                </div>
                <button class="btn-primary" style="width: auto; padding: 5px 15px;" onclick="addScannedNetwork('${net.ssid}')" data-i18n="add">Añadir</button>
            `;
            list.appendChild(div);
        });
        translatePage();
    } catch(e) {
        title.setAttribute('data-i18n', 'scannedNetworks');
        list.innerHTML = '<p>Error al escanear redes.</p>';
        translatePage();
    }
}

function addScannedNetwork(ssid) {
    addNetwork(ssid, "");
    closeWifiModal();
}

async function showMoveStatus(msg, isError = false) {
    const el = document.getElementById("move-status-msg");
    el.innerText = msg;
    el.style.color = isError ? "red" : "green";
    setTimeout(() => el.innerText = "", 3000);
}

// Focuser Move functions
async function focuserMoveAbsolute() {
    const pos = parseInt(document.getElementById("f_target_pos").value);
    if(isNaN(pos)) return;
    try {
        const formData = new URLSearchParams();
        formData.append("Position", pos);
        const res = await fetch("/api/v1/focuser/0/move", {
            method: "PUT",
            headers: { "Content-Type": "application/x-www-form-urlencoded" },
            body: formData.toString()
        });
        const data = await res.json();
        if(data.ErrorNumber === 0) showMoveStatus("Moviendo a " + pos);
        else showMoveStatus("Error: " + data.ErrorMessage, true);
    } catch(e) {
        showMoveStatus("Error de conexión", true);
    }
}

async function focuserSyncPosition() {
    const pos = parseInt(document.getElementById("f_target_pos").value);
    if(isNaN(pos)) return;
    try {
        const formData = new URLSearchParams();
        formData.append("Position", pos);
        const res = await fetch("/api/v1/focuser/0/sync", {
            method: "PUT",
            headers: { "Content-Type": "application/x-www-form-urlencoded" },
            body: formData.toString()
        });
        const data = await res.json();
        if(data.ErrorNumber === 0) showMoveStatus("Posición establecida a " + pos);
        else showMoveStatus("Error: " + data.ErrorMessage, true);
    } catch(e) {
        showMoveStatus("Error de conexión", true);
    }
}

async function focuserClearPosition() {
    try {
        const res = await fetch("/api/setup/reset", { method: "POST" });
        if(res.ok) {
            document.getElementById("f_target_pos").value = 0;
            showMoveStatus("Posición puesta a cero internamente");
        } else {
            showMoveStatus("Error poniendo a cero", true);
        }
    } catch(e) {
        showMoveStatus("Error de conexión", true);
    }
}

async function focuserMoveRelative(type, dir) {
    // Current position
    let posRes;
    try {
        posRes = await fetch("/api/v1/focuser/0/position");
    } catch(e) { return showMoveStatus("Error de conexión", true); }
    
    const posData = await posRes.json();
    if(posData.ErrorNumber !== 0) return showMoveStatus("Error: " + posData.ErrorMessage, true);
    
    const currentPos = posData.Value;
    const stepVal = parseInt(document.getElementById(type === "fine" ? "f_fine_step" : "f_coarse_step").value) || 0;
    const target = currentPos + (stepVal * dir);
    
    try {
        const formData = new URLSearchParams();
        formData.append("Position", target);
        const res = await fetch("/api/v1/focuser/0/move", {
            method: "PUT",
            headers: { "Content-Type": "application/x-www-form-urlencoded" },
            body: formData.toString()
        });
        const data = await res.json();
        if(data.ErrorNumber === 0) {
            showMoveStatus("Moviendo a " + target);
            document.getElementById("f_target_pos").value = target;
        } else showMoveStatus("Error: " + data.ErrorMessage, true);
    } catch(e) {
        showMoveStatus("Error de conexión", true);
    }
}

// Rotator Move functions
async function rotatorMoveAbsolute() {
    const angle = parseFloat(document.getElementById("r_target_angle").value);
    if(isNaN(angle)) return;
    try {
        const formData = new URLSearchParams();
        formData.append("Position", angle);
        const res = await fetch("/api/v1/rotator/0/moveabsolute", {
            method: "PUT",
            headers: { "Content-Type": "application/x-www-form-urlencoded" },
            body: formData.toString()
        });
        const data = await res.json();
        if(data.ErrorNumber === 0) showMoveStatus("Moviendo a " + angle + "°");
        else showMoveStatus("Error: " + data.ErrorMessage, true);
    } catch(e) {
        showMoveStatus("Error de conexión", true);
    }
}

async function rotatorMoveRelative(type, dir) {
    let currentPos = 0;
    try {
        const pRes = await fetch("/api/v1/rotator/0/position");
        const pData = await pRes.json();
        if(pData.ErrorNumber === 0) currentPos = pData.Value;
    } catch(e) {}

    const stepVal = parseFloat(document.getElementById(type === "fine" ? "r_fine_step" : "r_coarse_step").value) || 0;
    const amount = stepVal * dir;
    let target = currentPos + amount;
    if(target < 0) target += 360;
    target = target % 360;
    
    try {
        const formData = new URLSearchParams();
        formData.append("Position", amount);
        const res = await fetch("/api/v1/rotator/0/move", {
            method: "PUT",
            headers: { "Content-Type": "application/x-www-form-urlencoded" },
            body: formData.toString()
        });
        const data = await res.json();
        if(data.ErrorNumber === 0) {
            showMoveStatus("Moviendo " + amount + "°");
            document.getElementById("r_target_angle").value = target.toFixed(2);
        } else showMoveStatus("Error: " + data.ErrorMessage, true);
    } catch(e) {
        showMoveStatus("Error de conexión", true);
    }
}

window.onload = loadConfig;
