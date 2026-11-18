/**
 * SmartShell — Enterprise ANSI Terminal Emulator
 * Features 2D grid buffer, scrollback history, alternate screen buffer,
 * 256-color ANSI rendering, and robust WebSocket lifecycle management.
 */

class SmartTerminal {
  constructor(containerEl, options = {}) {
    this.container = containerEl;
    this.sessionId = options.sessionId || null;
    this.initialTicket = options.ticket || null;
    this.onExit = options.onExit || null;
    this.onStatusChange = options.onStatusChange || null;

    this.cols = options.cols || 80;
    this.rows = options.rows || 24;
    this.maxScrollback = options.maxScrollback || 1500;

    this.ws = null;
    this.pingInterval = null;
    this.isConnected = false;
    this.isReconnecting = false;
    this.reconnectAttempts = 0;
    this.maxReconnectAttempts = 6;
    this.explicitClose = false;

    // Buffer state
    this.cursorX = 0;
    this.cursorY = 0;
    this.cursorVisible = true;
    this.savedCursor = { x: 0, y: 0 };

    this.currentAttrs = {
      fg: null,
      bg: null,
      bold: false,
      dim: false,
      italic: false,
      underline: false,
      inverse: false
    };

    // Primary and Alternate Screen Buffers
    this.lines = [];
    this.scrollback = [];
    this.altLines = [];
    this.isAltScreen = false;

    this._initBuffer();
    this._initDOM();
    this._attachEvents();
  }

  _createEmptyLine() {
    return [];
  }

  _initBuffer() {
    this.lines = [];
    for (let r = 0; r < this.rows; r++) {
      this.lines.push(this._createEmptyLine());
    }
  }

  _initDOM() {
    this.container.innerHTML = '';
    this.wrapper = document.createElement('div');
    this.wrapper.className = 'terminal-screen-wrapper';
    this.wrapper.tabIndex = 0;
    this.wrapper.spellcheck = false;

    this.container.appendChild(this.wrapper);
    this._render();
  }

  _attachEvents() {
    this.wrapper.addEventListener('keydown', (e) => this._onKeyDown(e));
    this.wrapper.addEventListener('paste', (e) => this._onPaste(e));
    this.container.addEventListener('click', () => this.focus());

    // Resize observer
    this.resizeObserver = new ResizeObserver(() => this.fit());
    this.resizeObserver.observe(this.container);
  }

  focus() {
    if (this.wrapper) {
      this.wrapper.focus();
    }
  }

  fit() {
    if (!this.container || !this.wrapper) return;
    const rect = this.container.getBoundingClientRect();
    if (rect.width <= 0 || rect.height <= 0) return;

    // Measure character cell dimensions
    const testSpan = document.createElement('span');
    testSpan.style.fontFamily = 'ui-monospace, SFMono-Regular, "SF Mono", Menlo, Consolas, monospace';
    testSpan.style.fontSize = '13.5px';
    testSpan.style.lineHeight = '1.25';
    testSpan.style.visibility = 'hidden';
    testSpan.style.position = 'absolute';
    testSpan.textContent = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789';
    this.wrapper.appendChild(testSpan);
    const charW = testSpan.getBoundingClientRect().width / 36;
    const charH = testSpan.getBoundingClientRect().height || 17;
    testSpan.remove();

    const padX = 24;
    const padY = 16;
    const availableW = Math.max(100, rect.width - padX);
    const availableH = Math.max(80, rect.height - padY);

    const newCols = Math.max(20, Math.min(220, Math.floor(availableW / (charW || 8.2))));
    const newRows = Math.max(5, Math.min(80, Math.floor(availableH / (charH || 17))));

    if (newCols !== this.cols || newRows !== this.rows) {
      this.resize(newCols, newRows);
    }
  }

  resize(newCols, newRows) {
    const oldRows = this.rows;
    this.cols = newCols;
    this.rows = newRows;

    const curLines = this.isAltScreen ? this.altLines : this.lines;
    if (newRows > oldRows) {
      for (let r = oldRows; r < newRows; r++) {
        curLines.push(this._createEmptyLine());
      }
    } else if (newRows < oldRows) {
      // Shrink rows, move extra top lines to scrollback if normal screen
      const diff = oldRows - newRows;
      for (let i = 0; i < diff; i++) {
        if (!this.isAltScreen && curLines.length > 0) {
          this.scrollback.push(curLines.shift());
          if (this.scrollback.length > this.maxScrollback) {
            this.scrollback.shift();
          }
        } else {
          curLines.pop();
        }
      }
      this.cursorY = Math.min(this.cursorY, newRows - 1);
    }
    this.cursorX = Math.min(this.cursorX, newCols - 1);

    this._sendResize(this.cols, this.rows);
    this._render();
  }

  _sendResize(cols, rows) {
    if (this.ws && this.ws.readyState === WebSocket.OPEN && this.isConnected) {
      this.ws.send(JSON.stringify({ type: 'resize', cols, rows }));
    }
    if (this.sessionId && window.api) {
      window.api.resizeSession(this.sessionId, cols, rows).catch(() => {});
    }
  }

  connect(ticket = null) {
    const wsTicket = ticket || this.initialTicket;
    if (!this.sessionId || !wsTicket) {
      this._updateStatus('disconnected', 'Missing credentials');
      return;
    }

    this.explicitClose = false;
    this._updateStatus('connecting', 'Connecting...');
    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    const wsUrl = `${protocol}//${window.location.host}/ws/terminal`;

    try {
      this.ws = new WebSocket(wsUrl);
    } catch (e) {
      this._updateStatus('disconnected', `Connection error: ${e.message}`);
      return;
    }

    this.ws.onopen = () => {
      // Send in-band authentication as required by Phase 13/14
      this.ws.send(JSON.stringify({
        type: 'auth',
        session_id: this.sessionId,
        ticket: wsTicket
      }));
    };

    this.ws.onmessage = (event) => {
      try {
        const msg = JSON.parse(event.data);
        this._handleServerMessage(msg);
      } catch (err) {
        this.write(event.data);
      }
    };

    this.ws.onerror = (e) => {
      console.warn('Terminal socket error:', e);
    };

    this.ws.onclose = (event) => {
      this._stopPing();
      this.isConnected = false;

      if (this.explicitClose || event.code === 1000) {
        this._updateStatus('disconnected', 'Session terminated');
      } else {
        this._updateStatus('disconnected', 'Disconnected');
        this._attemptReconnect();
      }
    };
  }

  _handleServerMessage(msg) {
    switch (msg.type) {
      case 'auth_ok':
        this.isConnected = true;
        this.isReconnecting = false;
        this.reconnectAttempts = 0;
        this._updateStatus('connected', 'Connected');
        this._startPing();
        this.fit();
        break;

      case 'output':
        if (msg.data) {
          this.write(msg.data);
        }
        break;

      case 'exit':
        this.isConnected = false;
        this._stopPing();
        this._updateStatus('disconnected', `Exited (${msg.code})`);
        this.write(`\r\n\x1b[1;33m[Process completed with exit code ${msg.code}]\x1b[0m\r\n`);
        if (this.onExit) this.onExit(msg.code);
        break;

      case 'error':
        this.write(`\r\n\x1b[1;31m[Error: ${msg.message}]\x1b[0m\r\n`);
        break;

      case 'pong':
        // Heartbeat confirmed
        break;
    }
  }

  async _attemptReconnect() {
    if (this.explicitClose || this.isReconnecting || this.reconnectAttempts >= this.maxReconnectAttempts) return;
    this.isReconnecting = true;
    this.reconnectAttempts++;

    this._updateStatus('connecting', `Reconnecting (${this.reconnectAttempts}/${this.maxReconnectAttempts})...`);
    this.write(`\r\n\x1b[33m[Connection dropped. Reconnecting within grace period...]\x1b[0m\r\n`);

    await new Promise((r) => setTimeout(r, 1500));

    try {
      const res = await window.api.reconnectSession(this.sessionId);
      if (res && res.ws_ticket) {
        this.connect(res.ws_ticket);
      } else {
        throw new Error('Grace period expired or session closed');
      }
    } catch (err) {
      this.isReconnecting = false;
      this._updateStatus('disconnected', 'Session expired');
      this.write(`\r\n\x1b[31m[Reconnection failed: ${err.message}]\x1b[0m\r\n`);
    }
  }

  _startPing() {
    this._stopPing();
    this.pingInterval = setInterval(() => {
      if (this.ws && this.ws.readyState === WebSocket.OPEN) {
        this.ws.send(JSON.stringify({ type: 'ping' }));
      }
    }, 12000);
  }

  _stopPing() {
    if (this.pingInterval) {
      clearInterval(this.pingInterval);
      this.pingInterval = null;
    }
  }

  _updateStatus(status, label) {
    if (this.onStatusChange) {
      this.onStatusChange(status, label);
    }
  }

  sendInput(data) {
    if (this.ws && this.ws.readyState === WebSocket.OPEN && this.isConnected) {
      this.ws.send(JSON.stringify({ type: 'input', data }));
    }
  }

  // --- Keyboard & Paste Handling ---
  _onKeyDown(e) {
    if (!this.isConnected) return;

    let payload = null;

    if (e.ctrlKey && !e.altKey && !e.metaKey) {
      // Allow native browser copy (Ctrl+C) when text is selected
      if (e.key === 'c' || e.key === 'C') {
        const sel = window.getSelection();
        if (sel && sel.toString().length > 0) {
          return;
        }
      }
      // Allow native browser paste (Ctrl+V) so paste event fires
      if (e.key === 'v' || e.key === 'V') {
        return;
      }

      const keyUpper = e.key.toUpperCase();
      const code = keyUpper.charCodeAt(0);
      if (code >= 65 && code <= 90) { // Ctrl+A to Ctrl+Z
        payload = String.fromCharCode(code - 64);
      } else if (e.key === ' ') {
        payload = '\x00';
      } else if (e.key === '[') {
        payload = '\x1b';
      } else if (e.key === '\\') {
        payload = '\x1c';
      } else if (e.key === ']') {
        payload = '\x1d';
      }
    } else {
      switch (e.key) {
        case 'Enter':
          payload = '\r';
          break;
        case 'Backspace':
          payload = '\x7f';
          break;
        case 'Tab':
          payload = '\t';
          e.preventDefault();
          break;
        case 'Escape':
          payload = '\x1b';
          break;
        case 'ArrowUp':
          payload = '\x1b[A';
          e.preventDefault();
          break;
        case 'ArrowDown':
          payload = '\x1b[B';
          e.preventDefault();
          break;
        case 'ArrowRight':
          payload = '\x1b[C';
          e.preventDefault();
          break;
        case 'ArrowLeft':
          payload = '\x1b[D';
          e.preventDefault();
          break;
        case 'Home':
          payload = '\x1b[H';
          break;
        case 'End':
          payload = '\x1b[F';
          break;
        case 'PageUp':
          payload = '\x1b[5~';
          break;
        case 'PageDown':
          payload = '\x1b[6~';
          break;
        case 'Insert':
          payload = '\x1b[2~';
          break;
        case 'Delete':
          payload = '\x1b[3~';
          break;
        default:
          if (e.key.length === 1 && !e.altKey && !e.metaKey) {
            payload = e.key;
          }
          break;
      }
    }

    if (payload !== null) {
      e.preventDefault();
      this.sendInput(payload);
    }
  }

  _onPaste(e) {
    e.preventDefault();
    const text = (e.clipboardData || window.clipboardData).getData('text');
    if (text) {
      this.sendInput(text);
    }
  }

  // --- ANSI Parsing & Screen Buffer Operations ---
  write(data) {
    if (!data) return;

    let i = 0;
    const len = data.length;

    while (i < len) {
      const ch = data[i];

      if (ch === '\r') {
        this.cursorX = 0;
        i++;
      } else if (ch === '\n') {
        this._lineFeed();
        i++;
      } else if (ch === '\b') {
        this.cursorX = Math.max(0, this.cursorX - 1);
        i++;
      } else if (ch === '\t') {
        const tabSize = 8;
        const spaces = tabSize - (this.cursorX % tabSize);
        for (let s = 0; s < spaces; s++) {
          this._putChar(' ');
        }
        i++;
      } else if (ch === '\x07') {
        // Bell (ignore)
        i++;
      } else if (ch === '\x1b') {
        // Escape sequence
        if (i + 1 < len && data[i + 1] === '[') {
          // CSI sequence
          let j = i + 2;
          let isPrivate = false;
          if (j < len && data[j] === '?') {
            isPrivate = true;
            j++;
          }
          while (j < len && !/[a-zA-Z~]/.test(data[j])) {
            j++;
          }
          if (j < len) {
            const finalChar = data[j];
            const rawParams = data.slice(isPrivate ? i + 3 : i + 2, j);
            this._handleCSI(finalChar, rawParams, isPrivate);
            i = j + 1;
          } else {
            break;
          }
        } else if (i + 1 < len && data[i + 1] === ']') {
          // OSC sequence (Operating System Command) - skip until ST (\x07 or \x1b\\)
          let j = i + 2;
          while (j < len && data[j] !== '\x07' && !(data[j] === '\x1b' && data[j + 1] === '\\')) {
            j++;
          }
          i = (j < len && data[j] === '\x1b') ? j + 2 : j + 1;
        } else {
          // Other 2-char escape sequences
          i += 2;
        }
      } else {
        this._putChar(ch);
        i++;
      }
    }

    this._render();
  }

  _lineFeed() {
    this.cursorY++;
    const curLines = this.isAltScreen ? this.altLines : this.lines;
    if (this.cursorY >= this.rows) {
      this.cursorY = this.rows - 1;
      const topRow = curLines.shift();
      if (!this.isAltScreen) {
        this.scrollback.push(topRow || this._createEmptyLine());
        if (this.scrollback.length > this.maxScrollback) {
          this.scrollback.shift();
        }
      }
      curLines.push(this._createEmptyLine());
    }
  }

  _putChar(ch) {
    if (this.cursorX >= this.cols) {
      this.cursorX = 0;
      this._lineFeed();
    }

    const curLines = this.isAltScreen ? this.altLines : this.lines;
    while (curLines.length <= this.cursorY) {
      curLines.push(this._createEmptyLine());
    }

    const line = curLines[this.cursorY];
    while (line.length < this.cursorX) {
      line.push({ ch: ' ', attrs: null });
    }

    const cellAttrs = (this.currentAttrs.fg !== null || this.currentAttrs.bg !== null ||
                       this.currentAttrs.bold || this.currentAttrs.underline || this.currentAttrs.dim || this.currentAttrs.italic)
      ? { ...this.currentAttrs }
      : null;

    line[this.cursorX] = { ch, attrs: cellAttrs };
    this.cursorX++;
  }

  _handleCSI(finalChar, rawParams, isPrivate) {
    if (isPrivate) {
      const mode = parseInt(rawParams, 10);
      if (finalChar === 'h') {
        if (mode === 25) this.cursorVisible = true;
        if (mode === 1049 || mode === 47) {
          // Switch to alternate screen buffer
          this.isAltScreen = true;
          this.altLines = [];
          for (let r = 0; r < this.rows; r++) this.altLines.push(this._createEmptyLine());
          this.cursorX = 0;
          this.cursorY = 0;
        }
      } else if (finalChar === 'l') {
        if (mode === 25) this.cursorVisible = false;
        if (mode === 1049 || mode === 47) {
          // Switch back to primary screen buffer
          this.isAltScreen = false;
          this.altLines = [];
        }
      }
      return;
    }

    const args = rawParams ? rawParams.split(';').map((p) => parseInt(p, 10) || 0) : [];
    const curLines = this.isAltScreen ? this.altLines : this.lines;

    switch (finalChar) {
      case 'm': // SGR Styling
        this._handleSGR(args);
        break;

      case 'H': // Cursor Home / Position
      case 'f':
        const r = (args[0] && args[0] > 0) ? args[0] - 1 : 0;
        const c = (args[1] && args[1] > 0) ? args[1] - 1 : 0;
        this.cursorY = Math.max(0, Math.min(this.rows - 1, r));
        this.cursorX = Math.max(0, Math.min(this.cols - 1, c));
        break;

      case 'A': // Cursor Up
        this.cursorY = Math.max(0, this.cursorY - (args[0] || 1));
        break;

      case 'B': // Cursor Down
        this.cursorY = Math.min(this.rows - 1, this.cursorY + (args[0] || 1));
        break;

      case 'C': // Cursor Forward
        this.cursorX = Math.min(this.cols - 1, this.cursorX + (args[0] || 1));
        break;

      case 'D': // Cursor Backward
        this.cursorX = Math.max(0, this.cursorX - (args[0] || 1));
        break;

      case 'G': // Cursor Horizontal Absolute
        this.cursorX = Math.max(0, Math.min(this.cols - 1, (args[0] || 1) - 1));
        break;

      case 'J': // Erase in Display
        const jMode = args[0] || 0;
        if (jMode === 2 || jMode === 3) {
          // Clear entire screen
          for (let row = 0; row < this.rows; row++) {
            curLines[row] = this._createEmptyLine();
          }
          this.cursorX = 0;
          this.cursorY = 0;
        } else if (jMode === 0) {
          // Clear cursor to end
          if (curLines[this.cursorY]) {
            curLines[this.cursorY] = curLines[this.cursorY].slice(0, this.cursorX);
          }
          for (let row = this.cursorY + 1; row < this.rows; row++) {
            curLines[row] = this._createEmptyLine();
          }
        }
        break;

      case 'K': // Erase in Line
        const kMode = args[0] || 0;
        if (!curLines[this.cursorY]) curLines[this.cursorY] = this._createEmptyLine();
        if (kMode === 0) {
          curLines[this.cursorY] = curLines[this.cursorY].slice(0, this.cursorX);
        } else if (kMode === 2) {
          curLines[this.cursorY] = this._createEmptyLine();
        }
        break;

      case 's': // Save cursor
        this.savedCursor = { x: this.cursorX, y: this.cursorY };
        break;

      case 'u': // Restore cursor
        this.cursorX = this.savedCursor.x;
        this.cursorY = this.savedCursor.y;
        break;
    }
  }

  _handleSGR(args) {
    if (args.length === 0) args = [0];

    for (let i = 0; i < args.length; i++) {
      const code = args[i];

      if (code === 0) {
        this.currentAttrs = { fg: null, bg: null, bold: false, dim: false, italic: false, underline: false, inverse: false };
      } else if (code === 1) {
        this.currentAttrs.bold = true;
      } else if (code === 2) {
        this.currentAttrs.dim = true;
      } else if (code === 3) {
        this.currentAttrs.italic = true;
      } else if (code === 4) {
        this.currentAttrs.underline = true;
      } else if (code === 7) {
        this.currentAttrs.inverse = true;
      } else if (code === 22) {
        this.currentAttrs.bold = false;
        this.currentAttrs.dim = false;
      } else if (code === 24) {
        this.currentAttrs.underline = false;
      } else if (code === 27) {
        this.currentAttrs.inverse = false;
      } else if (code >= 30 && code <= 37) {
        this.currentAttrs.fg = code - 30; // standard colors 0-7
      } else if (code === 39) {
        this.currentAttrs.fg = null; // default fg
      } else if (code >= 40 && code <= 47) {
        this.currentAttrs.bg = code - 40; // standard bg
      } else if (code === 49) {
        this.currentAttrs.bg = null; // default bg
      } else if (code >= 90 && code <= 97) {
        this.currentAttrs.fg = (code - 90) + 8; // bright colors 8-15
      } else if (code >= 100 && code <= 107) {
        this.currentAttrs.bg = (code - 100) + 8; // bright bg
      } else if (code === 38 && args[i + 1] === 5 && args[i + 2] !== undefined) {
        this.currentAttrs.fg = args[i + 2]; // 256 colors
        i += 2;
      } else if (code === 48 && args[i + 1] === 5 && args[i + 2] !== undefined) {
        this.currentAttrs.bg = args[i + 2];
        i += 2;
      }
    }
  }

  // --- DOM Renderer ---
  _render() {
    if (!this.wrapper) return;

    // Preserve scroll position if user deliberately scrolled up into history
    const wasNearBottom = (this.wrapper.scrollHeight - this.wrapper.scrollTop - this.wrapper.clientHeight) < 40;

    const visibleLines = [];

    // Include scrollback if not in alternate screen
    if (!this.isAltScreen && this.scrollback.length > 0) {
      for (const line of this.scrollback) {
        visibleLines.push(this._formatLineHTML(line));
      }
    }

    const curLines = this.isAltScreen ? this.altLines : this.lines;
    for (let r = 0; r < this.rows; r++) {
      const line = curLines[r] || [];
      const isCursorRow = (r === this.cursorY);
      visibleLines.push(this._formatLineHTML(line, isCursorRow));
    }

    this.wrapper.innerHTML = visibleLines.join('');
    if (wasNearBottom) {
      this.wrapper.scrollTop = this.wrapper.scrollHeight;
    }
  }

  _formatLineHTML(line, isCursorRow = false) {
    let out = '<div class="terminal-line">';
    const colCount = Math.max(line.length, isCursorRow ? this.cursorX + 1 : 0);

    let curSpanClass = '';
    let curSpanOpen = false;

    for (let c = 0; c < colCount; c++) {
      const cell = line[c] || { ch: ' ', attrs: null };
      const isCursor = isCursorRow && c === this.cursorX && this.cursorVisible;

      if (isCursor) {
        if (curSpanOpen) {
          out += '</span>';
          curSpanOpen = false;
        }
        out += `<span class="terminal-cursor">${this._escape(cell.ch || ' ')}</span>`;
        continue;
      }

      const attrs = cell.attrs;
      let spanClass = '';
      if (attrs) {
        const classes = [];
        if (attrs.bold) classes.push('s-bold');
        if (attrs.dim) classes.push('s-dim');
        if (attrs.italic) classes.push('s-italic');
        if (attrs.underline) classes.push('s-underline');
        if (attrs.inverse) classes.push('s-inverse');
        if (attrs.fg !== null && attrs.fg !== undefined) classes.push(`c-${attrs.fg % 16}`);
        if (attrs.bg !== null && attrs.bg !== undefined) classes.push(`bg-${attrs.bg % 8}`);
        spanClass = classes.join(' ');
      }

      if (spanClass !== curSpanClass) {
        if (curSpanOpen) out += '</span>';
        if (spanClass) {
          out += `<span class="${spanClass}">`;
          curSpanOpen = true;
        } else {
          curSpanOpen = false;
        }
        curSpanClass = spanClass;
      }

      out += this._escape(cell.ch);
    }

    if (curSpanOpen) out += '</span>';

    // If cursor is at the very end of line
    if (isCursorRow && this.cursorX >= colCount && this.cursorVisible) {
      out += '<span class="terminal-cursor">&nbsp;</span>';
    }

    out += '</div>';
    return out;
  }

  _escape(ch) {
    if (ch === '&') return '&amp;';
    if (ch === '<') return '&lt;';
    if (ch === '>') return '&gt;';
    if (ch === '"') return '&quot;';
    if (ch === "'") return '&#039;';
    return ch;
  }

  clear() {
    this.scrollback = [];
    this._initBuffer();
    this.cursorX = 0;
    this.cursorY = 0;
    this._render();
  }

  destroy() {
    this.explicitClose = true;
    this._stopPing();
    if (this.resizeObserver) {
      this.resizeObserver.disconnect();
    }
    if (this.ws) {
      this.ws.close();
      this.ws = null;
    }
  }
}

window.SmartTerminal = SmartTerminal;
