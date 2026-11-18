/**
 * SmartShell — REST API Client
 */

class SmartShellAPI {
  constructor() {
    this.token = this._loadToken();
    this.baseUrl = window.location.origin;
    this.onUnauthorized = null;
  }

  _loadToken() {
    // Check URL parameters first (e.g. ?token=...)
    const urlParams = new URLSearchParams(window.location.search);
    const urlToken = urlParams.get('token');
    const urlView = urlParams.get('view');
    if (urlView) {
      try { window.sessionStorage.setItem('smartshell_view', urlView); } catch (_) {}
    }
    if (urlToken) {
      localStorage.setItem('smartshell_token', urlToken.trim());
      // Clean query string from URL without reload while preserving hash/view
      const hash = urlView ? `#${urlView}` : (window.location.hash || '');
      window.history.replaceState({}, document.title, window.location.pathname + hash);
      return urlToken.trim();
    }
    return localStorage.getItem('smartshell_token') || '';
  }

  setToken(token) {
    this.token = (token || '').trim();
    if (this.token) {
      localStorage.setItem('smartshell_token', this.token);
    } else {
      localStorage.removeItem('smartshell_token');
    }
  }

  async _request(method, path, body = null, requireAuth = true) {
    const headers = {
      'Accept': 'application/json'
    };

    if (body !== null) {
      headers['Content-Type'] = 'application/json';
    }

    if (requireAuth) {
      if (!this.token) {
        if (this.onUnauthorized) this.onUnauthorized('No token configured');
        throw new Error('Authentication token required');
      }
      headers['Authorization'] = `Bearer ${this.token}`;
    }

    const options = {
      method,
      headers
    };

    if (body !== null) {
      options.body = typeof body === 'string' ? body : JSON.stringify(body);
    }

    const res = await fetch(`${this.baseUrl}${path}`, options);

    if (res.status === 401 && requireAuth) {
      if (this.onUnauthorized) this.onUnauthorized('Invalid or expired token');
      throw new Error('Unauthorized');
    }

    let data;
    const contentType = res.headers.get('content-type') || '';
    if (contentType.includes('application/json')) {
      data = await res.json();
    } else {
      data = await res.text();
    }

    if (!res.ok) {
      const msg = (data && data.error) ? data.error : `HTTP ${res.status}: ${res.statusText}`;
      const err = new Error(msg);
      err.status = res.status;
      err.data = data;
      throw err;
    }

    return data;
  }

  // --- Core Health & Auth ---
  async health() {
    return this._request('GET', '/api/health', null, false);
  }

  async verifyAuth() {
    return this._request('GET', '/api/auth/verify', null, true);
  }

  // --- System Monitoring ---
  async getSysinfo() {
    return this._request('GET', '/api/sysinfo', null, true);
  }

  async getMetrics() {
    return this._request('GET', '/api/metrics', null, true);
  }

  async getProcesses() {
    return this._request('GET', '/api/processes', null, true);
  }

  async getProcess(pid) {
    return this._request('GET', `/api/processes/${pid}`, null, true);
  }

  async getProcessTree() {
    return this._request('GET', '/api/process-tree', null, true);
  }

  // --- CPU Scheduling Lab ---
  async simulateScheduler(algorithm, processes, quantum = 4) {
    const payload = {
      algorithm,
      quantum,
      processes
    };
    return this._request('POST', '/api/scheduler/simulate', payload, true);
  }

  async compareScheduler(processes, quantum = 4) {
    const payload = {
      quantum,
      processes
    };
    return this._request('POST', '/api/scheduler/compare', payload, true);
  }

  // --- Terminal Session Management ---
  async listSessions() {
    return this._request('GET', '/api/terminal', null, true);
  }

  async createSession(cols = 80, rows = 24) {
    return this._request('POST', '/api/terminal', { cols, rows }, true);
  }

  async getSession(sessionId) {
    return this._request('GET', `/api/terminal/${sessionId}`, null, true);
  }

  async deleteSession(sessionId) {
    return this._request('DELETE', `/api/terminal/${sessionId}`, null, true);
  }

  async resizeSession(sessionId, cols, rows) {
    return this._request('POST', `/api/terminal/${sessionId}/resize`, { cols, rows }, true);
  }

  async reconnectSession(sessionId) {
    return this._request('POST', `/api/terminal/${sessionId}/reconnect`, null, true);
  }
}

// Global singleton instance
window.api = new SmartShellAPI();
