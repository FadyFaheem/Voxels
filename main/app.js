// SPA Router and section loader
// api.js is loaded before this script

let currentSection = "setup";
let setupComplete = false;

// Toast notification system
function showToast(title, message, type = "info", duration = 3000) {
  const container = document.getElementById("toastContainer");
  if (!container) return;

  const toast = document.createElement("div");
  toast.className = `toast ${type}`;

  const icons = {
    success: "✓",
    error: "✕",
    warning: "⚠",
    info: "ℹ",
  };

  toast.innerHTML = `
    <span class="toast-icon">${icons[type] || icons.info}</span>
    <div class="toast-content">
      <div class="toast-title">${title}</div>
      ${message ? `<div class="toast-message">${message}</div>` : ""}
    </div>
    <button class="toast-close" aria-label="Close">×</button>
  `;

  const closeBtn = toast.querySelector(".toast-close");
  const closeToast = () => {
    toast.classList.add("hiding");
    setTimeout(() => {
      if (toast.parentNode) {
        toast.parentNode.removeChild(toast);
      }
    }, 300);
  };

  closeBtn.addEventListener("click", closeToast);

  container.appendChild(toast);

  // Auto-remove after duration
  if (duration > 0) {
    setTimeout(closeToast, duration);
  }

  return toast;
}

// Update navigation visibility based on setup status
function updateNavigationVisibility() {
  const setupLink = document.getElementById("nav-setup");
  const widgetsLink = document.getElementById("nav-widgets");
  const settingsLink = document.getElementById("nav-settings");

  if (setupComplete) {
    // Hide setup, show widgets and settings
    if (setupLink) setupLink.style.display = "none";
    if (widgetsLink) widgetsLink.style.display = "block";
    if (settingsLink) settingsLink.style.display = "block";

    // If currently on setup, redirect to widgets
    if (currentSection === "setup") {
      window.location.hash = "widgets";
      route();
    }
  } else {
    // Show setup, hide widgets and settings
    if (setupLink) setupLink.style.display = "block";
    if (widgetsLink) widgetsLink.style.display = "none";
    if (settingsLink) settingsLink.style.display = "none";

    // If trying to access widgets/settings, redirect to setup
    if (currentSection === "widgets" || currentSection === "settings") {
      window.location.hash = "setup";
      route();
    }
  }
}

// Load section HTML
async function loadSection(sectionName) {
  try {
    const resp = await fetch(`/sections/${sectionName}.html`);
    if (!resp.ok) {
      throw new Error(`Failed to load section: ${resp.status}`);
    }
    const html = await resp.text();
    document.getElementById("content").innerHTML = html;

    // Initialize section-specific code
    if (sectionName === "setup") {
      initSetupSection();
    } else if (sectionName === "widgets") {
      initWidgetsSection();
    } else if (sectionName === "settings") {
      initSettingsSection();
    }
  } catch (err) {
    console.error("Error loading section:", err);
    document.getElementById(
      "content"
    ).innerHTML = `<div class="container"><h1>Error</h1><p>Failed to load ${sectionName} section.</p></div>`;
  }
}

// Router
function route() {
  let hash = window.location.hash.slice(1) || "setup";

  // Validate section access based on setup status
  if (!setupComplete) {
    // Only allow setup during initial setup
    if (hash !== "setup") {
      hash = "setup";
      window.location.hash = "setup";
    }
  } else {
    // After setup, don't allow access to setup
    if (hash === "setup") {
      hash = "widgets";
      window.location.hash = "widgets";
    }
  }

  currentSection = hash;

  // Update nav links (only visible ones)
  document.querySelectorAll(".nav-link").forEach((link) => {
    if (link.style.display !== "none") {
      link.classList.remove("active");
      if (link.dataset.section === hash) {
        link.classList.add("active");
      }
    }
  });

  // Load section
  loadSection(hash);
}

// Mobile menu toggle
function initMobileMenu() {
  const menuToggle = document.getElementById("menuToggle");
  const sidebar = document.getElementById("sidebar");
  const overlay = document.getElementById("sidebarOverlay");

  function toggleMenu() {
    sidebar.classList.toggle("open");
    overlay.classList.toggle("open");
    menuToggle.classList.toggle("active");
  }

  function closeMenu() {
    sidebar.classList.remove("open");
    overlay.classList.remove("open");
    menuToggle.classList.remove("active");
  }

  if (menuToggle) {
    menuToggle.addEventListener("click", toggleMenu);
  }

  if (overlay) {
    overlay.addEventListener("click", closeMenu);
  }

  // Close menu when clicking a nav link on mobile
  document.querySelectorAll(".nav-link").forEach((link) => {
    link.addEventListener("click", () => {
      if (window.innerWidth < 768) {
        closeMenu();
      }
    });
  });
}

// Check setup status and initialize
async function checkSetupStatus() {
  try {
    const status = await api.getStatus();
    setupComplete = status.setup_complete || false;
    updateNavigationVisibility();
    route();
  } catch (err) {
    console.error("Error checking setup status:", err);
    // Default to setup mode if we can't check
    setupComplete = false;
    updateNavigationVisibility();
    route();
  }
}

// Initialize on load
window.addEventListener("DOMContentLoaded", () => {
  initMobileMenu();
  checkSetupStatus();
});

// Handle hash changes
window.addEventListener("hashchange", route);

// Setup section initialization
function initSetupSection() {
  let selectedNetwork = null;

  window.showResetConfirm = function () {
    document.getElementById("resetModal").classList.remove("hidden");
  };

  window.hideResetConfirm = function () {
    document.getElementById("resetModal").classList.add("hidden");
  };

  window.performFactoryReset = async function () {
    const modal = document.getElementById("resetModal");
    modal.querySelector(".btn-confirm-danger").disabled = true;
    modal.querySelector(".btn-confirm-danger").textContent = "Resetting...";
    modal.querySelector(".btn-cancel").disabled = true;

    try {
      await api.factoryReset();
      modal.querySelector(".modal p").textContent =
        "Device is resetting... Please reconnect to the device's WiFi network.";
      modal.querySelector(".modal-buttons").classList.add("hidden");
    } catch (err) {
      modal.querySelector(".modal p").textContent =
        "Reset failed. Please try again.";
      modal.querySelector(".btn-confirm-danger").disabled = false;
      modal.querySelector(".btn-confirm-danger").textContent = "Reset";
      modal.querySelector(".btn-cancel").disabled = false;
    }
  };

  function getSignalClass(rssi) {
    if (rssi >= -50) return "signal-strong";
    if (rssi >= -70) return "signal-medium";
    return "signal-weak";
  }

  function getSignalBars(rssi) {
    if (rssi >= -50) return "▂▄▆█";
    if (rssi >= -60) return "▂▄▆░";
    if (rssi >= -70) return "▂▄░░";
    return "▂░░░";
  }

  window.scanNetworks = async function () {
    const btn = document.getElementById("scanBtn");
    const list = document.getElementById("networkList");
    const ssidInput = document.getElementById("ssidInputGroup");

    btn.disabled = true;
    btn.innerHTML = '<span class="spinner"></span>Scanning...';
    list.classList.remove("hidden");
    list.innerHTML =
      '<div style="text-align: center; padding: 20px; color: #888;">Scanning for networks...</div>';

    try {
      const networks = await api.scanNetworks();

      if (networks.length === 0) {
        list.innerHTML =
          '<div style="text-align: center; padding: 20px; color: #888;">No networks found</div>';
      } else {
        networks.sort((a, b) => b.rssi - a.rssi);

        const seen = new Set();
        const unique = networks.filter((n) => {
          if (seen.has(n.ssid)) return false;
          seen.add(n.ssid);
          return true;
        });

        list.innerHTML =
          unique
            .map(
              (n) => `
            <div class="network-item" onclick="selectNetwork('${n.ssid.replace(
              /'/g,
              "\\'"
            )}')">
              <span class="network-name">${n.ssid}</span>
              <span class="network-signal ${getSignalClass(
                n.rssi
              )}">${getSignalBars(n.rssi)}</span>
            </div>
          `
            )
            .join("") +
          `
            <div class="network-item other" onclick="selectOther()">
              <span class="network-name">Other (Hidden Network)...</span>
            </div>
          `;
      }

      ssidInput.classList.add("hidden");
    } catch (err) {
      list.innerHTML =
        '<div style="text-align: center; padding: 20px; color: #f44336;">Failed to scan networks</div>';
    }

    btn.disabled = false;
    btn.textContent = "Scan for Networks";
  };

  window.selectNetwork = function (ssid) {
    selectedNetwork = ssid;
    document.getElementById("wifiSsid").value = ssid;

    document
      .querySelectorAll(".network-item")
      .forEach((el) => el.classList.remove("selected"));
    event.currentTarget.classList.add("selected");

    document.getElementById("ssidInputGroup").classList.add("hidden");
    document.getElementById("wifiPass").focus();
  };

  window.selectOther = function () {
    selectedNetwork = null;
    document.getElementById("wifiSsid").value = "";

    document
      .querySelectorAll(".network-item")
      .forEach((el) => el.classList.remove("selected"));
    event.currentTarget.classList.add("selected");

    document.getElementById("ssidInputGroup").classList.remove("hidden");
    document.getElementById("wifiSsid").focus();
  };

  window.showSetupScreen = function () {
    document.getElementById("setupScreen").classList.remove("hidden");
    document.getElementById("successScreen").classList.add("hidden");
  };

  window.showSuccessScreen = function (deviceName, wifiSsid) {
    document.getElementById("setupScreen").classList.add("hidden");
    document.getElementById("successScreen").classList.remove("hidden");

    document.getElementById("savedDeviceName").textContent =
      deviceName || "Voxels Device";
    document.getElementById("savedWifiSsid").textContent = wifiSsid || "-";
    document.getElementById("targetNetwork").textContent =
      wifiSsid || "your WiFi network";

    checkConnectionStatus();
  };

  async function checkConnectionStatus() {
    const connectingStatus = document.getElementById("connectingStatus");
    const ipDisplay = document.getElementById("ipDisplay");
    const networkIp = document.getElementById("networkIp");

    let attempts = 0;
    const maxAttempts = 30;

    const checkInterval = setInterval(async () => {
      attempts++;

      try {
        const status = await api.getStatus();

        // Update setup status if it changed
        if (status.setup_complete && !setupComplete) {
          setupComplete = true;
          updateNavigationVisibility();
        }

        if (status.sta_connected && status.sta_ip) {
          clearInterval(checkInterval);
          connectingStatus.classList.add("hidden");
          ipDisplay.classList.remove("hidden");
          networkIp.textContent = status.sta_ip;
        } else if (attempts >= maxAttempts) {
          clearInterval(checkInterval);
          connectingStatus.innerHTML =
            '<span style="color: #ff9800;">⚠️ Could not connect to WiFi. Check password and try again.</span>';
        }
      } catch (err) {
        if (attempts >= maxAttempts) {
          clearInterval(checkInterval);
          connectingStatus.innerHTML =
            '<span style="color: #f44336;">Connection check failed</span>';
        }
      }
    }, 1000);
  }

  // Setup form handler
  const setupForm = document.getElementById("setupForm");
  if (setupForm) {
    setupForm.addEventListener("submit", async (e) => {
      e.preventDefault();

      const btn = document.getElementById("saveBtn");
      const msg = document.getElementById("message");

      btn.disabled = true;
      btn.textContent = "Saving...";
      msg.className = "message";
      msg.style.display = "none";

      const deviceName = document.getElementById("deviceName").value;
      const wifiSsid = document.getElementById("wifiSsid").value;
      const wifiPass = document.getElementById("wifiPass").value;

      try {
        await api.saveConfig({
          device_name: deviceName,
          wifi_ssid: wifiSsid,
          wifi_pass: wifiPass,
        });

        showSuccessScreen(deviceName, wifiSsid);
        showToast(
          "Setup Complete",
          "Device configuration saved successfully!",
          "success",
          5000
        );

        // Check if setup is now complete and update navigation
        setTimeout(async () => {
          try {
            const status = await api.getStatus();
            if (status.setup_complete) {
              setupComplete = true;
              updateNavigationVisibility();
            }
          } catch (err) {
            console.error("Error checking setup status after save:", err);
          }
        }, 1000);
      } catch (err) {
        msg.textContent = "Failed to save settings. Please try again.";
        msg.className = "message error";
        showToast(
          "Error",
          "Failed to save settings. Please try again.",
          "error"
        );
        btn.disabled = false;
        btn.textContent = "Save & Connect";
        console.error("Error saving setup:", err);
      }
    });
  }
}

// Widgets section initialization
async function initWidgetsSection() {
  try {
    const widgets = await api.getWidgets();
    const activeWidget = await api.getActiveWidget();

    // Render widget grid
    const grid = document.getElementById("widgetGrid");
    if (grid) {
      grid.innerHTML = widgets
        .map(
          (w) => `
        <div class="widget-card ${w.active ? "active" : ""}" data-id="${w.id}">
          <div class="widget-icon">${w.icon || "📦"}</div>
          <div class="widget-name">${w.name}</div>
        </div>
      `
        )
        .join("");

      // Add click handlers
      grid.querySelectorAll(".widget-card").forEach((card) => {
        card.addEventListener("click", async () => {
          const widgetId = card.dataset.id;
          await api.setActiveWidget(widgetId);
          location.reload(); // Reload to show updated active state
        });
      });
    }

    // Load config for active widget
    if (activeWidget && activeWidget.widget_id) {
      loadWidgetConfig(activeWidget.widget_id);
    }
  } catch (err) {
    console.error("Error loading widgets:", err);
  }
}

async function loadWidgetConfig(widgetId) {
  try {
    const config = await api.getWidgetConfig(widgetId);
    const configPanel = document.getElementById("widgetConfig");

    if (widgetId === "clock") {
      renderClockConfig(config, configPanel);
    } else if (widgetId === "timer") {
      renderTimerConfig(config, configPanel);
    } else if (widgetId === "weather") {
      renderWeatherConfig(config, configPanel);
    }
  } catch (err) {
    console.error("Error loading widget config:", err);
  }
}

function renderClockConfig(config, panel) {
  if (!panel) return;

  panel.innerHTML = `
    <h2>Clock Settings</h2>
    <div class="config-group">
      <label>Display Mode</label>
      <div class="toggle-group">
        <button class="${
          config.mode === "digital" ? "active" : ""
        }" data-value="digital">Digital</button>
        <button class="${
          config.mode === "analog" ? "active" : ""
        }" data-value="analog">Analog</button>
      </div>
    </div>
    <div class="config-group">
      <label><input type="checkbox" id="showSeconds" ${
        config.show_seconds ? "checked" : ""
      }> Show Seconds</label>
    </div>
    <div class="config-group">
      <label>Hour Format</label>
      <div class="toggle-group">
        <button class="${
          !config.is_24h ? "active" : ""
        }" data-value="12h">12 Hour</button>
        <button class="${
          config.is_24h ? "active" : ""
        }" data-value="24h">24 Hour</button>
      </div>
    </div>
    <button class="btn" onclick="saveClockConfig()">Apply</button>
  `;

  // Add toggle handlers
  panel.querySelectorAll(".toggle-group button").forEach((btn) => {
    btn.addEventListener("click", () => {
      btn.parentElement
        .querySelectorAll("button")
        .forEach((b) => b.classList.remove("active"));
      btn.classList.add("active");
    });
  });

  window.saveClockConfig = async function () {
    const mode =
      panel.querySelector(".toggle-group button.active")?.dataset.value ===
      "analog"
        ? "analog"
        : "digital";
    const showSeconds = document.getElementById("showSeconds").checked;
    const is24h =
      panel.querySelectorAll(".toggle-group")[1]?.querySelector(".active")
        ?.dataset.value === "24h";

    try {
      await api.setWidgetConfig("clock", {
        mode: mode,
        show_seconds: showSeconds,
        is_24h: is24h,
      });
      showToast(
        "Settings Saved",
        "Clock settings saved successfully!",
        "success"
      );
    } catch (err) {
      showToast("Error", "Failed to save clock settings", "error");
    }
  };
}

function renderTimerConfig(config, panel) {
  if (!panel) return;

  panel.innerHTML = `
    <h2>Timer Settings</h2>
    <div class="config-group">
      <label>Mode</label>
      <div class="toggle-group">
        <button class="${
          config.mode === "countdown" ? "active" : ""
        }" data-value="countdown">Countdown</button>
        <button class="${
          config.mode === "stopwatch" ? "active" : ""
        }" data-value="stopwatch">Stopwatch</button>
      </div>
    </div>
    <button class="btn" onclick="saveTimerConfig()">Apply</button>
  `;

  // Add toggle handlers
  panel.querySelectorAll(".toggle-group button").forEach((btn) => {
    btn.addEventListener("click", () => {
      btn.parentElement
        .querySelectorAll("button")
        .forEach((b) => b.classList.remove("active"));
      btn.classList.add("active");
    });
  });

  window.saveTimerConfig = async function () {
    const mode =
      panel.querySelector(".toggle-group button.active")?.dataset.value ||
      "countdown";
    try {
      await api.setWidgetConfig("timer", { mode: mode });
      showToast(
        "Settings Saved",
        "Timer settings saved successfully!",
        "success"
      );
    } catch (err) {
      showToast("Error", "Failed to save timer settings", "error");
    }
  };
}

function renderWeatherConfig(config, panel) {
  if (!panel) return;

  // Load current weather settings
  (async () => {
    try {
      const zipData = await api.getWeatherZipCode();
      const tempUnitData = await api.getWeatherTempUnit();

      const zipCode = zipData.zip_code || "";
      const tempUnit = tempUnitData.temp_unit || "celsius";

      panel.innerHTML = `
        <h2>Weather Settings</h2>
        <div class="config-group">
          <label for="weatherZipCode">Zip Code</label>
          <input
            type="text"
            id="weatherZipCode"
            name="weather_zip_code"
            placeholder="90210"
            maxlength="10"
            value="${zipCode}"
          />
          <p style="color: #888; font-size: 0.8rem; margin-top: 5px;">
            Enter your zip code or postal code for weather data.
          </p>
        </div>

        <div class="config-group">
          <label for="weatherTempUnit">Temperature Unit</label>
          <select id="weatherTempUnit" name="weather_temp_unit">
            <option value="celsius" ${
              tempUnit === "celsius" ? "selected" : ""
            }>Celsius (°C)</option>
            <option value="fahrenheit" ${
              tempUnit === "fahrenheit" ? "selected" : ""
            }>Fahrenheit (°F)</option>
          </select>
          <p style="color: #888; font-size: 0.8rem; margin-top: 5px;">
            Select the temperature unit for weather display.
          </p>
        </div>
        <button class="btn" onclick="saveWeatherConfig()">Apply</button>
      `;
    } catch (err) {
      console.error("Error loading weather config:", err);
      panel.innerHTML = `
        <h2>Weather Settings</h2>
        <div class="config-group">
          <label for="weatherZipCode">Zip Code</label>
          <input
            type="text"
            id="weatherZipCode"
            name="weather_zip_code"
            placeholder="90210"
            maxlength="10"
          />
          <p style="color: #888; font-size: 0.8rem; margin-top: 5px;">
            Enter your zip code or postal code for weather data.
          </p>
        </div>

        <div class="config-group">
          <label for="weatherTempUnit">Temperature Unit</label>
          <select id="weatherTempUnit" name="weather_temp_unit">
            <option value="celsius" selected>Celsius (°C)</option>
            <option value="fahrenheit">Fahrenheit (°F)</option>
          </select>
          <p style="color: #888; font-size: 0.8rem; margin-top: 5px;">
            Select the temperature unit for weather display.
          </p>
        </div>
        <button class="btn" onclick="saveWeatherConfig()">Apply</button>
      `;
    }
  })();

  window.saveWeatherConfig = async function () {
    const zipCode = document.getElementById("weatherZipCode").value.trim();
    const tempUnit = document.getElementById("weatherTempUnit").value;

    try {
      if (zipCode) {
        await api.setWeatherZipCode(zipCode);
      }
      await api.setWeatherTempUnit(tempUnit);
      showToast(
        "Settings Saved",
        "Weather settings saved successfully!",
        "success"
      );
    } catch (err) {
      showToast("Error", "Failed to save weather settings", "error");
      console.error("Error saving weather config:", err);
    }
  };
}

// Settings section initialization
async function initSettingsSection() {
  try {
    // Load current device config (device name, WiFi SSID)
    const configData = await api.getConfig();
    const deviceNameInput = document.getElementById("deviceName");
    const wifiSsidInput = document.getElementById("wifiSsid");
    if (deviceNameInput && configData.device_name) {
      deviceNameInput.value = configData.device_name;
    }
    if (wifiSsidInput && configData.wifi_ssid) {
      wifiSsidInput.value = configData.wifi_ssid;
    }

    // Load current timezone
    const tzData = await api.getTimezone();
    const timezoneSelect = document.getElementById("timezone");
    if (timezoneSelect && tzData.timezone) {
      timezoneSelect.value = tzData.timezone;
    }

    // Load current font size
    const fontSizeData = await api.getFontSize();
    const fontSizeSelect = document.getElementById("fontSize");
    if (fontSizeSelect && fontSizeData.font_size !== undefined) {
      fontSizeSelect.value = fontSizeData.font_size.toString();
    }

    // Setup form handler
    const settingsForm = document.getElementById("settingsForm");
    if (settingsForm) {
      settingsForm.addEventListener("submit", async (e) => {
        e.preventDefault();

        const btn = document.getElementById("saveSettingsBtn");
        const msg = document.getElementById("settingsMessage");

        btn.disabled = true;
        btn.textContent = "Saving...";
        msg.className = "message";
        msg.style.display = "none";

        const deviceName = document.getElementById("deviceName").value.trim();
        const wifiSsid = document.getElementById("wifiSsid").value.trim();
        const wifiPass = document.getElementById("wifiPass").value;
        const timezone = document.getElementById("timezone").value;
        const fontSize = parseInt(document.getElementById("fontSize").value);

        try {
          // Save device name and WiFi settings if provided
          const configData = {};
          if (deviceName) {
            configData.device_name = deviceName;
          }
          if (wifiSsid) {
            configData.wifi_ssid = wifiSsid;
          }
          if (wifiPass) {
            configData.wifi_pass = wifiPass;
          }
          if (Object.keys(configData).length > 0) {
            await api.saveConfig(configData);
          }

          // Save timezone and font size
          await api.setTimezone(timezone);
          await api.setFontSize(fontSize);

          msg.textContent = "Settings saved successfully!";
          msg.className = "message success";
          msg.style.display = "block";
          showToast(
            "Settings Saved",
            "All settings saved successfully!",
            "success"
          );

          // Clear WiFi password field after successful save
          if (wifiPass) {
            document.getElementById("wifiPass").value = "";
          }
        } catch (err) {
          msg.textContent = "Failed to save settings. Please try again.";
          msg.className = "message error";
          msg.style.display = "block";
          showToast(
            "Error",
            "Failed to save settings. Please try again.",
            "error"
          );
          console.error("Error saving settings:", err);
        }

        btn.disabled = false;
        btn.textContent = "Save Settings";
      });
    }

    // Setup factory reset handler
    const factoryResetBtn = document.getElementById("factoryResetBtn");
    if (factoryResetBtn) {
      factoryResetBtn.addEventListener("click", () => {
        if (
          confirm(
            "Are you sure you want to factory reset? This will erase all settings and restart the device. This action cannot be undone."
          )
        ) {
          if (
            confirm(
              "This will delete all your settings and restart the device. Are you absolutely sure?"
            )
          ) {
            factoryResetBtn.disabled = true;
            factoryResetBtn.textContent = "Resetting...";
            api
              .factoryReset()
              .then(() => {
                showToast(
                  "Factory Reset",
                  "Device will restart shortly.",
                  "info",
                  5000
                );
              })
              .catch((err) => {
                showToast(
                  "Error",
                  "Failed to perform factory reset. Please try again.",
                  "error"
                );
                factoryResetBtn.disabled = false;
                factoryResetBtn.textContent = "Factory Reset";
                console.error("Factory reset error:", err);
              });
          }
        }
      });
    }
  } catch (err) {
    console.error("Error loading settings:", err);
  }
}
