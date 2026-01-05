// API client functions
const api = {
  async get(endpoint) {
    const resp = await fetch(endpoint);
    if (!resp.ok) {
      throw new Error(`API error: ${resp.status}`);
    }
    return resp.json();
  },

  async post(endpoint, data) {
    const resp = await fetch(endpoint, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(data),
    });
    if (!resp.ok) {
      throw new Error(`API error: ${resp.status}`);
    }
    return resp.json();
  },

  // Config API
  async getConfig() {
    return this.get("/api/config");
  },

  async saveConfig(data) {
    return this.post("/api/config", data);
  },

  async getStatus() {
    return this.get("/api/status");
  },

  async scanNetworks() {
    return this.get("/api/scan");
  },

  async factoryReset() {
    return this.post("/api/reset", {});
  },

  // Widget API
  async getWidgets() {
    return this.get("/api/widgets");
  },

  async getActiveWidget() {
    return this.get("/api/widgets/active");
  },

  async setActiveWidget(widgetId) {
    return this.post("/api/widgets/active", { widget_id: widgetId });
  },

  async getWidgetConfig(widgetId) {
    return this.get(`/api/widgets/${widgetId}/config`);
  },

  async setWidgetConfig(widgetId, config) {
    return this.post(`/api/widgets/${widgetId}/config`, config);
  },

  // Timezone API
  async getTimezone() {
    return this.get("/api/timezone");
  },

  async setTimezone(timezone) {
    return this.post("/api/timezone", { timezone: timezone });
  },

  // Font size API
  async getFontSize() {
    return this.get("/api/font-size");
  },

  async setFontSize(fontSize) {
    return this.post("/api/font-size", { font_size: fontSize });
  },

  // Weather API
  async getWeatherZipCode() {
    return this.get("/api/weather/zip-code");
  },

  async setWeatherZipCode(zipCode) {
    return this.post("/api/weather/zip-code", { zip_code: zipCode });
  },

  async getWeatherData() {
    return this.get("/api/weather/data");
  },
};
