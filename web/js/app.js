/**
 * SmartShell 3.0 — Application Coordinator
 * Connects the dark-mode dashboard UI with the CivetWeb backend APIs,
 * terminal session lifecycle, process monitor, and CPU scheduling lab.
 */

document.addEventListener('DOMContentLoaded', () => {
  const app = {
    activeView: 'terminal',
    sessions: new Map(), // sessionId -> { meta, tabEl, wrapperEl, terminal }
    currentSessionId: null,
    metricsTimer: null,
    processTimer: null,
    processCache: [],
    selectedAlgorithm: 'fcfs',

    init() {
      this._bindNavigation();
      this._bindTerminalControls();
      this._bindProcessControls();
      this._bindSchedulingLab();
      this._bindSettings();
      this._bindModalControls();
      this._bindWindowResize();
      this._checkInitialAuth();
    },

    // ========================================================
    // NAVIGATION & VIEW ROUTING
    // ========================================================
    _bindNavigation() {
      const navItems = document.querySelectorAll('.nav-item');
      navItems.forEach((item) => {
        item.addEventListener('click', (e) => {
          e.preventDefault();
          const target = item.dataset.view;
          if (target) {
            this.switchView(target, true);
          }
        });
      });

      window.addEventListener('popstate', (e) => {
        const hashView = (window.location.hash || '').replace('#', '');
        const target = hashView || (e.state && e.state.view) || 'terminal';
        if (['terminal', 'dashboard', 'processes', 'scheduling', 'system', 'settings'].includes(target)) {
          this.switchView(target, false);
        }
      });
    },

    switchView(viewName, pushHistory = false) {
      this.activeView = viewName;
      if (window.history) {
        if (pushHistory && window.location.hash !== `#${viewName}`) {
          window.history.pushState({ view: viewName }, '', `#${viewName}`);
        } else if (window.history.replaceState) {
          window.history.replaceState({ view: viewName }, '', `#${viewName}`);
        }
      }

      // Update sidebar nav active state
      document.querySelectorAll('.nav-item').forEach((item) => {
        item.classList.toggle('active', item.dataset.view === viewName);
      });

      // Update main panels active state
      document.querySelectorAll('.view-panel').forEach((panel) => {
        panel.classList.toggle('active', panel.id === `view-${viewName}`);
      });

      // Update topbar title
      const titles = {
        terminal: 'Terminal Console',
        dashboard: 'System Dashboard',
        processes: 'Process Explorer',
        scheduling: 'CPU Scheduling Lab',
        system: 'System Explorer',
        settings: 'Settings & Auth'
      };
      const titleEl = document.getElementById('topbar-view-title');
      if (titleEl) {
        titleEl.textContent = titles[viewName] || 'SmartShell Console';
      }

      // Background polling control
      if (viewName === 'dashboard') {
        this.fetchDashboardData();
        this._startMetricsPolling();
      } else {
        this._stopMetricsPolling();
      }

      if (viewName === 'processes') {
        this.fetchProcesses();
        this._startProcessPolling();
      } else {
        this._stopProcessPolling();
      }

      if (viewName === 'system') {
        this.fetchSystemData();
      }

      if (viewName === 'scheduling') {
        if (!this._hasRunInitialSim) {
          this._hasRunInitialSim = true;
          setTimeout(() => this.runSimulation(), 60);
        }
      }

      if (viewName === 'terminal') {
        const activeTerm = this.getActiveTerminal();
        if (activeTerm) {
          setTimeout(() => {
            activeTerm.fit();
            activeTerm.focus();
          }, 40);
        }
      }
    },

    // ========================================================
    // STATUS BADGE & TOAST NOTIFICATIONS
    // ========================================================
    setConnectionStatus(status, label) {
      const badge = document.getElementById('sidebar-status-badge');
      const text = document.getElementById('sidebar-status-text');
      if (badge) {
        badge.className = `status-badge ${status}`;
      }
      if (text) {
        text.textContent = label;
      }
    },

    showToast(message, type = 'info') {
      const container = document.getElementById('toast-container');
      if (!container) return;

      const toast = document.createElement('div');
      toast.className = `toast ${type}`;
      toast.textContent = message;

      container.appendChild(toast);
      setTimeout(() => {
        toast.style.opacity = '0';
        toast.style.transform = 'translateY(10px)';
        setTimeout(() => toast.remove(), 300);
      }, 3500);
    },

    async _checkInitialAuth() {
      // Wire up unauthorized callback
      window.api.onUnauthorized = (msg) => {
        this.setConnectionStatus('disconnected', 'Unauthorized');
        this.showToast(msg || 'Authentication token required', 'error');
        this.switchView('settings');
      };

      try {
        const health = await window.api.health();
        if (health && health.status === 'ok') {
          this.setConnectionStatus('connecting', 'Server Online');
        }

        if (window.api.token) {
          const auth = await window.api.verifyAuth();
          if (auth && auth.authenticated) {
            this.setConnectionStatus('connected', 'Authenticated');
            await this.syncTerminalSessions();
            let targetView = (window.location.hash || '').replace('#', '');
            if (!targetView) {
              try { targetView = window.sessionStorage.getItem('smartshell_view'); } catch (_) {}
            }
            if (targetView && ['terminal', 'dashboard', 'processes', 'scheduling', 'system', 'settings'].includes(targetView)) {
              this.switchView(targetView);
            }
          } else {
            this.setConnectionStatus('disconnected', 'Invalid Token');
            this.switchView('settings');
          }
        } else {
          this.setConnectionStatus('disconnected', 'No Auth Token');
          this.showToast('Please enter your access token in Settings', 'warn');
          this.switchView('settings');
        }
      } catch (err) {
        this.setConnectionStatus('disconnected', 'Offline / Unreachable');
      }
    },

    // ========================================================
    // TERMINAL SESSION MANAGEMENT
    // ========================================================
    _bindTerminalControls() {
      const newBtn = document.getElementById('btn-new-terminal');
      if (newBtn) {
        newBtn.addEventListener('click', () => this.createNewSession());
      }

      const clearBtn = document.getElementById('btn-clear-term');
      if (clearBtn) {
        clearBtn.addEventListener('click', () => {
          const active = this.getActiveTerminal();
          if (active) {
            active.clear();
            active.focus();
          }
        });
      }
    },

    _bindWindowResize() {
      window.addEventListener('resize', () => {
        if (this.activeView === 'terminal') {
          const active = this.getActiveTerminal();
          if (active) active.fit();
        }
      });
    },

    _updateSessionCount() {
      const count = this.sessions.size;
      const countEl = document.getElementById('topbar-session-count');
      if (countEl) {
        countEl.textContent = `${count} session${count === 1 ? '' : 's'} active`;
      }
    },

    async syncTerminalSessions() {
      try {
        const res = await window.api.listSessions();
        const list = res.sessions || [];
        const activeList = list.filter((s) => s.status !== 'terminated');

        if (activeList.length === 0) {
          // Auto-spawn initial terminal session
          await this.createNewSession();
        } else {
          for (const s of activeList) {
            if (!this.sessions.has(s.session_id)) {
              this._addSessionTab(s.session_id, s.status, null);
            }
          }
          if (!this.currentSessionId && activeList.length > 0) {
            this.activateSession(activeList[0].session_id);
          }
        }
        this._updateSessionCount();
      } catch (err) {
        console.warn('Failed to sync terminal sessions:', err);
      }
    },

    async createNewSession() {
      try {
        const res = await window.api.createSession(80, 24);
        if (res && res.session_id) {
          this._addSessionTab(res.session_id, 'active', res.ws_ticket);
          this.activateSession(res.session_id);
          this._updateSessionCount();
          this.showToast('Terminal session spawned', 'success');
        }
      } catch (err) {
        this.showToast(`Failed to spawn terminal: ${err.message}`, 'error');
      }
    },

    _addSessionTab(sessionId, status = 'active', initialTicket = null) {
      const tabsContainer = document.getElementById('session-tabs-container');
      if (!tabsContainer) return;

      const shortId = sessionId.substring(0, 8);
      const tabEl = document.createElement('div');
      tabEl.className = 'term-tab';
      tabEl.id = `tab-${sessionId}`;
      tabEl.dataset.sessionId = sessionId;

      tabEl.innerHTML = `
        <span class="tab-dot"></span>
        <span>tty:${shortId}</span>
        <button class="tab-close" title="Close Session">&times;</button>
      `;

      tabEl.addEventListener('click', (e) => {
        if (!e.target.classList.contains('tab-close')) {
          this.activateSession(sessionId);
        }
      });

      const closeBtn = tabEl.querySelector('.tab-close');
      closeBtn.addEventListener('click', (e) => {
        e.stopPropagation();
        this.closeSession(sessionId);
      });

      tabsContainer.appendChild(tabEl);

      // Terminal screen container
      const termStage = document.getElementById('terminal-stage');
      const termWrapper = document.createElement('div');
      termWrapper.id = `term-wrap-${sessionId}`;
      termWrapper.style.width = '100%';
      termWrapper.style.height = '100%';
      termWrapper.style.display = 'none';
      termStage.appendChild(termWrapper);

      // Terminal instance
      const terminal = new SmartTerminal(termWrapper, {
        sessionId,
        ticket: initialTicket,
        onExit: () => {
          const dot = tabEl.querySelector('.tab-dot');
          if (dot) dot.style.backgroundColor = 'var(--text-faint)';
        },
        onStatusChange: (s) => {
          const dot = tabEl.querySelector('.tab-dot');
          if (dot) {
            dot.style.backgroundColor =
              s === 'connected' ? 'var(--accent-emerald)' :
              s === 'connecting' ? 'var(--accent-amber)' : 'var(--accent-rose)';
          }
        }
      });

      this.sessions.set(sessionId, {
        tabEl,
        wrapperEl: termWrapper,
        terminal
      });

      // Connect WebSocket with initial ticket or request a reconnect ticket
      if (initialTicket) {
        terminal.connect(initialTicket);
      } else {
        window.api.reconnectSession(sessionId).then((r) => {
          if (r && r.ws_ticket) {
            terminal.connect(r.ws_ticket);
          }
        }).catch((e) => console.warn('Reconnect error:', e));
      }
    },

    activateSession(sessionId) {
      if (!this.sessions.has(sessionId)) return;

      this.currentSessionId = sessionId;

      // Update tab selection styles
      document.querySelectorAll('.term-tab').forEach((t) => {
        t.classList.toggle('active', t.dataset.sessionId === sessionId);
      });

      // Toggle terminal wrapper visibility
      this.sessions.forEach((s, id) => {
        s.wrapperEl.style.display = (id === sessionId) ? 'block' : 'none';
      });

      // Fit and focus
      const active = this.getActiveTerminal();
      if (active) {
        setTimeout(() => {
          active.fit();
          active.focus();
        }, 30);
      }
    },

    async closeSession(sessionId) {
      if (!this.sessions.has(sessionId)) return;

      const s = this.sessions.get(sessionId);
      s.terminal.destroy();
      s.tabEl.remove();
      s.wrapperEl.remove();
      this.sessions.delete(sessionId);
      this._updateSessionCount();

      try {
        await window.api.deleteSession(sessionId);
      } catch (e) {
        console.warn('Session deletion error:', e);
      }

      if (this.currentSessionId === sessionId) {
        const remaining = Array.from(this.sessions.keys());
        if (remaining.length > 0) {
          this.activateSession(remaining[0]);
        } else {
          this.currentSessionId = null;
        }
      }
    },

    getActiveTerminal() {
      if (this.currentSessionId && this.sessions.has(this.currentSessionId)) {
        return this.sessions.get(this.currentSessionId).terminal;
      }
      return null;
    },

    // ========================================================
    // SYSTEM DASHBOARD & REAL-TIME METRICS
    // ========================================================
    _startMetricsPolling() {
      this._stopMetricsPolling();
      this.metricsTimer = setInterval(() => this.fetchDashboardData(), 2500);
    },

    _stopMetricsPolling() {
      if (this.metricsTimer) {
        clearInterval(this.metricsTimer);
        this.metricsTimer = null;
      }
    },

    async fetchDashboardData() {
      try {
        const [sysinfo, metrics] = await Promise.all([
          window.api.getSysinfo().catch(() => null),
          window.api.getMetrics().catch(() => null)
        ]);

        if (sysinfo) {
          this._setElText('dash-hostname', sysinfo.os_name || 'Linux');
          this._setElText('dash-os', sysinfo.os || `${sysinfo.os_name} ${sysinfo.kernel_release}`);
          this._setElText('dash-uptime', `${sysinfo.architecture || 'x86_64'} • ${sysinfo.cpus_online || 1} Cores`);

          this._setElText('dash-detail-os', sysinfo.os_name || '-');
          this._setElText('dash-detail-kernel', sysinfo.kernel_release || '-');
          this._setElText('dash-detail-arch', sysinfo.architecture || '-');
          this._setElText('dash-detail-cores', `${sysinfo.cpus_online || 1} online / ${sysinfo.cpus_configured || 1} configured`);
          this._setElText('dash-load-val', `${sysinfo.cpus_online || 1} online core(s)`);
        }

        if (metrics) {
          // CPU Utilization
          const cpuVal = (metrics.cpu_utilization !== null && metrics.cpu_utilization !== undefined)
            ? Number(metrics.cpu_utilization).toFixed(1)
            : '0.0';
          this._setElText('dash-cpu-val', `${cpuVal}%`);

          const cpuBar = document.getElementById('dash-cpu-bar');
          if (cpuBar) {
            const cpuNum = parseFloat(cpuVal);
            cpuBar.style.width = `${Math.min(100, Math.max(0, cpuNum))}%`;
            cpuBar.className = `progress-fill ${cpuNum > 85 ? 'crit' : cpuNum > 65 ? 'warn' : ''}`;
          }

          // Memory Allocation
          if (metrics.memory && metrics.memory.total_kb > 0) {
            const totalKb = metrics.memory.total_kb;
            const availKb = metrics.memory.available_kb || metrics.memory.free_kb || 0;
            const usedKb = totalKb - availKb;
            const memPct = ((usedKb / totalKb) * 100).toFixed(1);
            this._setElText('dash-mem-val', `${memPct}%`);

            const memBar = document.getElementById('dash-mem-bar');
            if (memBar) {
              const memNum = parseFloat(memPct);
              memBar.style.width = `${memNum}%`;
              memBar.className = `progress-fill ${memNum > 85 ? 'crit' : memNum > 65 ? 'warn' : ''}`;
            }

            const usedMB = (usedKb / 1024).toFixed(0);
            const totalMB = (totalKb / 1024).toFixed(0);
            this._setElText('dash-mem-desc', `${usedMB} MB used / ${totalMB} MB total`);

            const swapTotalMB = ((metrics.memory.swap_total_kb || 0) / 1024).toFixed(0);
            const swapFreeMB = ((metrics.memory.swap_free_kb || 0) / 1024).toFixed(0);
            const swapUsedMB = Math.max(0, parseFloat(swapTotalMB) - parseFloat(swapFreeMB)).toFixed(0);
            this._setElText('dash-detail-swap', `${swapUsedMB} MB used / ${swapTotalMB} MB total`);
          }

          // Process Count
          const procRes = await window.api.getProcesses().catch(() => null);
          if (procRes && procRes.total !== undefined) {
            this._setElText('dash-proc-val', procRes.total);
            this._setElText('dash-proc-desc', `${procRes.count || procRes.total} active monitored tasks`);
          }
        }
      } catch (err) {
        console.warn('Dashboard data fetch error:', err);
      }
    },

    // ========================================================
    // PROCESS EXPLORER
    // ========================================================
    _bindProcessControls() {
      const searchInput = document.getElementById('proc-search-input');
      if (searchInput) {
        searchInput.addEventListener('input', () => this._renderProcesses());
      }

      const sortSelect = document.getElementById('proc-sort-select');
      if (sortSelect) {
        sortSelect.addEventListener('change', () => this._renderProcesses());
      }
    },

    _startProcessPolling() {
      this._stopProcessPolling();
      this.processTimer = setInterval(() => this.fetchProcesses(), 3000);
    },

    _stopProcessPolling() {
      if (this.processTimer) {
        clearInterval(this.processTimer);
        this.processTimer = null;
      }
    },

    async fetchProcesses() {
      try {
        const res = await window.api.getProcesses();
        this.processCache = res.processes || [];
        this._renderProcesses();
      } catch (err) {
        console.warn('Failed to fetch processes:', err);
      }
    },

    _renderProcesses() {
      const tbody = document.getElementById('proc-table-body');
      if (!tbody) return;

      const search = (document.getElementById('proc-search-input')?.value || '').trim().toLowerCase();
      const sortBy = document.getElementById('proc-sort-select')?.value || 'cpu';

      let list = [...this.processCache];

      // Filter by search query
      if (search) {
        list = list.filter((p) =>
          String(p.pid).includes(search) ||
          (p.name && p.name.toLowerCase().includes(search))
        );
      }

      // Sort
      list.sort((a, b) => {
        if (sortBy === 'cpu') {
          return (b.cpu_percent || 0) - (a.cpu_percent || 0);
        } else if (sortBy === 'memory') {
          return (b.rss_bytes || 0) - (a.rss_bytes || 0);
        } else if (sortBy === 'pid') {
          return a.pid - b.pid;
        } else if (sortBy === 'name') {
          return (a.name || '').localeCompare(b.name || '');
        }
        return 0;
      });

      const scrollContainer = document.querySelector('.table-scroll');
      const prevScrollTop = scrollContainer ? scrollContainer.scrollTop : 0;

      // Display top 60
      const displayList = list.slice(0, 60);
      tbody.innerHTML = '';

      if (displayList.length === 0) {
        const emptyTr = document.createElement('tr');
        emptyTr.innerHTML = '<td colspan="8"><div class="empty-state">No matching processes found</div></td>';
        tbody.appendChild(emptyTr);
        return;
      }

      displayList.forEach((p) => {
        const tr = document.createElement('tr');

        const stateChar = (p.state || 'S').toUpperCase();
        let badgeClass = 'badge-sleep';
        if (stateChar === 'R') badgeClass = 'badge-run';
        else if (stateChar === 'T') badgeClass = 'badge-stop';
        else if (stateChar === 'Z' || stateChar === 'X') badgeClass = 'badge-zombie';

        const cpuText = p.cpu_percent !== undefined ? `${Number(p.cpu_percent).toFixed(1)}%` : '-';
        const memMB = ((p.rss_bytes || 0) / (1024 * 1024)).toFixed(1);
        const escName = this._escapeHTML(p.name);

        tr.innerHTML = `
          <td><strong>${p.pid}</strong></td>
          <td>${p.ppid}</td>
          <td><span class="proc-name" title="${escName}">${escName}</span></td>
          <td><span class="badge ${badgeClass}">${stateChar}</span></td>
          <td class="num">${cpuText}</td>
          <td class="num">${memMB} MB</td>
          <td class="num">${p.threads || 1}</td>
          <td class="num">
            <button class="btn btn-secondary btn-sm" onclick="app.openProcModal(${p.pid})">Inspect</button>
          </td>
        `;

        tbody.appendChild(tr);
      });

      if (scrollContainer && prevScrollTop > 0) {
        scrollContainer.scrollTop = prevScrollTop;
      }
    },

    // Process Details Modal
    async openProcModal(pid) {
      const modal = document.getElementById('proc-modal');
      const title = document.getElementById('modal-proc-title');
      const content = document.getElementById('modal-proc-content');
      if (!modal || !content) return;

      if (title) title.textContent = `Process Details — PID ${pid}`;
      content.innerHTML = '<div class="loading-state">Loading process metrics...</div>';
      modal.classList.add('active');

      try {
        const p = await window.api.getProcess(pid);
        const memRSS = ((p.rss_bytes || 0) / (1024 * 1024)).toFixed(2);
        const memVSIZE = ((p.vsize_bytes || 0) / (1024 * 1024)).toFixed(2);
        const cpuPct = p.cpu_percent !== undefined ? `${Number(p.cpu_percent).toFixed(1)}%` : '-';

        content.innerHTML = `
          <table class="info-table">
            <tbody>
              <tr><td>Process Name</td><td><strong>${this._escapeHTML(p.name)}</strong></td></tr>
              <tr><td>Process ID (PID)</td><td class="num">${p.pid}</td></tr>
              <tr><td>Parent PID (PPID)</td><td class="num">${p.ppid}</td></tr>
              <tr><td>Process Group (PGID)</td><td class="num">${p.pgid}</td></tr>
              <tr><td>Session ID (SID)</td><td class="num">${p.sid}</td></tr>
              <tr><td>User ID (UID)</td><td class="num">${p.uid}</td></tr>
              <tr><td>Execution State</td><td>${p.state_str || p.state} (${p.state})</td></tr>
              <tr><td>Thread Count</td><td class="num">${p.threads}</td></tr>
              <tr><td>Live CPU Utilization</td><td class="num">${cpuPct}</td></tr>
              <tr><td>Resident Memory (RSS)</td><td class="num">${memRSS} MB (${(p.rss_bytes || 0).toLocaleString()} bytes)</td></tr>
              <tr><td>Virtual Memory (VSIZE)</td><td class="num">${memVSIZE} MB (${(p.vsize_bytes || 0).toLocaleString()} bytes)</td></tr>
              <tr><td>User Time Ticks</td><td class="num">${p.utime_ticks || 0}</td></tr>
              <tr><td>Kernel System Ticks</td><td class="num">${p.stime_ticks || 0}</td></tr>
            </tbody>
          </table>
        `;
      } catch (err) {
        content.innerHTML = `<div class="error-state">Failed to inspect process ${pid}: ${this._escapeHTML(err.message)}</div>`;
      }
    },

    closeProcModal() {
      const modal = document.getElementById('proc-modal');
      if (modal) {
        modal.classList.remove('active');
      }
    },

    _bindModalControls() {
      const modal = document.getElementById('proc-modal');
      if (modal) {
        modal.addEventListener('click', (e) => {
          if (e.target === modal) {
            this.closeProcModal();
          }
        });
      }

      document.addEventListener('keydown', (e) => {
        if (e.key === 'Escape' || e.key === 'Esc') {
          const m = document.getElementById('proc-modal');
          if (m && m.classList.contains('active')) {
            this.closeProcModal();
          }
        }
      });
    },

    // ========================================================
    // CPU SCHEDULING LAB
    // ========================================================
    _bindSchedulingLab() {
      // Algorithm chip selector
      const chips = document.querySelectorAll('.chip-btn');
      chips.forEach((chip) => {
        chip.addEventListener('click', () => {
          chips.forEach((c) => c.classList.remove('active'));
          chip.classList.add('active');
          this.selectedAlgorithm = chip.dataset.algo || 'fcfs';
        });
      });

      // Add process button
      const addBtn = document.getElementById('btn-add-process');
      if (addBtn) {
        addBtn.addEventListener('click', () => {
          const rows = document.querySelectorAll('#workload-tbody tr');
          const nextPid = rows.length + 1;
          this.addWorkloadRow(nextPid, 0, 5, 2);
        });
      }

      // Demo workload button
      const demoBtn = document.getElementById('btn-load-demo');
      if (demoBtn) {
        demoBtn.addEventListener('click', () => this.loadDemoWorkload());
      }

      // Simulate button
      const simBtn = document.getElementById('btn-simulate');
      if (simBtn) {
        simBtn.addEventListener('click', () => this.runSimulation());
      }

      // Compare button
      const compBtn = document.getElementById('btn-compare');
      if (compBtn) {
        compBtn.addEventListener('click', () => this.runCompare());
      }

      // Load initial default demo workload
      this.loadDemoWorkload();
    },

    loadDemoWorkload() {
      const tbody = document.getElementById('workload-tbody');
      if (!tbody) return;

      tbody.innerHTML = '';
      const demo = [
        { pid: 1, arrival: 0, burst: 8, priority: 3 },
        { pid: 2, arrival: 1, burst: 4, priority: 1 },
        { pid: 3, arrival: 3, burst: 9, priority: 4 },
        { pid: 4, arrival: 5, burst: 2, priority: 2 },
        { pid: 5, arrival: 26, burst: 4, priority: 2 }
      ];

      demo.forEach((p) => this.addWorkloadRow(p.pid, p.arrival, p.burst, p.priority));
    },

    addWorkloadRow(pid, arrival, burst, priority) {
      const tbody = document.getElementById('workload-tbody');
      if (!tbody) return;

      const tr = document.createElement('tr');
      tr.innerHTML = `
        <td><input type="number" class="form-input form-input-sm form-input-num" value="${pid}" min="1"></td>
        <td><input type="number" class="form-input form-input-sm form-input-num" value="${arrival}" min="0"></td>
        <td><input type="number" class="form-input form-input-sm form-input-num" value="${burst}" min="1"></td>
        <td><input type="number" class="form-input form-input-sm form-input-num" value="${priority}" min="1"></td>
        <td class="num">
          <button class="btn btn-ghost btn-sm remove-row-btn" title="Remove process">&times;</button>
        </td>
      `;

      tr.querySelector('.remove-row-btn').addEventListener('click', () => {
        tr.remove();
      });

      tbody.appendChild(tr);
    },

    _getWorkloadProcesses() {
      const rows = document.querySelectorAll('#workload-tbody tr');
      const processes = [];

      rows.forEach((r) => {
        const inputs = r.querySelectorAll('input');
        if (inputs.length >= 4) {
          const pid = parseInt(inputs[0].value, 10);
          const arrival = parseInt(inputs[1].value, 10);
          const burst = parseInt(inputs[2].value, 10);
          const priority = parseInt(inputs[3].value, 10);

          if (!isNaN(pid) && !isNaN(arrival) && !isNaN(burst) && burst > 0) {
            processes.push({
              pid,
              arrival_time: Math.max(0, arrival),
              burst_time: burst,
              priority: isNaN(priority) ? 1 : priority
            });
          }
        }
      });

      return processes;
    },

    async runSimulation() {
      let processes = this._getWorkloadProcesses();
      if (processes.length === 0) {
        this.loadDemoWorkload();
        processes = this._getWorkloadProcesses();
      }

      const quantum = parseInt(document.getElementById('sched-quantum-input')?.value || '4', 10);
      const container = document.getElementById('sched-results-container');
      if (container) {
        container.innerHTML = '<div class="loading-state">Running scheduling simulation...</div>';
      }

      try {
        const res = await window.api.simulateScheduler(this.selectedAlgorithm, processes, quantum);
        this._renderSimulationOutput(res);
      } catch (err) {
        if (container) {
          container.innerHTML = `<div class="error-state">Simulation failed: ${this._escapeHTML(err.message)}</div>`;
        }
        this.showToast(`Simulation failed: ${err.message}`, 'error');
      }
    },

    _renderSimulationOutput(res) {
      const container = document.getElementById('sched-results-container');
      if (!container) return;

      const summary = res.summary || {};
      const timeline = res.timeline || [];
      const processes = res.processes || [];
      const totalTime = summary.total_elapsed_time || (timeline.length > 0 ? timeline[timeline.length - 1].end_time : 1);
      const util = summary.cpu_utilization !== undefined ? (summary.cpu_utilization * 100).toFixed(1) : '100.0';

      const colors = [
        'var(--gantt-p1)',
        'var(--gantt-p2)',
        'var(--gantt-p3)',
        'var(--gantt-p4)',
        'var(--gantt-p5)',
        'var(--gantt-p6)',
        'var(--gantt-p7)'
      ];

      let timelineHtml = '';

      if (timeline.length > 0) {
        const slices = timeline.map((slice) => {
          const dur = slice.end_time - slice.start_time;
          const pct = Math.max(0.5, (dur / totalTime) * 100);
          const isIdle = slice.is_idle || slice.pid === 0;
          const bg = isIdle ? 'var(--gantt-idle)' : colors[(slice.pid - 1 + colors.length) % colors.length];
          const label = isIdle ? 'IDLE' : `P${slice.pid}`;

          return `<div class="gantt-block" style="width: ${pct}%; background-color: ${bg};" title="${label}: [${slice.start_time} - ${slice.end_time}] (${dur} ticks)">
            <span>${label}</span>
          </div>`;
        }).join('');

        timelineHtml = `
          <div class="gantt-timeline-section">
            <div class="gantt-timeline-label">CPU Execution Timeline</div>
            <div class="gantt-chart-bar">${slices}</div>
            <div class="gantt-time-scale">
              <span>T: 0</span>
              <span>T: ${Math.round(totalTime / 2)}</span>
              <span>T: ${totalTime}</span>
            </div>
          </div>
        `;
      }

      let procTableHtml = '';
      if (processes.length > 0) {
        procTableHtml = `
          <table class="data-table">
            <thead>
              <tr>
                <th>PID</th>
                <th class="num">Arrival</th>
                <th class="num">Burst</th>
                <th class="num">Start</th>
                <th class="num">Completion</th>
                <th class="num">Waiting</th>
                <th class="num">Turnaround</th>
                <th class="num">Response</th>
              </tr>
            </thead>
            <tbody>
              ${processes.map((p) => `
                <tr>
                  <td><strong>P${p.pid}</strong></td>
                  <td class="num">${p.arrival_time}</td>
                  <td class="num">${p.burst_time}</td>
                  <td class="num">${p.start_time}</td>
                  <td class="num">${p.completion_time}</td>
                  <td class="num">${p.waiting_time}</td>
                  <td class="num">${p.turnaround_time}</td>
                  <td class="num">${p.response_time}</td>
                </tr>
              `).join('')}
            </tbody>
          </table>
        `;
      }

      container.innerHTML = `
        <div class="card-header">
          <span class="card-title">${(res.algorithm || this.selectedAlgorithm).toUpperCase()} Execution Results</span>
          <span class="text-secondary font-mono text-xs">
            Elapsed: <strong>${totalTime}</strong> ticks &nbsp;|&nbsp; CPU Util: <strong>${util}%</strong>
          </span>
        </div>
        <div class="sim-summary-grid">
          <div class="sim-stat-box">
            <div class="sim-stat-label">Avg Waiting Time</div>
            <div class="sim-stat-val">
              ${summary.avg_waiting_time !== undefined ? summary.avg_waiting_time.toFixed(2) : '-'}
            </div>
          </div>
          <div class="sim-stat-box">
            <div class="sim-stat-label">Avg Turnaround Time</div>
            <div class="sim-stat-val">
              ${summary.avg_turnaround_time !== undefined ? summary.avg_turnaround_time.toFixed(2) : '-'}
            </div>
          </div>
          <div class="sim-stat-box">
            <div class="sim-stat-label">Avg Response Time</div>
            <div class="sim-stat-val">
              ${summary.avg_response_time !== undefined ? summary.avg_response_time.toFixed(2) : '-'}
            </div>
          </div>
          <div class="sim-stat-box">
            <div class="sim-stat-label">CPU Utilization</div>
            <div class="sim-stat-val">
              ${util}%
            </div>
          </div>
        </div>
        ${timelineHtml}
        ${procTableHtml}
      `;
    },

    async runCompare() {
      let processes = this._getWorkloadProcesses();
      if (processes.length === 0) {
        this.loadDemoWorkload();
        processes = this._getWorkloadProcesses();
      }

      const quantum = parseInt(document.getElementById('sched-quantum-input')?.value || '4', 10);
      const container = document.getElementById('sched-results-container');
      if (container) {
        container.innerHTML = '<div class="loading-state">Comparing all 5 scheduling algorithms...</div>';
      }

      try {
        const res = await window.api.compareScheduler(processes, quantum);
        this._renderCompareOutput(res);
      } catch (err) {
        if (container) {
          container.innerHTML = `<div class="error-state">Comparison failed: ${this._escapeHTML(err.message)}</div>`;
        }
        this.showToast(`Comparison failed: ${err.message}`, 'error');
      }
    },

    _renderCompareOutput(res) {
      const container = document.getElementById('sched-results-container');
      if (!container) return;

      const algos = ['fcfs', 'sjf', 'rr', 'priority', 'mlfq'];
      const names = {
        fcfs: 'First-Come First-Served (FCFS)',
        sjf: 'Shortest Job First (SJF)',
        rr: `Round Robin (RR, q=${res.quantum || 4})`,
        priority: 'Preemptive Priority',
        mlfq: 'Multi-Level Feedback Queue (MLFQ)'
      };

      const results = res.results || {};

      container.innerHTML = `
        <div class="card-header">
          <span class="card-title">Comparative Analysis of 5 Scheduling Algorithms</span>
          <span class="text-secondary text-xs">Workload: <strong>${(res.workload_count || 5)}</strong> processes</span>
        </div>
        <table class="data-table">
          <thead>
            <tr>
              <th>Algorithm</th>
              <th class="num">Avg Waiting Time</th>
              <th class="num">Avg Turnaround Time</th>
              <th class="num">Avg Response Time</th>
              <th class="num">CPU Utilization</th>
              <th class="num">Total Elapsed</th>
            </tr>
          </thead>
          <tbody>
            ${algos.map((algo) => {
              const r = results[algo] || {};
              const s = r.summary || {};
              const util = s.cpu_utilization !== undefined ? `${(s.cpu_utilization * 100).toFixed(1)}%` : '-';
              return `
                <tr>
                  <td><strong>${names[algo] || algo.toUpperCase()}</strong></td>
                  <td class="num">${s.avg_waiting_time !== undefined ? s.avg_waiting_time.toFixed(2) : '-'}</td>
                  <td class="num">${s.avg_turnaround_time !== undefined ? s.avg_turnaround_time.toFixed(2) : '-'}</td>
                  <td class="num">${s.avg_response_time !== undefined ? s.avg_response_time.toFixed(2) : '-'}</td>
                  <td class="num">${util}</td>
                  <td class="num">${s.total_elapsed_time !== undefined ? s.total_elapsed_time + ' ticks' : '-'}</td>
                </tr>
              `;
            }).join('')}
          </tbody>
        </table>
      `;
    },

    // ========================================================
    // SYSTEM EXPLORER
    // ========================================================
    async fetchSystemData() {
      try {
        const [sysinfo, metrics] = await Promise.all([
          window.api.getSysinfo().catch(() => null),
          window.api.getMetrics().catch(() => null)
        ]);

        if (sysinfo) {
          this._setElText('sys-os-name', sysinfo.os_name || '-');
          this._setElText('sys-kernel-ver', sysinfo.kernel_version || sysinfo.kernel_release || '-');
          this._setElText('sys-arch', sysinfo.architecture || '-');
          this._setElText('sys-cpus-online', sysinfo.cpus_online || 1);
          this._setElText('sys-cpus-conf', sysinfo.cpus_configured || 1);
          this._setElText('sys-proc-status', sysinfo.proc_accessible ? 'Accessible (/proc virtual filesystem)' : 'Restricted');

          if (sysinfo.shell) {
            this._setElText('sys-shell-pid', sysinfo.shell.pid);
            this._setElText('sys-shell-ppid', sysinfo.shell.ppid);
            this._setElText('sys-shell-pgid', sysinfo.shell.pgid);
            this._setElText('sys-shell-sid', sysinfo.shell.sid);
          }

          if (sysinfo.mem) {
            this._setElText('sys-mem-total', `${(sysinfo.mem.total_kb || 0).toLocaleString()} KiB`);
            this._setElText('sys-mem-avail', `${(sysinfo.mem.available_kb || 0).toLocaleString()} KiB`);
            this._setElText('sys-mem-free', `${(sysinfo.mem.free_kb || 0).toLocaleString()} KiB`);
            this._setElText('sys-mem-swap-total', `${(sysinfo.mem.swap_total_kb || 0).toLocaleString()} KiB`);
            this._setElText('sys-mem-swap-free', `${(sysinfo.mem.swap_free_kb || 0).toLocaleString()} KiB`);
          }
        }

        if (metrics && metrics.memory) {
          this._setElText('sys-mem-buffers', `${(metrics.memory.buffers_kb || 0).toLocaleString()} KiB`);
          this._setElText('sys-mem-cached', `${(metrics.memory.cached_kb || 0).toLocaleString()} KiB`);
        }
      } catch (err) {
        console.warn('System Explorer fetch error:', err);
      }
    },

    // ========================================================
    // SETTINGS & AUTH
    // ========================================================
    _bindSettings() {
      const tokenInput = document.getElementById('auth-token-input');
      if (tokenInput && window.api.token) {
        tokenInput.value = window.api.token;
      }

      const saveBtn = document.getElementById('btn-save-token');
      if (tokenInput && saveBtn) {
        tokenInput.addEventListener('keydown', (e) => {
          if (e.key === 'Enter') {
            e.preventDefault();
            saveBtn.click();
          }
        });
      }

      if (saveBtn) {
        saveBtn.addEventListener('click', async () => {
          const val = (tokenInput?.value || '').trim();
          if (!val) {
            this.showToast('Token cannot be empty', 'warn');
            return;
          }
          window.api.setToken(val);
          try {
            const auth = await window.api.verifyAuth();
            if (auth && auth.authenticated) {
              this.setConnectionStatus('connected', 'Authenticated');
              this.showToast('Access token verified and saved!', 'success');
              this.switchView('terminal');
              this.syncTerminalSessions();
            } else {
              this.showToast('Server rejected the provided token', 'error');
            }
          } catch (err) {
            this.showToast(`Verification failed: ${err.message}`, 'error');
          }
        });
      }

      const clearBtn = document.getElementById('btn-clear-token');
      if (clearBtn) {
        clearBtn.addEventListener('click', () => {
          window.api.setToken('');
          if (tokenInput) tokenInput.value = '';
          // Terminate active terminal sessions and close WebSockets
          this.sessions.forEach((s) => {
            s.terminal.destroy();
            s.tabEl.remove();
            s.wrapperEl.remove();
          });
          this.sessions.clear();
          this.currentSessionId = null;
          this._updateSessionCount();
          this.setConnectionStatus('disconnected', 'Logged Out');
          this.showToast('Logged out. Token and sessions cleared.', 'info');
        });
      }
    },

    // Helpers
    _setElText(id, val) {
      const el = document.getElementById(id);
      if (el) el.textContent = val !== null && val !== undefined ? String(val) : '-';
    },

    _escapeHTML(str) {
      return (str || '')
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;');
    }
  };

  window.app = app;
  app.init();
});
