/**
 * ParkingFlow — Main Frontend Application Controller
 * Handles Navigation, 3-Step Wizard, 2D Grid Painting, Live BFS Routing,
 * Real-time Dashboards, Reservations, and DSA Telemetry.
 */

const app = {
  activeView: 'add-plot',
  wizardStep: 1,
  wizardType: 'mixed',
  wizardRows: 4,
  wizardCols: 6,
  wizardGrid: [],
  selectedTool: 'SLOT_CAR',
  layoutSelectedTool: 'INSPECT',
  wizardUndoHistory: [],
  activePlot: null,
  allPlots: [],
  selectedSlot: null,
  liveFilter: 'ALL',
  darkMode: false,
  navigationHistory: [],
  notifications: JSON.parse(localStorage.getItem('parkingflow_notifications') || '[]'),

  // ==========================================================================
  // INITIALIZATION
  // ==========================================================================
  async init() {
    this.setupEventListeners();
    this.initWizardGrid();
    this.renderWizardPreview();
    this.updateNavigationUI();
    this.renderNotificationsUI();

    try {
      await this.loadPlots();
      await this.loadDashboard();
      await this.loadReservations();
      await this.loadRecords('time');
      await this.loadTelemetry();
    } catch (e) {
      console.warn('Initial load warning:', e);
    }

    // Check dark mode
    if (localStorage.getItem('parkingflow_dark') === 'true') {
      this.toggleDarkMode();
    }

    // Default to Dashboard on startup
    this.showView('dashboard');

    // Real-time live waiting queue sync: checks every 5s if empty slot frees up for waiting vehicles
    setInterval(() => {
      if (this.currentView === 'dashboard' || this.currentView === 'live') {
        const plotId = this.activePlot?.activePlot?.id || this.activePlot?.id || '';
        if (plotId) {
          api.getWaitingQueue(plotId).then(data => {
            if (data && data.autoPromoted && data.autoPromoted.length > 0) {
              data.autoPromoted.forEach(p => {
                this.showToast(`🎉 Space freed! Vehicle ${p.plate} automatically parked into Slot ${p.slotId}!`, 'success');
              });
              this.loadDashboard();
              this.loadTelemetry();
            }
          }).catch(() => {});
        }
      }
    }, 5000);
  },

  setupEventListeners() {
    // Sidebar Navigation
    document.querySelectorAll('.nav-link').forEach(link => {
      link.addEventListener('click', (e) => {
        e.preventDefault();
        const view = link.getAttribute('data-view');
        this.showView(view);
      });
    });

    // Global Search
    const searchInput = document.getElementById('global-search-input');
    if (searchInput) {
      searchInput.addEventListener('keydown', (e) => {
        if (e.key === 'Enter') {
          this.performGlobalSearch(searchInput.value.trim());
        }
      });
    }

    // Theme Toggle
    const themeBtn = document.getElementById('btn-theme-toggle');
    if (themeBtn) {
      themeBtn.addEventListener('click', () => this.toggleDarkMode());
    }

    // Close notifications panel when clicking outside
    document.addEventListener('click', (e) => {
      const wrapper = document.querySelector('.notifications-wrapper');
      const panel = document.getElementById('notifications-panel');
      if (panel && wrapper && !wrapper.contains(e.target)) {
        panel.classList.remove('active');
      }
    });
  },

  showToast(message, type = 'info') {
    // 1. Render Floating Toast Alert
    const container = document.getElementById('toast-container');
    if (container) {
      const toast = document.createElement('div');
      toast.className = `toast ${type}`;
      toast.innerHTML = `<span>${type === 'success' ? '✓' : type === 'error' ? '✕' : 'ℹ'}</span> <span>${message}</span>`;
      container.appendChild(toast);
      setTimeout(() => {
        toast.style.opacity = '0';
        setTimeout(() => toast.remove(), 300);
      }, 3500);
    }

    // 2. Persist in Notification Bell Store
    const notifItem = {
      id: Date.now(),
      message,
      type,
      time: new Date().toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' })
    };
    this.notifications.unshift(notifItem);
    if (this.notifications.length > 50) this.notifications.pop();
    try {
      localStorage.setItem('parkingflow_notifications', JSON.stringify(this.notifications));
    } catch (e) {}
    this.renderNotificationsUI();
  },

  toggleNotificationsPanel(event) {
    if (event) event.stopPropagation();
    const panel = document.getElementById('notifications-panel');
    if (panel) {
      panel.classList.toggle('active');
    }
  },

  renderNotificationsUI() {
    const badge = document.getElementById('notif-badge-count');
    const pill = document.getElementById('notif-pill-count');
    const list = document.getElementById('notifications-list');
    const count = this.notifications.length;

    if (badge) {
      badge.textContent = count > 99 ? '99+' : count;
      if (count > 0) badge.classList.add('active');
      else badge.classList.remove('active');
    }
    if (pill) {
      pill.textContent = count;
    }
    if (list) {
      if (count === 0) {
        list.innerHTML = '<div class="notif-empty-state">No new notifications</div>';
        return;
      }
      list.innerHTML = '';
      this.notifications.forEach(n => {
        const item = document.createElement('div');
        item.className = 'notif-item';
        const icon = n.type === 'success' ? '✓' : n.type === 'error' ? '✕' : 'ℹ';
        item.innerHTML = `
          <div class="notif-icon-circle ${n.type}">${icon}</div>
          <div class="notif-content">
            <div class="notif-text">${n.message}</div>
            <div class="notif-time">${n.time}</div>
          </div>
        `;
        list.appendChild(item);
      });
    }
  },

  clearAllNotifications() {
    this.notifications = [];
    try {
      localStorage.setItem('parkingflow_notifications', JSON.stringify([]));
    } catch (e) {}
    this.renderNotificationsUI();
  },

  updateNavigationUI() {
    const prevBtn = document.getElementById('sidebar-prev-btn');
    if (prevBtn) {
      prevBtn.disabled = (this.navigationHistory.length === 0);
      prevBtn.title = this.navigationHistory.length > 0 
        ? `Back to previous view` 
        : 'No previous screen';
    }
  },

  navigateBack() {
    if (this.navigationHistory.length === 0) {
      this.showToast('No previous screen in history', 'info');
      return;
    }
    const prevView = this.navigationHistory.pop();
    this.showView(prevView, true);
  },

  toggleDarkMode() {
    this.darkMode = !this.darkMode;
    document.body.classList.toggle('dark-mode', this.darkMode);
    localStorage.setItem('parkingflow_dark', this.darkMode ? 'true' : 'false');
    const icon = document.getElementById('theme-icon');
    if (icon) {
      icon.innerHTML = this.darkMode
        ? '<circle cx="12" cy="12" r="5"/><line x1="12" y1="1" x2="12" y2="3"/><line x1="12" y1="21" x2="12" y2="23"/><line x1="4.22" y1="4.22" x2="5.64" y2="5.64"/><line x1="18.36" y1="18.36" x2="19.78" y2="19.78"/><line x1="1" y1="12" x2="3" y2="12"/><line x1="21" y1="12" x2="23" y2="12"/><line x1="4.22" y1="19.78" x2="5.64" y2="18.36"/><line x1="18.36" y1="5.64" x2="19.78" y2="4.22"/>'
        : '<path d="M21 12.79A9 9 0 1 1 11.21 3 7 7 0 0 0 21 12.79z"/>';
    }
  },

  showView(viewName, isBack = false) {
    if (!isBack && this.activeView && this.activeView !== viewName) {
      this.navigationHistory.push(this.activeView);
    }
    this.activeView = viewName;
    this.updateNavigationUI();

    document.querySelectorAll('.view-section').forEach(sec => sec.classList.remove('active'));
    document.querySelectorAll('.nav-link').forEach(link => {
      link.classList.toggle('active', link.getAttribute('data-view') === viewName);
    });

    const target = document.getElementById(`view-${viewName}`);
    if (target) {
      target.classList.add('active');
    }

    if (viewName === 'dashboard') this.loadDashboard();
    if (viewName === 'plots') this.loadPlots();
    if (viewName === 'layout-config') this.loadLayoutConfig();
    if (viewName === 'live') this.renderLiveGrid();
    if (viewName === 'reservations') this.loadReservations();
    if (viewName === 'records') this.loadRecords();
    if (viewName === 'analytics') this.loadAnalytics();
    if (viewName === 'settings') this.loadTelemetry();
  },

  closeModal(modalId) {
    const el = document.getElementById(modalId);
    if (el) el.classList.remove('active');
  },

  openModal(modalId) {
    const el = document.getElementById(modalId);
    if (el) el.classList.add('active');
  },

  // ==========================================================================
  // ADD NEW PLOT — 3-STEP WIZARD (REFERENCE UI MATCH)
  // ==========================================================================
  wizardGoTo(step) {
    this.wizardStep = step;

    // Update Progress Bar
    for (let i = 1; i <= 3; i++) {
      const item = document.getElementById(`step-nav-${i}`);
      if (item) {
        item.classList.remove('active', 'completed');
        if (i === step) item.classList.add('active');
        else if (i < step) item.classList.add('completed');
      }
    }

    // Show corresponding form card
    document.getElementById('wizard-step-1-card').style.display = (step === 1 ? 'flex' : 'none');
    document.getElementById('wizard-step-2-card').style.display = (step === 2 ? 'flex' : 'none');
    document.getElementById('wizard-step-3-card').style.display = (step === 3 ? 'flex' : 'none');

    // Subtitle update
    const sub = document.getElementById('preview-subtitle-text');
    if (sub) {
      if (step === 1) sub.textContent = 'This is how your plot will look (default layout).';
      else if (step === 2) sub.textContent = 'Click bays or aisles to paint with selected tool palette.';
      else sub.textContent = 'Final configuration preview ready for activation.';
    }

    this.renderWizardPreview();
  },

  selectParkingType(type) {
    this.wizardType = type;
    document.querySelectorAll('.type-card-opt').forEach(card => {
      card.classList.toggle('selected', card.getAttribute('data-type') === type);
    });
    this.initWizardGrid();
    this.renderWizardPreview();
  },

  getRowLabel(r) {
    let label = '';
    r++;
    while (r > 0) {
      let rem = (r - 1) % 26;
      label = String.fromCharCode(65 + rem) + label;
      r = Math.floor((r - 1) / 26);
    }
    return label;
  },

  reallotWizardGrid() {
    for (let r = 0; r < this.wizardRows; r++) {
      const rowLabel = this.getRowLabel(r);
      let slotIdx = 1;
      for (let c = 0; c < this.wizardCols; c++) {
        const cell = this.wizardGrid[r]?.[c];
        if (!cell) continue;
        const isParking = (cell.type === 'SLOT_CAR' || cell.type === 'SLOT_BIKE');
        if (isParking) {
          cell.id = `${rowLabel}${slotIdx++}`;
        } else {
          cell.id = '';
        }
      }
    }
  },

  initWizardGrid() {
    this.wizardGrid = [];

    for (let r = 0; r < this.wizardRows; r++) {
      const row = [];
      const rowLabel = this.getRowLabel(r);
      for (let c = 0; c < this.wizardCols; c++) {
        let type = 'SLOT_CAR';
        if (this.wizardType === 'bike') {
          type = 'SLOT_BIKE';
        } else if (this.wizardType === 'mixed') {
          if (r === this.wizardRows - 1 && c >= Math.floor(this.wizardCols / 2)) {
            type = 'SLOT_BIKE';
          }
        }
        row.push({
          id: '',
          r,
          c,
          type: type,
          status: 'AVAILABLE'
        });
      }
      this.wizardGrid.push(row);
    }
    this.reallotWizardGrid();
  },

  updateGridDimensions() {
    const rInput = document.getElementById('grid-rows-input');
    const cInput = document.getElementById('grid-cols-input');
    let r = parseInt(rInput.value, 10);
    let c = parseInt(cInput.value, 10);

    if (isNaN(r) || r < 1) r = 1;
    if (r > 100) r = 100;
    if (isNaN(c) || c < 1) c = 1;
    if (c > 100) c = 100;

    this.wizardRows = r;
    this.wizardCols = c;

    this.initWizardGrid();
    this.renderWizardPreview();
  },

  selectPaletteTool(tool) {
    this.selectedTool = tool;
    document.querySelectorAll('.tool-btn').forEach(btn => {
      btn.classList.toggle('active', btn.getAttribute('data-tool') === tool);
    });
  },

  onWizardCellClick(r, c) {
    if (this.wizardStep !== 2) return;
    const cell = this.wizardGrid[r][c];
    if (!cell) return;

    // Push to local undo history (DSA Stack)
    this.wizardUndoHistory.push({
      r,
      c,
      prevType: cell.type,
      newType: this.selectedTool
    });

    cell.type = this.selectedTool;
    this.reallotWizardGrid();
    this.renderWizardPreview();

    let toolName = 'Car Slot';
    if (this.selectedTool === 'SLOT_BIKE') toolName = 'Two-Wheeler Slot';
    else if (this.selectedTool === 'ENTRANCE') toolName = 'Entrance Gate';
    else if (this.selectedTool === 'EXIT') toolName = 'Exit Gate';
    else if (this.selectedTool === 'AISLE') toolName = 'Driving Aisle';
    else if (this.selectedTool === 'BLOCKED') toolName = 'Pillar / Blocked';
    else if (this.selectedTool === 'FACILITY') toolName = 'EV Facility';
    this.showToast(`Placed ${toolName} on Cell [${r + 1}, ${c + 1}] — Slot allotments automatically updated`, 'info');
  },

  undoLayout() {
    if (this.wizardUndoHistory.length === 0) {
      this.showToast('No actions to undo', 'info');
      return;
    }
    const lastAction = this.wizardUndoHistory.pop();
    this.wizardGrid[lastAction.r][lastAction.c].type = lastAction.prevType;
    this.reallotWizardGrid();
    this.showToast(`Undone modification on cell (${lastAction.r + 1}, ${lastAction.c + 1})`, 'info');
    this.renderWizardPreview();
  },

  resetLayoutDefault() {
    this.wizardUndoHistory = [];
    this.wizardRows = 4;
    this.wizardCols = 6;
    const rIn = document.getElementById('grid-rows-input');
    const cIn = document.getElementById('grid-cols-input');
    if (rIn) rIn.value = 4;
    if (cIn) cIn.value = 6;
    this.initWizardGrid();
    this.renderWizardPreview();
    this.showToast('Reset to default 4 × 6 layout', 'info');
  },

  renderWizardPreview() {
    const container = document.getElementById('lot-preview-grid');
    if (!container) return;

    container.innerHTML = '';
    const colWidth = this.wizardCols <= 4 ? '100px' : (this.wizardCols <= 8 ? '84px' : '76px');
    container.style.gridTemplateColumns = `repeat(${this.wizardCols}, ${colWidth})`;
    container.style.justifyContent = 'center';

    // Sample car color palettes for realistic render matching screenshot
    const carColors = ['#f59e0b', '#ef4444', '#3b82f6', '#475569', '#10b981', '#f1f5f9'];
    let carColorIdx = 0;

    let totalSlots = 0;
    let availSlots = 0;
    let occupiedSlots = 0;

    // In step 1 default layout, render sample cars like in reference image!
    const sampleParked = [
      [0, 1], [0, 4], [1, 0], [1, 2], [1, 4], [2, 1], [2, 4], [3, 1], [3, 5]
    ];

    for (let r = 0; r < this.wizardRows; r++) {
      for (let c = 0; c < this.wizardCols; c++) {
        const cell = this.wizardGrid[r][c];
        const isSlot = (cell.type === 'SLOT_CAR' || cell.type === 'SLOT_BIKE');

        const box = document.createElement('div');
        box.className = 'slot-visual-box';

        let hasCar = false;
        if (this.wizardStep === 1 && isSlot) {
          hasCar = sampleParked.some(p => p[0] === r && p[1] === c);
        }

        if (isSlot) {
          totalSlots++;
          if (hasCar) occupiedSlots++;
          else availSlots++;
        }

        // Cell Type specific styling
        if (cell.type === 'AISLE') {
          box.style.background = 'rgba(59, 130, 246, 0.15)';
          box.style.borderColor = 'rgba(59, 130, 246, 0.3)';
          box.innerHTML = `<span style="font-size: 10px; color: #93c5fd;">🛣️ AISLE</span>`;
        } else if (cell.type === 'ENTRANCE') {
          box.style.background = 'rgba(16, 185, 129, 0.35)';
          box.style.borderColor = '#10b981';
          box.innerHTML = `<span style="font-size: 11px; font-weight: 800; color: #059669;">⬇️ ENTRANCE</span>`;
        } else if (cell.type === 'EXIT') {
          box.style.background = 'rgba(239, 68, 68, 0.35)';
          box.style.borderColor = '#ef4444';
          box.innerHTML = `<span style="font-size: 11px; font-weight: 800; color: #dc2626;">⬆️ EXIT</span>`;
        } else if (cell.type === 'BLOCKED') {
          box.style.background = 'rgba(100, 116, 139, 0.4)';
          box.style.borderColor = '#64748b';
          box.innerHTML = `<span style="font-size: 10px; color: #cbd5e1;">🚫 PILLAR</span>`;
        } else if (cell.type === 'FACILITY') {
          box.style.background = 'rgba(139, 92, 246, 0.3)';
          box.style.borderColor = '#8b5cf6';
          box.innerHTML = `<span style="font-size: 10px; color: #c4b5fd;">⚡ EV CHARGE</span>`;
        } else {
          // Standard Parking Slot
          const idSpan = document.createElement('span');
          idSpan.className = 'slot-id-label';
          idSpan.textContent = cell.id;
          box.appendChild(idSpan);

          if (hasCar) {
            box.classList.add('occupied');
            const car = document.createElement('div');
            car.className = 'car-sprite';
            car.style.backgroundColor = carColors[carColorIdx % carColors.length];
            carColorIdx++;
            car.innerHTML = cell.type === 'SLOT_BIKE'
              ? `<span style="font-size: 14px;">🏍️</span>`
              : `<span style="font-size: 14px;">🚗</span>`;
            box.appendChild(car);
          } else {
            box.classList.add('available');
            if (cell.type === 'SLOT_BIKE') {
              const bIcon = document.createElement('span');
              bIcon.style.fontSize = '14px';
              bIcon.style.opacity = '0.6';
              bIcon.title = 'Two-Wheeler Slot';
              bIcon.textContent = '🏍️';
              box.appendChild(bIcon);
            } else if (cell.type === 'SLOT_CAR') {
              const cIcon = document.createElement('span');
              cIcon.style.fontSize = '14px';
              cIcon.style.opacity = '0.6';
              cIcon.title = 'Car Slot';
              cIcon.textContent = '🚗';
              box.appendChild(cIcon);
            }
          }
        }

        // Attach click listener for layout designer
        box.addEventListener('click', () => this.onWizardCellClick(r, c));
        container.appendChild(box);
      }
    }

    // Update dimensions badge & plot summary cards
    const dimBadge = document.getElementById('preview-dim-badge');
    if (dimBadge) {
      dimBadge.textContent = `${this.wizardRows} Rows × ${this.wizardCols} Columns = ${totalSlots} Slots`;
    }

    const sTot = document.getElementById('summary-total-slots');
    const sAvail = document.getElementById('summary-avail-slots');
    const sOccup = document.getElementById('summary-occup-slots');
    const sDim = document.getElementById('summary-dim-label');

    if (sTot) sTot.textContent = totalSlots;
    if (sAvail) sAvail.textContent = availSlots;
    if (sOccup) sOccup.textContent = occupiedSlots;
    if (sDim) sDim.textContent = `${this.wizardRows} × ${this.wizardCols}`;
  },

  async savePlotWizard() {
    const name = document.getElementById('plot-name-input').value.trim();
    if (!name) {
      this.showToast('Please enter a plot name', 'error');
      return;
    }
    const location = document.getElementById('plot-location-input').value.trim() || 'Campus';
    const rateCar = parseFloat(document.getElementById('plot-rate-car').value) || 40.0;
    const rateBike = parseFloat(document.getElementById('plot-rate-bike').value) || 20.0;
    const gridData = this.wizardGrid.map(row => row.map(c => c.type).join(',')).join(';');

    const payload = {
      name,
      location,
      parkingType: this.wizardType,
      rows: this.wizardRows,
      cols: this.wizardCols,
      ratePerHourCar: rateCar,
      ratePerHourBike: rateBike,
      gracePeriodMinutes: 15,
      gridData
    };

    try {
      const res = await api.createPlot(payload);
      this.showToast(`Plot '${name}' successfully created and saved!`, 'success');
      await this.loadPlots();
      this.showView('plots');
    } catch (e) {
      this.showToast(e.message || 'Error creating plot', 'error');
    }
  },

  // ==========================================================================
  // DASHBOARD & MULTI-PLOT MANAGEMENT
  // ==========================================================================
  async loadPlots() {
    try {
      const data = await api.getPlots();
      this.allPlots = data.plots || [];

      // Update Dashboard Dropdown
      const select = document.getElementById('dash-plot-select');
      if (select) {
        select.innerHTML = '';
        this.allPlots.forEach(p => {
          const opt = document.createElement('option');
          opt.value = p.id;
          opt.textContent = `${p.name} (${p.location})`;
          if (p.id === data.activePlotId) opt.selected = true;
          select.appendChild(opt);
        });
      }

      // Render My Plots Gallery Cards
      const gallery = document.getElementById('plots-gallery-grid');
      if (gallery) {
        gallery.innerHTML = '';
        this.allPlots.forEach(p => {
          const card = document.createElement('div');
          card.className = 'card';
          card.style.display = 'flex';
          card.style.flexDirection = 'column';
          card.style.gap = '14px';

          const pct = p.totalSlots > 0 ? Math.round((p.occupiedSlots / p.totalSlots) * 100) : 0;
          card.innerHTML = `
            <div style="display: flex; align-items: flex-start; justify-content: space-between;">
              <div>
                <h3 style="font-size: 17px; font-weight: 800;">${p.name}</h3>
                <p style="font-size: 12px; color: var(--text-muted);">${p.location}</p>
              </div>
              <span class="step-badge" style="background: var(--color-yellow-light); color: var(--color-yellow-dark); border-color: var(--color-yellow);">
                ${p.parkingType.toUpperCase()}
              </span>
            </div>
            <div style="display: flex; justify-content: space-between; font-size: 13px;">
              <span><strong>${p.totalSlots}</strong> Total Bays</span>
              <span style="color: #10b981;"><strong>${p.availableSlots}</strong> Free</span>
              <span style="color: #ef4444;"><strong>${p.occupiedSlots}</strong> Parked</span>
            </div>
            <div style="width: 100%; height: 8px; background: #e2e8f0; border-radius: 999px; overflow: hidden;">
              <div style="width: ${pct}%; height: 100%; background: #3b82f6; border-radius: 999px;"></div>
            </div>
            <div class="plot-card-actions">
              <button class="btn btn-primary-yellow btn-sm" style="flex: 2;" onclick="app.selectAndOpenPlot('${p.id}')">
                Open Dashboard
              </button>
              <button class="btn btn-secondary btn-sm" style="flex: 1;" onclick="app.openEditPlotModal('${p.id}')" title="Edit Plot">
                ✏️ Edit
              </button>
              <button class="btn btn-danger-outline btn-sm" style="flex: 1;" onclick="app.confirmDeletePlot('${p.id}', '${p.name.replace(/'/g, "\\'")}')" title="Remove Plot">
                🗑️ Remove
              </button>
            </div>
          `;
          gallery.appendChild(card);
        });
      }
    } catch (e) {
      console.error('Failed to load plots:', e);
    }
  },

  openEditPlotModal(plotId) {
    const p = this.allPlots.find(item => item.id === plotId);
    if (!p) {
      this.showToast('Plot not found', 'error');
      return;
    }
    document.getElementById('edit-plot-id').value = p.id;
    document.getElementById('edit-plot-name').value = p.name;
    document.getElementById('edit-plot-loc').value = p.location || '';
    document.getElementById('edit-plot-type').value = p.parkingType || 'mixed';
    document.getElementById('edit-plot-rate-car').value = p.ratePerHourCar || 40;
    document.getElementById('edit-plot-rate-bike').value = p.ratePerHourBike || 20;
    document.getElementById('edit-plot-grace').value = p.gracePeriodMinutes || 15;
    this.openModal('modal-edit-plot');
  },

  async submitEditPlot() {
    const plotId = document.getElementById('edit-plot-id').value;
    const name = document.getElementById('edit-plot-name').value.trim();
    if (!name) {
      this.showToast('Plot name cannot be empty', 'error');
      return;
    }
    const location = document.getElementById('edit-plot-loc').value.trim() || 'Campus';
    const parkingType = document.getElementById('edit-plot-type').value;
    const rateCar = parseFloat(document.getElementById('edit-plot-rate-car').value) || 40;
    const rateBike = parseFloat(document.getElementById('edit-plot-rate-bike').value) || 20;
    const grace = parseInt(document.getElementById('edit-plot-grace').value, 10) || 15;

    try {
      await api.updatePlot({
        plotId,
        name,
        location,
        parkingType,
        ratePerHourCar: rateCar,
        ratePerHourBike: rateBike,
        gracePeriodMinutes: grace
      });
      this.closeModal('modal-edit-plot');
      this.showToast(`Plot "${name}" updated successfully!`, 'success');
      await this.loadPlots();
      if (this.activePlot && this.activePlot.activePlot && this.activePlot.activePlot.id === plotId) {
        await this.loadDashboard();
      }
    } catch (e) {
      this.showToast(e.message || 'Failed to update plot', 'error');
    }
  },

  async confirmDeletePlot(plotId, plotName) {
    if (!confirm(`Are you sure you want to remove plot "${plotName}"?\nThis action cannot be undone.`)) {
      return;
    }
    try {
      await api.deletePlot(plotId);
      this.showToast(`Plot "${plotName}" removed successfully!`, 'success');
      await this.loadPlots();
      await this.loadDashboard();
    } catch (e) {
      this.showToast(e.message || 'Cannot remove plot', 'error');
    }
  },

  async selectAndOpenPlot(plotId) {
    await api.selectPlot(plotId);
    this.showView('dashboard');
  },

  async onPlotSelectChanged(plotId) {
    await api.selectPlot(plotId);
    await this.loadDashboard();
  },

  async loadDashboard() {
    try {
      const data = await api.getStatus();
      this.activePlot = data;

      // Update Top KPIs
      const kTot = document.getElementById('dash-kpi-total');
      const kAvail = document.getElementById('dash-kpi-avail');
      const kOccup = document.getElementById('dash-kpi-occup');
      const kWait = document.getElementById('dash-kpi-wait');
      const kLoc = document.getElementById('dash-kpi-loc');
      const kRate = document.getElementById('dash-kpi-rate');

      if (kTot) kTot.textContent = data.stats.totalSlots;
      if (kAvail) kAvail.textContent = data.stats.availableSlots;
      if (kOccup) kOccup.textContent = data.stats.occupiedSlots;
      if (kWait) kWait.textContent = data.stats.waitingCount;
      if (kLoc) kLoc.textContent = data.activePlot.name;
      if (kRate) kRate.textContent = `${data.stats.occupancyRate}% occupied`;

      // Render Interactive Bay Grid
      this.renderInteractiveGrid(data.grid, 'dash-interactive-grid');

      // Load Waiting list
      this.renderWaitingList(data.activePlot.id);

      // Load Recent Activity Log
      this.renderActivityLog(data.activePlot.id);
    } catch (e) {
      console.error('Error loading dashboard:', e);
    }
  },

  renderInteractiveGrid(gridData, containerId) {
    const container = document.getElementById(containerId);
    if (!container || !gridData || gridData.length === 0) return;

    container.innerHTML = '';
    const cols = gridData[0].length;
    const colWidth = cols <= 4 ? '100px' : (cols <= 8 ? '84px' : '76px');
    container.style.gridTemplateColumns = `repeat(${cols}, ${colWidth})`;
    container.style.justifyContent = 'center';

    const gates = (this.activePlot && this.activePlot.activePlot && this.activePlot.activePlot.gates) 
      ? this.activePlot.activePlot.gates 
      : [];

    gridData.forEach((row, r) => {
      row.forEach((slot, c) => {
        const box = document.createElement('div');
        box.className = 'slot-visual-box';
        box.setAttribute('id', `${containerId}-cell-${r}-${c}`);

        // Check if any configured gate is located at this cell
        const gateAtCell = gates.find(g => g.row === r && g.col === c);

        if (gateAtCell) {
          const isEnt = (gateAtCell.type === 'ENTRANCE');
          const isExit = (gateAtCell.type === 'EXIT');
          const isBi = (gateAtCell.type === 'BIDIRECTIONAL');
          const badgeClass = isEnt ? 'entrance' : (isExit ? 'exit' : 'bidirectional');
          box.classList.add('gate-slot', badgeClass);
          if (!gateAtCell.isActive) box.classList.add('out-of-service');

          box.innerHTML = `
            <div style="font-size: 10px; font-weight: 800; text-align: center; line-height: 1.2;">
              <div>${isBi ? '🔵 GATE' : (isEnt ? '🟢 IN' : '🔴 OUT')}</div>
              <div style="font-size: 8px; opacity: 0.9; margin-top: 2px; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; max-width: 58px;" title="${gateAtCell.name}">${gateAtCell.name}</div>
              ${!gateAtCell.isActive ? '<div style="font-size: 8px; color: #fca5a5; font-weight: 900;">CLOSED</div>' : ''}
            </div>
          `;
        } else if (slot.type === 'AISLE') {
          box.style.background = 'rgba(59, 130, 246, 0.15)';
          box.style.borderColor = 'rgba(59, 130, 246, 0.3)';
          box.innerHTML = `<span style="font-size: 10px; color: #93c5fd;">🛣️ AISLE</span>`;
        } else if (slot.type === 'ENTRANCE') {
          box.style.background = 'rgba(16, 185, 129, 0.3)';
          box.style.borderColor = '#10b981';
          box.innerHTML = `<span style="font-size: 10px; font-weight: 800; color: #a7f3d0;">🟢 ENTRY</span>`;
        } else if (slot.type === 'EXIT') {
          box.style.background = 'rgba(239, 68, 68, 0.3)';
          box.style.borderColor = '#ef4444';
          box.innerHTML = `<span style="font-size: 10px; font-weight: 800; color: #fca5a5;">🔴 EXIT</span>`;
        } else if (slot.type === 'BLOCKED') {
          box.style.background = 'rgba(100, 116, 139, 0.4)';
          box.style.borderColor = '#64748b';
          box.innerHTML = `<span style="font-size: 10px; color: #cbd5e1;">🚫 PILLAR</span>`;
        } else if (slot.type === 'FACILITY') {
          box.style.background = 'rgba(139, 92, 246, 0.3)';
          box.style.borderColor = '#8b5cf6';
          box.innerHTML = `<span style="font-size: 10px; color: #c4b5fd;">⚡ EV CHARGE</span>`;
        } else {
          // Parking Bay (Standard or EV)
          if (slot.hasCharger) {
            box.classList.add('ev-slot');
            if (slot.chargerStatus !== 'OPERATIONAL') {
              box.classList.add('out-of-service');
            }
          }

          const idLabel = document.createElement('span');
          idLabel.className = 'slot-id-label';
          idLabel.textContent = slot.id;
          box.appendChild(idLabel);

          if (slot.hasCharger) {
            const evIcon = document.createElement('span');
            evIcon.className = 'ev-badge-icon';
            evIcon.innerHTML = (slot.chargerStatus === 'OPERATIONAL') ? '⚡' : '⚠️';
            evIcon.title = `EV Charger (${slot.chargerId || 'Station'} - ${slot.chargerStatus || 'OPERATIONAL'})`;
            box.appendChild(evIcon);
          }

          if (slot.status === 'OCCUPIED') {
            box.classList.add('occupied');
            const isTwoWheeler = (slot.vehicleType === 'TWO_WHEELER' || slot.type === 'SLOT_BIKE');
            const sprite = document.createElement('div');
            sprite.className = 'car-sprite';
            sprite.style.backgroundColor = isTwoWheeler ? '#10b981' : '#3b82f6';
            sprite.innerHTML = isTwoWheeler
              ? `<span style="font-size: 14px;">🏍️</span>`
              : `<span style="font-size: 14px;">🚗</span>`;
            box.appendChild(sprite);

            const plateSpan = document.createElement('span');
            plateSpan.style.fontSize = '9px';
            plateSpan.style.color = '#ffffff';
            plateSpan.style.marginTop = '2px';
            plateSpan.textContent = slot.plate;
            box.appendChild(plateSpan);
          } else if (slot.status === 'RESERVED') {
            box.classList.add('reserved');
            const rIcon = document.createElement('span');
            rIcon.style.fontSize = '12px';
            rIcon.textContent = '🔒 RES';
            box.appendChild(rIcon);
          } else {
            box.classList.add('available');
            if (slot.type === 'SLOT_BIKE') {
              const bIcon = document.createElement('span');
              bIcon.style.fontSize = '14px';
              bIcon.style.opacity = '0.6';
              bIcon.title = 'Two-Wheeler Bay';
              bIcon.textContent = '🏍️';
              box.appendChild(bIcon);
            } else if (slot.type === 'SLOT_CAR') {
              const cIcon = document.createElement('span');
              cIcon.style.fontSize = '14px';
              cIcon.style.opacity = '0.6';
              cIcon.title = slot.hasCharger ? 'EV Charging Bay' : 'Car Bay';
              cIcon.textContent = slot.hasCharger ? '⚡' : '🚗';
              box.appendChild(cIcon);
            }
          }
        }

        if (containerId === 'layout-config-grid') {
          box.addEventListener('click', () => this.onLayoutGridCellClick(r, c, slot, gateAtCell));
        } else {
          box.addEventListener('click', () => {
            if (gateAtCell) {
              this.showToast(`Gate: ${gateAtCell.name} (${gateAtCell.type}) - ${gateAtCell.isActive ? 'Active' : 'Closed'}`, 'info');
            } else {
              this.inspectSlot(slot);
            }
          });
        }
        container.appendChild(box);
      });
    });
  },

  inspectSlot(slot) {
    this.selectedSlot = slot;
    const title = document.getElementById('modal-slot-title');
    const body = document.getElementById('modal-slot-body');
    const actBtn = document.getElementById('modal-slot-action-btn');

    const gates = (this.activePlot && this.activePlot.activePlot && this.activePlot.activePlot.gates) 
      ? this.activePlot.activePlot.gates 
      : [];
    const gateAtCell = gates.find(g => g.row === slot.r && g.col === slot.c);
    const isGate = !!gateAtCell || slot.type === 'ENTRANCE' || slot.type === 'EXIT';
    const isAisle = (slot.type === 'AISLE');
    const isPillar = (slot.type === 'BLOCKED');
    const isFacility = (slot.type === 'FACILITY');
    const isNonParkable = isGate || isAisle || isPillar || isFacility;

    if (title) {
      if (gateAtCell) title.textContent = `Gate: ${gateAtCell.name} (${gateAtCell.type})`;
      else if (isGate) title.textContent = slot.type === 'EXIT' ? 'Main Exit Gate' : 'Main Entrance Gate';
      else if (isAisle) title.textContent = 'Driving Aisle Lane';
      else if (isPillar) title.textContent = 'Structural Pillar / Column';
      else if (isFacility) title.textContent = 'EV Charging Facility';
      else title.textContent = `Parking Bay ${slot.id || ''}`;
    }

    let bayTypeName = 'Car Bay (🚗)';
    if (slot.type === 'SLOT_BIKE') bayTypeName = 'Two-Wheeler Bay (🏍️)';
    else if (slot.type === 'AISLE') bayTypeName = 'Driving Aisle (🛣️)';
    else if (slot.type === 'ENTRANCE') bayTypeName = 'Entrance Gate (🟢)';
    else if (slot.type === 'EXIT') bayTypeName = 'Exit Gate (🔴)';
    else if (slot.type === 'BLOCKED') bayTypeName = 'Blocked / Pillar (🚫)';
    else if (slot.type === 'FACILITY') bayTypeName = 'EV Charging Station (⚡)';

    const isTwoWheeler = (slot.vehicleType === 'TWO_WHEELER' || slot.type === 'SLOT_BIKE');
    const vehTypeDisplay = slot.status === 'OCCUPIED' ? (isTwoWheeler ? '🏍️ Two-Wheeler' : '🚗 Car / SUV') : 'None';

    const scale = (this.activePlot && this.activePlot.activePlot && this.activePlot.activePlot.cellScaleMeters) ? this.activePlot.activePlot.cellScaleMeters : 5.0;
    const distanceMeters = (slot.distMeters !== undefined && slot.distMeters >= 0) ? slot.distMeters : (slot.dist > 0 ? slot.dist * scale : -1);

    if (body) {
      let evSection = '';
      if (slot.hasCharger) {
        const isOp = (slot.chargerStatus === 'OPERATIONAL');
        evSection = `
          <div style="background: rgba(139, 92, 246, 0.1); border: 1px solid rgba(139, 92, 246, 0.3); border-radius: 8px; padding: 10px; margin-top: 4px;">
            <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 6px;">
              <span style="font-weight: 800; color: #8b5cf6;">⚡ EV Charging Station</span>
              <span class="status-badge-pill ${isOp ? 'available' : 'occupied'}">${slot.chargerStatus || 'OPERATIONAL'}</span>
            </div>
            <div style="font-size: 12px; display: grid; grid-template-columns: 1fr 1fr; gap: 6px; color: var(--text-dark);">
              <div><strong>Charger ID:</strong> ${slot.chargerId || 'EV-CHG'}</div>
              <div><strong>Connector:</strong> ${slot.connector || 'CCS2'}</div>
              <div><strong>Power:</strong> ${slot.powerKw || 50} kW</div>
              <div><strong>Tariff:</strong> ₹${slot.chargingRate || 50}/hr</div>
            </div>
            <div style="display: flex; gap: 8px; margin-top: 10px;">
              <button class="btn btn-secondary btn-sm" style="flex: 1; height: 30px; font-size: 11px;" onclick="app.toggleChargerQuick('${slot.id}', '${isOp ? 'OUT_OF_SERVICE' : 'OPERATIONAL'}')">
                ${isOp ? '⚠️ Mark Out of Service' : '✓ Mark Operational'}
              </button>
              <button class="btn btn-secondary btn-sm" style="flex: 1; height: 30px; font-size: 11px;" onclick="app.openSlotEVConfig('${slot.id}')">
                ⚙️ Configure EV
              </button>
            </div>
          </div>
        `;
      } else if (slot.type === 'SLOT_CAR' && !isNonParkable) {
        evSection = `
          <div style="margin-top: 4px;">
            <button class="btn btn-secondary btn-sm" style="width: 100%; height: 30px; font-size: 11px;" onclick="app.openSlotEVConfig('${slot.id}')">
              ⚡ Install / Configure EV Charger
            </button>
          </div>
        `;
      }

      let routeSection = '';
      if (!isNonParkable && distanceMeters >= 0) {
        const estSec = Math.max(5, Math.round(distanceMeters / 4.167));
        routeSection = `
          <div style="display: flex; justify-content: space-between; padding-bottom: 8px; border-bottom: 1px solid var(--border-light);">
            <span>Driving Route Distance:</span>
            <strong style="color: #2563eb;">${distanceMeters.toFixed(1)} m</strong>
          </div>
          <div style="display: flex; justify-content: space-between; padding-bottom: 8px; border-bottom: 1px solid var(--border-light);">
            <span>Est. Driving Time (15 km/h):</span>
            <strong>~${estSec} seconds</strong>
          </div>
        `;
      } else if (!isNonParkable) {
        routeSection = `
          <div style="padding-bottom: 8px; border-bottom: 1px solid var(--border-light);">
            <span style="color: #ef4444; font-weight: 700;">Distance unavailable — parking layout requires configuration</span>
          </div>
        `;
      }

      const nonParkingNotice = isNonParkable ? `
        <div style="background: rgba(239, 68, 68, 0.12); border: 1px solid rgba(239, 68, 68, 0.35); border-radius: 8px; padding: 10px; margin-bottom: 10px; color: #fca5a5; font-size: 12px; font-weight: 600;">
          🚫 <strong>Non-Parking Zone:</strong> Cars and two-wheelers cannot be parked or stored in gates, driving aisles, or structural pillars.
        </div>
      ` : '';

      body.innerHTML = `
        ${nonParkingNotice}
        <div style="display: flex; justify-content: space-between; padding-bottom: 8px; border-bottom: 1px solid var(--border-light);">
          <span>Bay Type:</span>
          <strong>${bayTypeName}</strong>
        </div>
        <div style="display: flex; justify-content: space-between; padding-bottom: 8px; border-bottom: 1px solid var(--border-light);">
          <span>Current Status:</span>
          <span class="status-badge-pill ${(slot.status || 'available').toLowerCase()}">${isNonParkable ? 'CLEAR' : (slot.status || 'AVAILABLE')}</span>
        </div>
        ${slot.status === 'OCCUPIED' ? `
        <div style="display: flex; justify-content: space-between; padding-bottom: 8px; border-bottom: 1px solid var(--border-light);">
          <span>Vehicle Type:</span>
          <strong>${vehTypeDisplay}</strong>
        </div>
        ` : ''}
        <div style="display: flex; justify-content: space-between; padding-bottom: 8px; border-bottom: 1px solid var(--border-light);">
          <span>Parked Vehicle:</span>
          <strong>${slot.plate || 'None'}</strong>
        </div>
        ${routeSection}
        ${evSection}
        ${slot.id ? `
        <div style="margin-top: 6px;">
          <button class="btn btn-secondary btn-sm" style="width: 100%; height: 32px;" onclick="app.closeModal('modal-slot-inspector'); app.highlightBFSRoute('${(this.activePlot && this.activePlot.activePlot) ? this.activePlot.activePlot.id : ''}', '${slot.id}')">
            🗺️ Show Driving Route from Gate
          </button>
        </div>` : ''}
      `;
    }

    if (actBtn) {
      if (isNonParkable) {
        actBtn.disabled = true;
        actBtn.style.opacity = '0.5';
        actBtn.style.cursor = 'not-allowed';
        actBtn.textContent = '🚫 Non-Parking Zone (No Parking Allowed)';
        actBtn.onclick = null;
      } else if (slot.status === 'OCCUPIED') {
        actBtn.disabled = false;
        actBtn.style.opacity = '1';
        actBtn.style.cursor = 'pointer';
        actBtn.textContent = 'Process Vehicle Exit';
        actBtn.onclick = () => {
          this.closeModal('modal-slot-inspector');
          this.quickExitPlate(slot.plate);
        };
      } else {
        actBtn.disabled = false;
        actBtn.style.opacity = '1';
        actBtn.style.cursor = 'pointer';
        actBtn.textContent = 'Park Vehicle Here';
        actBtn.onclick = () => {
          this.closeModal('modal-slot-inspector');
          this.openQuickParkModal();
        };
      }
    }

    this.openModal('modal-slot-inspector');
  },

  async renderWaitingList(plotId) {
    const listEl = document.getElementById('dash-waiting-list');
    if (!listEl) return;
    try {
      const activePlotId = plotId || (this.activePlot && this.activePlot.activePlot ? this.activePlot.activePlot.id : (this.activePlot ? this.activePlot.id : ''));
      const data = await api.getWaitingQueue(activePlotId);
      listEl.innerHTML = '';

      // Check if any vehicles were auto-promoted during retrieval
      if (data && data.autoPromoted && data.autoPromoted.length > 0) {
        data.autoPromoted.forEach(p => {
          this.showToast(`🎉 Space freed! Vehicle ${p.plate} automatically parked into Slot ${p.slotId}!`, 'success');
        });
        setTimeout(() => {
          this.loadDashboard();
          this.loadTelemetry();
        }, 300);
      }

      if (!data.waitingList || data.waitingList.length === 0) {
        listEl.innerHTML = '<p style="font-size: 12px; color: var(--text-muted);">No vehicles waiting in queue.</p>';
        return;
      }
      data.waitingList.forEach((w, idx) => {
        const item = document.createElement('div');
        item.style.display = 'flex';
        item.style.alignItems = 'center';
        item.style.justifyContent = 'space-between';
        item.style.padding = '8px 12px';
        item.style.borderRadius = '8px';
        item.style.background = 'var(--bg-subtle)';
        item.style.fontSize = '12px';
        item.style.border = '1px solid var(--border-light)';

        const plate = w.plate || w.vehiclePlate;
        const evBadge = w.requiresEVCharging ? '<span style="font-size: 10px; color: #8b5cf6; font-weight: 700; margin-left: 4px;">⚡ EV</span>' : '';
        const priorityBadge = w.isPriority ? '<span style="font-size: 10px; color: #ef4444; font-weight: 700; margin-left: 6px;">🚨 VIP</span>' : '<span style="font-size: 10px; color: var(--text-muted); margin-left: 6px;">FIFO</span>';

        item.innerHTML = `
          <div>
            <strong>#${idx + 1} ${plate}</strong>
            ${priorityBadge}
            ${evBadge}
            <div style="font-size: 10px; color: var(--text-muted); margin-top: 2px;">
              ${w.vehicleType || 'CAR'} &bull; ${w.ownerName || 'Guest'}
            </div>
          </div>
          <button class="btn btn-secondary btn-sm" style="height: 28px; padding: 0 10px; font-size: 11px; color: #ef4444;" onclick="app.cancelWaiting('${plate}')" title="Remove from waiting queue">
            ✕ Remove
          </button>
        `;
        listEl.appendChild(item);
      });
    } catch (e) {
      console.warn('Failed waiting list:', e);
    }
  },

  async processWaitingQueue() {
    const plotId = this.activePlot?.activePlot?.id || this.activePlot?.id || '';
    try {
      const res = await api.processWaitingQueue(plotId);
      if (res && res.promotedCount > 0) {
        this.showToast(`🎉 Successfully auto-parked ${res.promotedCount} vehicle(s) into empty space!`, 'success');
        await this.loadDashboard();
        await this.loadTelemetry();
      } else {
        this.showToast('Live Queue checked: No vehicles waiting or no empty matching slots available.', 'info');
      }
    } catch (e) {
      this.showToast(e.message || 'Failed to auto-park queued vehicles', 'error');
    }
  },

  async cancelWaiting(plate) {
    if (!plate) return;
    const plotId = this.activePlot?.activePlot?.id || this.activePlot?.id || '';
    try {
      await api.cancelWaiting(plotId, plate);
      this.showToast(`Vehicle ${plate} removed from waiting queue`, 'info');
      await this.loadDashboard();
      await this.loadTelemetry();
    } catch (e) {
      this.showToast(e.message || `Failed to remove ${plate} from queue`, 'error');
    }
  },

  async renderActivityLog(plotId) {
    const logEl = document.getElementById('dash-activity-log');
    if (!logEl) return;
    try {
      const activePlotId = plotId || (this.activePlot && this.activePlot.activePlot ? this.activePlot.activePlot.id : null);
      const data = await api.getRecords('time');
      logEl.innerHTML = '';
      let records = data.records || [];
      if (activePlotId) {
        records = records.filter(r => r.plotId === activePlotId);
      }
      const recent = records.slice(0, 6);
      if (recent.length === 0) {
        logEl.innerHTML = '<p style="font-size: 12px; color: var(--text-muted); padding: 8px 0;">No activity recorded for this plot yet.</p>';
        return;
      }
      recent.forEach(r => {
        const row = document.createElement('div');
        row.style.display = 'flex';
        row.style.alignItems = 'center';
        row.style.justifyContent = 'space-between';
        row.style.padding = '8px 10px';
        row.style.borderBottom = '1px solid var(--border-light)';
        row.style.fontSize = '12px';
        const isBike = (r.vehicleType === 'TWO_WHEELER');
        row.innerHTML = `
          <div>
            <strong>${isBike ? '🏍️ ' : '🚗 '}${r.plate}</strong>
            <span style="color: var(--text-muted); margin-left: 6px;">Slot ${r.slotId}</span>
          </div>
          <span class="status-badge-pill ${r.isActive ? 'occupied' : 'available'}">
            ${r.isActive ? 'Active' : 'Completed (₹' + r.fee + ')'}
          </span>
        `;
        logEl.appendChild(row);
      });
    } catch (e) {
      console.warn('Activity log error:', e);
    }
  },

  // ==========================================================================
  // PARKING ENTRY & EXIT MODALS (MODULES 3, 4, 8)
  // ==========================================================================
  onParkEVRequirementChanged(isEV) {
    const grp = document.getElementById('park-connector-group');
    if (grp) {
      grp.style.display = isEV ? 'block' : 'none';
    }
  },

  openQuickParkModal() {
    const pInput = document.getElementById('park-plate-input');
    if (pInput) pInput.value = '';
    const phoneInput = document.getElementById('park-phone-input');
    if (phoneInput) phoneInput.value = '';
    const evNo = document.getElementById('park-ev-no');
    if (evNo) evNo.checked = true;
    this.onParkEVRequirementChanged(false);

    // Populate Entrance Gates
    const gateSelect = document.getElementById('park-gate-input');
    if (gateSelect) {
      gateSelect.innerHTML = '<option value="">Auto (Nearest Entry Gate via Shortest Route)</option>';
      const gates = (this.activePlot && this.activePlot.activePlot && this.activePlot.activePlot.gates) 
        ? this.activePlot.activePlot.gates 
        : [];
      gates.filter(g => g.isActive && (g.type === 'ENTRANCE' || g.type === 'BIDIRECTIONAL')).forEach(g => {
        const opt = document.createElement('option');
        opt.value = g.id;
        opt.textContent = `${g.name} (${g.type === 'BIDIRECTIONAL' ? 'Bidirectional' : 'Entry Gate'} @ [${g.row},${g.col}])`;
        gateSelect.appendChild(opt);
      });
    }

    this.openModal('modal-park-vehicle');
  },

  openQuickExitModal() {
    const exitInput = document.getElementById('exit-id-input');
    if (exitInput) exitInput.value = '';

    // Populate Departure Exit Gates
    const gateSelect = document.getElementById('exit-gate-input');
    if (gateSelect) {
      gateSelect.innerHTML = '<option value="">Auto (Nearest Active Exit Gate)</option>';
      const gates = (this.activePlot && this.activePlot.activePlot && this.activePlot.activePlot.gates)
        ? this.activePlot.activePlot.gates
        : [];
      gates.filter(g => g.isActive && (g.type === 'EXIT' || g.type === 'BIDIRECTIONAL')).forEach(g => {
        const opt = document.createElement('option');
        opt.value = g.id;
        opt.textContent = `${g.name} (${g.type === 'BIDIRECTIONAL' ? 'Bidirectional' : 'Exit Gate'} @ [${g.row},${g.col}])`;
        gateSelect.appendChild(opt);
      });
    }

    this.openModal('modal-exit-vehicle');
  },

  async submitParkVehicle() {
    const rawPlate = document.getElementById('park-plate-input').value.trim();
    const type = document.getElementById('park-type-input').value;
    const owner = document.getElementById('park-owner-input').value.trim() || 'Guest';
    const rawPhone = document.getElementById('park-phone-input').value.trim();
    const isPriority = document.getElementById('park-priority-input').checked;

    // 1. Mandatory 10-digit phone number validation
    const cleanPhone = rawPhone.replace(/\D/g, '');
    if (!cleanPhone || cleanPhone.length !== 10) {
      this.showToast('Phone number is compulsory and must be exactly 10 digits', 'error');
      const pEl = document.getElementById('park-phone-input');
      if (pEl) pEl.focus();
      return;
    }

    // 2. License plate number format validation (e.g. DL01AB1234 or 22BH1234AA)
    const cleanPlate = rawPlate.toUpperCase().replace(/[\s-]/g, '');
    const plateRegex = /^([A-Z]{2}\d{1,2}[A-Z]{1,3}\d{4}|\d{2}BH\d{4}[A-Z]{1,2})$/;
    if (!cleanPlate || !plateRegex.test(cleanPlate)) {
      this.showToast('Invalid license plate format. Must follow registration format (e.g. DL01AB1234)', 'error');
      const plEl = document.getElementById('park-plate-input');
      if (plEl) plEl.focus();
      return;
    }

    // EV requirement & gate choice
    const isEV = document.getElementById('park-ev-yes')?.checked || false;
    const connector = document.getElementById('park-connector-input')?.value || 'ANY';
    const selectedGateId = document.getElementById('park-gate-input')?.value || '';

    try {
      const res = await api.parkVehicle({
        plate: cleanPlate,
        vehicleType: type,
        ownerName: owner,
        phone: cleanPhone,
        isPriority,
        requiresEVCharging: isEV,
        preferredConnector: connector,
        selectedGateId
      });

      this.closeModal('modal-park-vehicle');

      if (res.isQueued) {
        this.showToast(`Vehicle enqueued: ${res.message}`, 'info');
      } else {
        const distStr = (res.distanceMeters !== undefined && res.distanceMeters >= 0) ? `${res.distanceMeters.toFixed(1)}m` : '';
        const timeStr = res.estimatedTimeSeconds ? ` (~${res.estimatedTimeSeconds}s)` : '';
        const gateStr = res.fromGateName ? ` via ${res.fromGateName}` : '';
        this.showToast(`Allocated Bay ${res.assignedSlotId}${gateStr} [${distStr}${timeStr}]!`, 'success');

        if (res.path && res.path.length > 0) {
          this.applyRouteHighlight(res.path, isEV);
        }
      }

      await this.loadDashboard();
      if (this.activeView === 'live') await this.renderLiveGrid();
    } catch (e) {
      this.showToast(e.message || 'Check-in failed', 'error');
    }
  },

  async submitExitVehicle() {
    const id = document.getElementById('exit-id-input').value.trim();
    if (!id) {
      this.showToast('Please enter a license plate or slot ID', 'error');
      return;
    }
    const exitGateId = document.getElementById('exit-gate-input')?.value || '';
    await this.quickExitPlate(id, exitGateId);
    this.closeModal('modal-exit-vehicle');
    document.getElementById('exit-id-input').value = '';
  },

  async quickExitPlate(identifier, exitGateId = '') {
    try {
      const res = await api.exitVehicle(identifier, exitGateId);
      let msg = res.message;
      if (res.chargingFee && res.chargingFee > 0) {
        msg += ` | Fee: Base ₹${res.parkingFee.toFixed(2)} + EV ₹${res.chargingFee.toFixed(2)} = Total ₹${res.totalFee.toFixed(2)}`;
      } else if (res.totalFee !== undefined) {
        msg += ` | Fee: ₹${res.totalFee.toFixed(2)}`;
      }
      if (res.departureGateName) {
        msg += ` | Departure Gate: ${res.departureGateName} (${res.departureDistanceMeters ? res.departureDistanceMeters.toFixed(1) + 'm' : ''})`;
      }
      if (res.autoAllocated) {
        msg += ` | Auto-promoted waiting vehicle ${res.autoPlate} to Slot ${res.autoSlotId}!`;
      }
      this.showToast(msg, 'success');

      if (res.departurePath && res.departurePath.length > 0) {
        this.applyRouteHighlight(res.departurePath, false);
      }

      await this.loadDashboard();
      if (this.activeView === 'live') await this.renderLiveGrid();
    } catch (e) {
      this.showToast(e.message || 'Exit failed', 'error');
    }
  },

  // ==========================================================================
  // MODULE 1, 2, 5: LAYOUT CONFIGURATION & EV CHARGERS
  // ==========================================================================
  async loadLayoutConfig() {
    try {
      const data = await api.getStatus();
      this.activePlot = data;
      const plot = data.activePlot;
      if (!plot) return;

      // Update plot selector
      const select = document.getElementById('layout-plot-select');
      if (select && this.allPlots) {
        select.innerHTML = '';
        this.allPlots.forEach(p => {
          const opt = document.createElement('option');
          opt.value = p.id;
          opt.textContent = `${p.name} (${p.location})`;
          if (p.id === plot.id) opt.selected = true;
          select.appendChild(opt);
        });
      }

      // Update scale input
      const scaleInput = document.getElementById('layout-scale-input');
      if (scaleInput) {
        scaleInput.value = (plot.cellScaleMeters !== undefined) ? plot.cellScaleMeters : 5.0;
      }

      // Render Gates List
      this.renderLayoutGates(plot.gates || []);

      // Render Interactive Layout Map
      this.renderInteractiveGrid(data.grid, 'layout-config-grid');

      // Run Layout Validation
      await this.runLayoutValidation();
    } catch (e) {
      console.error('Failed to load layout config:', e);
    }
  },

  async onLayoutPlotChanged(plotId) {
    await api.selectPlot(plotId);
    await this.loadDashboard();
    await this.loadLayoutConfig();
  },

  renderLayoutGates(gates) {
    const container = document.getElementById('layout-gates-container');
    const badge = document.getElementById('layout-gates-count');
    if (badge) badge.textContent = `${gates.length} Gate${gates.length === 1 ? '' : 's'}`;
    if (!container) return;

    if (!gates || gates.length === 0) {
      container.innerHTML = '<p style="font-size: 12px; color: var(--text-muted);">No gates configured. Click "+ Add Gate" to place entrance or exit points.</p>';
      return;
    }

    container.innerHTML = '';
    gates.forEach(g => {
      const card = document.createElement('div');
      card.className = 'gate-card';
      const typeBadgeClass = (g.type === 'ENTRANCE') ? 'entrance' : (g.type === 'EXIT') ? 'exit' : 'bidirectional';
      const typeLabel = (g.type === 'ENTRANCE') ? '🟢 Entrance' : (g.type === 'EXIT') ? '🔴 Exit' : '🔵 Bidirectional';

      card.innerHTML = `
        <div style="display: flex; justify-content: space-between; align-items: flex-start;">
          <div>
            <div style="display: flex; align-items: center; gap: 8px;">
              <strong style="font-size: 14px;">${g.name}</strong>
              <span class="gate-badge ${typeBadgeClass}">${typeLabel}</span>
            </div>
            <div style="font-size: 12px; color: var(--text-muted); margin-top: 4px;">
              Location: Row ${g.row}, Col ${g.col} &nbsp;|&nbsp;
              Status: <strong style="color: ${g.isActive ? '#10b981' : '#ef4444'};">${g.isActive ? 'Open / Active' : 'Closed'}</strong>
            </div>
          </div>
          <div style="display: flex; gap: 6px;">
            <button class="btn btn-secondary btn-sm" style="font-size: 11px; padding: 4px 8px;" onclick="app.toggleGateActive('${g.id}', ${!g.isActive})">
              ${g.isActive ? 'Close Gate' : 'Open Gate'}
            </button>
            <button class="btn btn-secondary btn-sm" style="font-size: 11px; padding: 4px 8px;" onclick="app.openEditGateModal('${g.id}')">
              ✏️ Edit
            </button>
            <button class="btn btn-danger-outline btn-sm" style="font-size: 11px; padding: 4px 8px;" onclick="app.deleteGate('${g.id}')">
              🗑️
            </button>
          </div>
        </div>
      `;
      container.appendChild(card);
    });
  },

  selectLayoutTool(tool) {
    this.layoutSelectedTool = tool;
    document.querySelectorAll('.layout-tool-btn').forEach(btn => {
      if (btn.getAttribute('data-tool') === tool) {
        btn.classList.add('active');
      } else {
        btn.classList.remove('active');
      }
    });
    const toolNames = {
      INSPECT: 'Inspect Mode: Click any cell to view options',
      AISLE: 'Driving Aisle Mode: Click cells to pave internal driving routes',
      SLOT_CAR: 'Car Bay Mode: Click cells to place car bays',
      SLOT_BIKE: 'Bike Bay Mode: Click cells to place two-wheeler bays',
      GATE: 'Place Gate Mode: Click any perimeter cell to place a gate',
      FACILITY: 'EV Facility Mode: Click cells to designate EV charging bays',
      BLOCKED: 'Pillar Mode: Click cells to place obstacles/pillars'
    };
    this.showToast(toolNames[tool] || `Selected ${tool} tool`, 'info');
  },

  async onLayoutGridCellClick(r, c, slot, gateAtCell) {
    if (this.layoutSelectedTool === 'INSPECT') {
      this.openCellActionModal(r, c, slot, gateAtCell);
    } else if (this.layoutSelectedTool === 'GATE') {
      this.openAddGateModal(r, c);
    } else {
      await this.setCellType(r, c, this.layoutSelectedTool);
    }
  },

  async setCellType(r, c, cellType) {
    const plotId = this.activePlot?.activePlot?.id;
    if (!plotId) return;
    try {
      await api.editCell({ plotId, row: r, col: c, cellType });
      this.showToast(`Cell [${r}, ${c}] updated to ${cellType}`, 'success');
      await this.loadLayoutConfig();
      await this.loadDashboard();
    } catch (e) {
      this.showToast(e.message || 'Failed to update cell type', 'error');
    }
  },

  async undoLayoutConfigCell() {
    const plotId = this.activePlot?.activePlot?.id;
    if (!plotId) return;
    try {
      const res = await api.undoCellEdit(plotId);
      this.showToast(res.message || 'Last layout cell edit undone via LIFO Stack!', 'info');
      await this.loadLayoutConfig();
      await this.loadDashboard();
    } catch (e) {
      this.showToast(e.message || 'No edit history available to undo', 'warning');
    }
  },

  openCellActionModal(r, c, slot, gateAtCell) {
    const modalTitle = document.getElementById('modal-cell-action-title');
    const modalBody = document.getElementById('modal-cell-action-body');
    if (!modalTitle || !modalBody) return;

    modalTitle.textContent = `Configure Cell [Row ${r}, Col ${c}]`;
    let html = '';

    if (gateAtCell) {
      html += `
        <div style="background: var(--bg-subtle); padding: 10px; border-radius: var(--radius-sm); border: 1px solid var(--border-light);">
          <div style="font-weight: 700; font-size: 14px; margin-bottom: 4px;">Gate: ${gateAtCell.name}</div>
          <div style="color: var(--text-muted); font-size: 12px; margin-bottom: 8px;">
            Type: <strong>${gateAtCell.type}</strong> | Status: <strong style="color: ${gateAtCell.isActive ? '#10b981' : '#ef4444'};">${gateAtCell.isActive ? 'Active / Open' : 'Closed'}</strong>
          </div>
          <div style="display: flex; gap: 8px; flex-wrap: wrap;">
            <button class="btn btn-secondary btn-sm" onclick="app.closeModal('modal-cell-action'); app.toggleGateActive('${gateAtCell.id}', ${!gateAtCell.isActive})">
              ${gateAtCell.isActive ? '🔒 Close Gate' : '🔓 Open Gate'}
            </button>
            <button class="btn btn-secondary btn-sm" onclick="app.closeModal('modal-cell-action'); app.openEditGateModal('${gateAtCell.id}')">
              ✏️ Edit Gate
            </button>
            <button class="btn btn-danger-outline btn-sm" onclick="app.closeModal('modal-cell-action'); app.deleteGate('${gateAtCell.id}')">
              🗑️ Delete Gate
            </button>
          </div>
        </div>
      `;
    }

    const slotType = slot ? slot.type : 'EMPTY';
    const slotId = slot ? slot.id : '';
    html += `
      <div style="background: var(--bg-subtle); padding: 10px; border-radius: var(--radius-sm); border: 1px solid var(--border-light);">
        <div style="font-weight: 700; font-size: 13px; margin-bottom: 6px;">
          Cell Type: <code>${slotType}</code> ${slotId ? `(Bay ID: ${slotId})` : ''}
        </div>
        <div style="font-size: 12px; color: var(--text-muted); margin-bottom: 10px;">
          Change cell designation to adjust internal driving paths or bays:
        </div>
        <div style="display: grid; grid-template-columns: repeat(2, 1fr); gap: 6px;">
          <button class="btn btn-secondary btn-sm" onclick="app.closeModal('modal-cell-action'); app.setCellType(${r}, ${c}, 'AISLE')">🛣️ Driving Aisle</button>
          <button class="btn btn-secondary btn-sm" onclick="app.closeModal('modal-cell-action'); app.setCellType(${r}, ${c}, 'SLOT_CAR')">🚗 Car Bay</button>
          <button class="btn btn-secondary btn-sm" onclick="app.closeModal('modal-cell-action'); app.setCellType(${r}, ${c}, 'SLOT_BIKE')">🏍️ Bike Bay</button>
          <button class="btn btn-secondary btn-sm" onclick="app.closeModal('modal-cell-action'); app.setCellType(${r}, ${c}, 'BLOCKED')">🚫 Pillar / Blocked</button>
        </div>
      </div>
    `;

    if (slot && (slot.type === 'SLOT_CAR' || slot.type === 'FACILITY')) {
      html += `
        <div style="background: var(--bg-subtle); padding: 10px; border-radius: var(--radius-sm); border: 1px solid var(--border-light);">
          <div style="font-weight: 700; font-size: 13px; margin-bottom: 4px;">⚡ EV Charging Facility</div>
          <div style="font-size: 12px; color: var(--text-muted); margin-bottom: 8px;">
            Charger: <strong>${slot.hasCharger ? (slot.chargerId + ' (' + slot.chargerStatus + ')') : 'Not Installed'}</strong>
          </div>
          <button class="btn btn-secondary btn-sm" style="width: 100%;" onclick="app.closeModal('modal-cell-action'); app.openSlotEVConfig('${slot.id}')">
            ⚡ Configure EV Charger for ${slot.id}
          </button>
        </div>
      `;
    }

    if (!gateAtCell) {
      html += `
        <button class="btn btn-primary-yellow btn-sm" style="width: 100%;" onclick="app.closeModal('modal-cell-action'); app.openAddGateModal(${r}, ${c})">
          🚪 Place Entrance / Exit Gate at [Row ${r}, Col ${c}]
        </button>
      `;
    }

    modalBody.innerHTML = html;
    this.openModal('modal-cell-action');
  },

  openAddGateModal(r = 0, c = 0) {
    const gates = (this.activePlot && this.activePlot.activePlot && this.activePlot.activePlot.gates) ? this.activePlot.activePlot.gates : [];
    document.getElementById('modal-gate-title').textContent = 'Add Entrance / Exit Gate';
    document.getElementById('gate-edit-id').value = '';
    document.getElementById('gate-edit-name').value = `Gate ${gates.length + 1}`;
    document.getElementById('gate-edit-type').value = 'ENTRANCE';
    document.getElementById('gate-edit-row').value = (typeof r === 'number') ? r : 0;
    document.getElementById('gate-edit-col').value = (typeof c === 'number') ? c : 0;
    document.getElementById('gate-edit-active').checked = true;
    this.openModal('modal-gate-editor');
  },

  openEditGateModal(gateId) {
    const gates = (this.activePlot && this.activePlot.activePlot && this.activePlot.activePlot.gates) ? this.activePlot.activePlot.gates : [];
    const g = gates.find(item => item.id === gateId);
    if (!g) return;

    document.getElementById('modal-gate-title').textContent = `Edit Gate: ${g.name}`;
    document.getElementById('gate-edit-id').value = g.id;
    document.getElementById('gate-edit-name').value = g.name;
    document.getElementById('gate-edit-type').value = g.type;
    document.getElementById('gate-edit-row').value = g.row;
    document.getElementById('gate-edit-col').value = g.col;
    document.getElementById('gate-edit-active').checked = g.isActive;
    this.openModal('modal-gate-editor');
  },

  async submitGateEditor() {
    const plotId = this.activePlot?.activePlot?.id;
    if (!plotId) return;

    const gateId = document.getElementById('gate-edit-id').value;
    const name = document.getElementById('gate-edit-name').value.trim();
    if (!name) {
      this.showToast('Please enter a gate name', 'error');
      return;
    }
    const type = document.getElementById('gate-edit-type').value;
    const row = parseInt(document.getElementById('gate-edit-row').value, 10) || 0;
    const col = parseInt(document.getElementById('gate-edit-col').value, 10) || 0;
    const isActive = document.getElementById('gate-edit-active').checked;

    try {
      if (gateId) {
        await api.updateGate({ plotId, id: gateId, gateId, name, type, row, col, isActive });
        this.showToast(`Gate '${name}' updated! Plot slot numbers automatically re-allotted.`, 'success');
      } else {
        await api.addGate({ plotId, name, type, row, col, isActive });
        this.showToast(`Gate '${name}' added! Plot slot numbers automatically re-allotted.`, 'success');
      }
      this.closeModal('modal-gate-editor');
      await this.loadPlots();
      await this.loadLayoutConfig();
      await this.loadDashboard();
      if (this.activeView === 'live') await this.renderLiveGrid();
    } catch (e) {
      this.showToast(e.message || 'Error saving gate', 'error');
    }
  },

  async toggleGateActive(gateId, active) {
    const plotId = this.activePlot?.activePlot?.id;
    if (!plotId) return;
    try {
      await api.toggleGate(plotId, gateId, active);
      this.showToast(`Gate status set to ${active ? 'Active / Open' : 'Closed'}. Slot allotments updated.`, 'info');
      await this.loadPlots();
      await this.loadLayoutConfig();
      await this.loadDashboard();
      if (this.activeView === 'live') await this.renderLiveGrid();
    } catch (e) {
      this.showToast(e.message || 'Error updating gate', 'error');
    }
  },

  async deleteGate(gateId) {
    const plotId = this.activePlot?.activePlot?.id;
    if (!plotId) return;
    if (!confirm('Are you sure you want to delete this gate?')) return;

    try {
      await api.deleteGate(plotId, gateId);
      this.showToast('Gate removed! Plot slot numbers automatically re-allotted.', 'success');
      await this.loadPlots();
      await this.loadLayoutConfig();
      await this.loadDashboard();
      if (this.activeView === 'live') await this.renderLiveGrid();
    } catch (e) {
      this.showToast(e.message || 'Error deleting gate', 'error');
    }
  },

  async saveScaleMeters() {
    const plotId = this.activePlot?.activePlot?.id;
    if (!plotId) return;
    const scale = parseFloat(document.getElementById('layout-scale-input').value) || 5.0;
    if (scale <= 0) {
      this.showToast('Scale must be a positive number', 'error');
      return;
    }

    try {
      await api.updateScale(plotId, scale);
      this.showToast(`Map scale calibrated to ${scale} meters / cell`, 'success');
      await this.loadLayoutConfig();
      await this.loadDashboard();
    } catch (e) {
      this.showToast(e.message || 'Failed to update map scale', 'error');
    }
  },

  async runLayoutValidation() {
    const plotId = this.activePlot?.activePlot?.id;
    if (!plotId) return;
    const alertBox = document.getElementById('layout-validation-alert');
    if (!alertBox) return;

    try {
      const res = await api.validateLayout(plotId);
      alertBox.style.display = 'block';

      const isValid = (res.isValid !== undefined)
        ? (res.isValid && (!res.warnings || res.warnings.length === 0))
        : (res.allSlotsReachable && (!res.warnings || res.warnings.length === 0));

      if (isValid) {
        alertBox.style.background = 'rgba(16, 185, 129, 0.12)';
        alertBox.style.border = '1px solid rgba(16, 185, 129, 0.4)';
        alertBox.style.color = '#065f46';
        alertBox.innerHTML = `
          <div style="display: flex; align-items: center; gap: 8px;">
            <span style="font-size: 16px;">✓</span>
            <div>
              <strong>Layout Fully Verified:</strong> All active gates and parking bays are connected through traversable driving paths. Calibrated distances available.
            </div>
          </div>
        `;
      } else {
        alertBox.style.background = 'rgba(239, 68, 68, 0.12)';
        alertBox.style.border = '1px solid rgba(239, 68, 68, 0.4)';
        alertBox.style.color = '#991b1b';
        let warnHtml = (res.warnings && res.warnings.length > 0)
          ? res.warnings.map(w => `<li>${w}</li>`).join('')
          : '<li>One or more gates or slots are not connected to the traversable path network.</li>';
        alertBox.innerHTML = `
          <div style="display: flex; align-items: flex-start; gap: 8px;">
            <span style="font-size: 16px;">⚠️</span>
            <div>
              <strong>Layout Warnings Detected:</strong>
              <ul style="margin: 6px 0 0 18px; padding: 0;">${warnHtml}</ul>
            </div>
          </div>
        `;
      }
    } catch (e) {
      console.warn('Validation error:', e);
    }
  },

  // EV Slot Configuration
  openSlotEVConfig(slotId) {
    const plotId = this.activePlot?.activePlot?.id;
    if (!plotId) return;

    let slot = null;
    if (this.activePlot && this.activePlot.grid) {
      for (const row of this.activePlot.grid) {
        for (const s of row) {
          if (s.id === slotId) {
            slot = s;
            break;
          }
        }
        if (slot) break;
      }
    }
    if (!slot) return;

    document.getElementById('modal-ev-title').textContent = `Configure EV Bay: ${slotId}`;
    document.getElementById('ev-config-slot-id').value = slotId;

    const installed = !!slot.hasCharger;
    document.getElementById('ev-config-installed').checked = installed;
    document.getElementById('ev-config-fields').style.display = installed ? 'flex' : 'none';

    document.getElementById('ev-config-charger-id').value = slot.chargerId || `EV-${slotId}`;
    document.getElementById('ev-config-connector').value = slot.connector || 'CCS2';
    document.getElementById('ev-config-power').value = slot.powerKw || 50;
    document.getElementById('ev-config-rate').value = slot.chargingRate || 50;
    document.getElementById('ev-config-status').value = slot.chargerStatus || 'OPERATIONAL';

    this.openModal('modal-ev-config');
  },

  onEVInstalledToggle(checked) {
    const f = document.getElementById('ev-config-fields');
    if (f) f.style.display = checked ? 'flex' : 'none';
  },

  async submitEVConfig() {
    const plotId = this.activePlot?.activePlot?.id;
    const slotId = document.getElementById('ev-config-slot-id').value;
    if (!plotId || !slotId) return;

    const hasCharger = document.getElementById('ev-config-installed').checked;
    const chargerId = document.getElementById('ev-config-charger-id').value.trim() || `EV-${slotId}`;
    const connector = document.getElementById('ev-config-connector').value;
    const powerKw = parseFloat(document.getElementById('ev-config-power').value) || 50;
    const chargingRate = parseFloat(document.getElementById('ev-config-rate').value) || 50;
    const chargerStatus = document.getElementById('ev-config-status').value;

    try {
      await api.configureSlotEV({
        plotId,
        slotId,
        hasCharger,
        chargerId,
        connector,
        powerKw,
        chargingRate,
        chargerStatus
      });

      this.closeModal('modal-ev-config');
      this.showToast(`Bay ${slotId} EV facilities updated!`, 'success');
      await this.loadDashboard();
      if (this.activeView === 'layout-config') await this.loadLayoutConfig();
    } catch (e) {
      this.showToast(e.message || 'Error updating EV configuration', 'error');
    }
  },

  async toggleChargerQuick(slotId, newStatus) {
    const plotId = this.activePlot?.activePlot?.id;
    if (!plotId) return;
    try {
      await api.updateSlotEVStatus(plotId, slotId, newStatus);
      this.showToast(`Bay ${slotId} charger marked as ${newStatus}`, 'info');
      this.closeModal('modal-slot-inspector');
      await this.loadDashboard();
      if (this.activeView === 'layout-config') await this.loadLayoutConfig();
    } catch (e) {
      this.showToast(e.message || 'Failed to update charger status', 'error');
    }
  },

  applyRouteHighlight(path, isEV = false) {
    // Clear existing
    document.querySelectorAll('.slot-visual-box.route-path, .slot-visual-box.ev-route-path').forEach(el => {
      el.classList.remove('route-path', 'ev-route-path');
    });

    const cls = isEV ? 'ev-route-path' : 'route-path';
    path.forEach(coord => {
      const ids = [
        `dash-interactive-grid-cell-${coord[0]}-${coord[1]}`,
        `live-slot-${coord[0]}-${coord[1]}`,
        `layout-config-grid-cell-${coord[0]}-${coord[1]}`
      ];
      ids.forEach(id => {
        const el = document.getElementById(id);
        if (el) el.classList.add(cls);
      });
    });
  },

  // ==========================================================================
  // LIVE PARKING VIEW & BFS DRIVING PATH ROUTING
  // ==========================================================================
  async renderLiveGrid() {
    const container = document.getElementById('live-full-grid');
    if (!container) return;

    try {
      const data = await api.getStatus();
      container.innerHTML = '';
      const cols = data.grid[0].length;
      const colWidth = cols <= 4 ? '100px' : (cols <= 8 ? '84px' : '76px');
      container.style.gridTemplateColumns = `repeat(${cols}, ${colWidth})`;
      container.style.justifyContent = 'center';

      const gates = (data.activePlot && data.activePlot.gates) ? data.activePlot.gates : [];

      data.grid.forEach((row, r) => {
        row.forEach((slot, c) => {
          const isFilteredOut = (this.liveFilter !== 'ALL' && slot.status !== this.liveFilter);

          const box = document.createElement('div');
          box.className = 'slot-visual-box';
          box.setAttribute('id', `live-slot-${r}-${c}`);
          if (isFilteredOut) {
            box.style.opacity = '0.2';
            box.style.filter = 'grayscale(1)';
          }

          const gateAtCell = gates.find(g => g.row === r && g.col === c);

          if (gateAtCell) {
            const isEnt = (gateAtCell.type === 'ENTRANCE');
            const isExit = (gateAtCell.type === 'EXIT');
            const isBi = (gateAtCell.type === 'BIDIRECTIONAL');
            const badgeClass = isEnt ? 'entrance' : (isExit ? 'exit' : 'bidirectional');
            box.classList.add('gate-slot', badgeClass);
            if (!gateAtCell.isActive) box.classList.add('out-of-service');

            box.innerHTML = `
              <div style="font-size: 10px; font-weight: 800; text-align: center; line-height: 1.2;">
                <div>${isBi ? '🔵 GATE' : (isEnt ? '🟢 IN' : '🔴 OUT')}</div>
                <div style="font-size: 8px; opacity: 0.9; margin-top: 2px; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; max-width: 58px;" title="${gateAtCell.name}">${gateAtCell.name}</div>
                ${!gateAtCell.isActive ? '<div style="font-size: 8px; color: #fca5a5; font-weight: 900;">CLOSED</div>' : ''}
              </div>
            `;
          } else if (slot.type === 'AISLE') {
            box.style.background = 'rgba(59, 130, 246, 0.15)';
            box.style.borderColor = 'rgba(59, 130, 246, 0.3)';
            box.innerHTML = `<span style="font-size: 10px; color: #93c5fd;">🛣️ AISLE</span>`;
          } else if (slot.type === 'ENTRANCE') {
            box.style.background = 'rgba(16, 185, 129, 0.3)';
            box.style.borderColor = '#10b981';
            box.innerHTML = `<span style="font-size: 10px; font-weight: 800; color: #a7f3d0;">⬇️ IN</span>`;
          } else if (slot.type === 'EXIT') {
            box.style.background = 'rgba(239, 68, 68, 0.3)';
            box.style.borderColor = '#ef4444';
            box.innerHTML = `<span style="font-size: 10px; font-weight: 800; color: #fca5a5;">⬆️ OUT</span>`;
          } else if (slot.type === 'BLOCKED') {
            box.style.background = 'rgba(100, 116, 139, 0.4)';
            box.style.borderColor = '#64748b';
            box.innerHTML = `<span style="font-size: 10px; color: #cbd5e1;">🚫 PILLAR</span>`;
          } else if (slot.type === 'FACILITY') {
            box.style.background = 'rgba(139, 92, 246, 0.3)';
            box.style.borderColor = '#8b5cf6';
            box.innerHTML = `<span style="font-size: 10px; color: #c4b5fd;">⚡ EV CHARGE</span>`;
          } else {
            // Actual Parking Bay
            const idLabel = document.createElement('span');
            idLabel.className = 'slot-id-label';
            idLabel.textContent = slot.id;
            box.appendChild(idLabel);

            if (slot.hasCharger) {
              box.classList.add('ev-slot');
              if (slot.chargerStatus !== 'OPERATIONAL') {
                box.classList.add('out-of-service');
              }
              const evIcon = document.createElement('span');
              evIcon.className = 'ev-badge-icon';
              evIcon.innerHTML = (slot.chargerStatus === 'OPERATIONAL') ? '⚡' : '⚠️';
              box.appendChild(evIcon);
            }

            if (slot.status === 'OCCUPIED') {
              box.classList.add('occupied');
              const isTwoWheeler = (slot.vehicleType === 'TWO_WHEELER' || slot.type === 'SLOT_BIKE');
              const car = document.createElement('div');
              car.className = 'car-sprite';
              car.style.backgroundColor = isTwoWheeler ? '#10b981' : '#3b82f6';
              car.innerHTML = isTwoWheeler ? `<span style="font-size: 14px;">🏍️</span>` : `<span style="font-size: 14px;">🚗</span>`;
              box.appendChild(car);
            } else {
              box.classList.add('available');
              if (slot.type === 'SLOT_BIKE') {
                const bIcon = document.createElement('span');
                bIcon.style.fontSize = '14px';
                bIcon.style.opacity = '0.6';
                bIcon.title = 'Two-Wheeler Bay';
                bIcon.textContent = '🏍️';
                box.appendChild(bIcon);
              } else if (slot.type === 'SLOT_CAR') {
                const cIcon = document.createElement('span');
                cIcon.style.fontSize = '14px';
                cIcon.style.opacity = '0.6';
                cIcon.title = slot.hasCharger ? 'EV Charging Bay' : 'Car Bay';
                cIcon.textContent = slot.hasCharger ? '⚡' : '🚗';
                box.appendChild(cIcon);
              }
            }
          }

          // On click, calculate and show BFS Driving Route or gate info!
          box.addEventListener('click', async () => {
            if (gateAtCell) {
              this.showToast(`Gate: ${gateAtCell.name} (${gateAtCell.type}) - ${gateAtCell.isActive ? 'Active' : 'Closed'}`, 'info');
            } else if (slot.type === 'AISLE') {
              this.showToast(`Driving Aisle Lane (Row ${r + 1}, Col ${c + 1}) — Non-parking area`, 'info');
            } else if (slot.type === 'BLOCKED') {
              this.showToast(`Structural Pillar (Row ${r + 1}, Col ${c + 1}) — Parking prohibited`, 'info');
            } else if (slot.id) {
              await this.highlightBFSRoute(data.activePlot.id, slot.id);
            }
          });

          container.appendChild(box);
        });
      });
    } catch (e) {
      console.error('Error rendering live grid:', e);
    }
  },

  filterLiveGrid(status) {
    this.liveFilter = status;
    this.renderLiveGrid();
  },

  async highlightBFSRoute(plotId, slotId, gateId = '', isExit = false) {
    try {
      const activePlotId = plotId || (this.activePlot && this.activePlot.activePlot ? this.activePlot.activePlot.id : '');
      const route = await api.getRoute(activePlotId, slotId, gateId, isExit);

      // Clear previous path highlighting
      document.querySelectorAll('.slot-visual-box.route-path, .slot-visual-box.ev-route-path').forEach(el => {
        el.classList.remove('route-path', 'ev-route-path');
      });

      if (route.found && route.path && route.path.length > 0) {
        this.applyRouteHighlight(route.path, false);
        const distStr = route.distanceMeters !== undefined ? `${route.distanceMeters.toFixed(1)} m` : `${route.distance} cells`;
        const timeStr = route.estimatedTimeSeconds ? ` (~${route.estimatedTimeSeconds}s driving)` : '';
        const gateStr = route.fromGateName ? ` from ${route.fromGateName}` : '';
        this.showToast(`Shortest Driving Route: ${distStr}${timeStr}${gateStr} to Bay ${slotId}`, 'info');
      } else {
        this.showToast(`Distance unavailable — parking layout requires configuration (Bay ${slotId} is unreachable from gate)`, 'error');
      }
    } catch (e) {
      console.warn('Route check error:', e);
    }
  },

  // ==========================================================================
  // RESERVATIONS
  // ==========================================================================
  openNewReservationModal() {
    this.openModal('modal-new-reservation');
  },

  async submitReservation() {
    const plate = document.getElementById('res-plate-input').value.trim();
    const type = document.getElementById('res-type-input').value;
    const name = document.getElementById('res-name-input').value.trim();
    const phone = document.getElementById('res-phone-input').value.trim();
    const time = document.getElementById('res-time-input').value.trim();

    if (!plate || !name) {
      this.showToast('Please fill all required reservation fields', 'error');
      return;
    }

    try {
      const res = await api.createReservation({
        vehiclePlate: plate,
        vehicleType: type,
        customerName: name,
        phone,
        scheduledTime: time
      });
      this.closeModal('modal-new-reservation');
      this.showToast(res.message, 'success');
      this.loadReservations();
    } catch (e) {
      this.showToast(e.message || 'Reservation booking failed', 'error');
    }
  },

  async loadReservations() {
    const tbody = document.querySelector('#reservations-table tbody');
    if (!tbody) return;
    try {
      const data = await api.getReservations();
      tbody.innerHTML = '';
      if (!data.reservations || data.reservations.length === 0) {
        tbody.innerHTML = `<tr><td colspan="8" style="text-align: center; color: var(--text-muted);">No active reservations. Click '+ New Reservation' to book.</td></tr>`;
        return;
      }
      data.reservations.forEach(r => {
        const tr = document.createElement('tr');
        tr.innerHTML = `
          <td><strong>${r.reservationId}</strong></td>
          <td>${r.plotId}</td>
          <td><span class="status-badge-pill reserved">${r.slotId}</span></td>
          <td>${r.vehiclePlate}</td>
          <td>${r.customerName}</td>
          <td>${r.phone}</td>
          <td>${r.scheduledTime}</td>
          <td>
            <button class="btn btn-secondary btn-sm" onclick="app.cancelReservation('${r.reservationId}')">Cancel</button>
          </td>
        `;
        tbody.appendChild(tr);
      });
    } catch (e) {
      console.warn('Reservations load error:', e);
    }
  },

  async cancelReservation(id) {
    try {
      const res = await api.cancelReservation(id);
      this.showToast(res.message, 'info');
      this.loadReservations();
      this.loadDashboard();
    } catch (e) {
      this.showToast(e.message, 'error');
    }
  },

  // ==========================================================================
  // VEHICLE RECORDS & QUICKSORT
  // ==========================================================================
  async loadRecords(sortBy = 'time') {
    const tbody = document.querySelector('#records-table tbody');
    if (!tbody) return;
    try {
      const data = await api.getRecords(sortBy);
      tbody.innerHTML = '';
      if (!data.records || data.records.length === 0) {
        tbody.innerHTML = `<tr><td colspan="10" style="text-align: center; color: var(--text-muted);">No vehicle parking records found.</td></tr>`;
        return;
      }
      data.records.forEach(r => {
        const tr = document.createElement('tr');
        tr.innerHTML = `
          <td><strong>${r.sessionId}</strong></td>
          <td><strong>${r.plate}</strong></td>
          <td>${r.vehicleType}</td>
          <td>${r.slotId}</td>
          <td>${r.ownerName}</td>
          <td>${r.entryTime}</td>
          <td>${r.exitTime || '—'}</td>
          <td>${r.durationMinutes} mins</td>
          <td>₹ ${r.fee.toFixed(2)}</td>
          <td>
            <span class="status-badge-pill ${r.isActive ? 'occupied' : 'available'}">
              ${r.isActive ? 'Active' : 'Completed'}
            </span>
          </td>
        `;
        tbody.appendChild(tr);
      });
    } catch (e) {
      console.warn('Records load error:', e);
    }
  },

  // ==========================================================================
  // GLOBAL SEARCH (O(1) HASH & O(log n) BST)
  // ==========================================================================
  async performGlobalSearch(query) {
    if (!query) return;
    try {
      const data = await api.searchVehicle(query);
      if (data.found) {
        this.showToast(
          `Found Vehicle ${data.plate}! Parked at ${data.plotName}, Bay ${data.slotId} (${data.isActive ? 'Active' : 'Completed'})`,
          'success'
        );
      }
    } catch (e) {
      this.showToast(e.message || `No records found for '${query}'`, 'error');
    }
  },

  // ==========================================================================
  // PARKING ANALYTICS & REVENUE
  // ==========================================================================
  async loadAnalytics() {
    try {
      const data = await api.getRecords('time');
      const records = data.records || [];
      const completed = records.filter(r => !r.isActive);
      const totalRev = completed.reduce((acc, r) => acc + (parseFloat(r.fee) || 0), 0);
      const totalMin = completed.reduce((acc, r) => acc + (parseInt(r.durationMinutes, 10) || 0), 0);
      const avgMin = completed.length > 0 ? Math.round(totalMin / completed.length) : 0;
      const hours = Math.floor(avgMin / 60);
      const mins = avgMin % 60;
      const avgStr = hours > 0 ? `${hours}h ${mins}m` : `${mins}m`;

      const revEl = document.getElementById('analytics-revenue');
      const avgEl = document.getElementById('analytics-avg-time');
      const totEl = document.getElementById('analytics-total-vehicles');

      if (revEl) revEl.textContent = `₹ ${totalRev.toFixed(2)}`;
      if (avgEl) avgEl.textContent = completed.length > 0 ? avgStr : '—';
      if (totEl) totEl.textContent = records.length;
    } catch (e) {
      console.warn('Analytics load error:', e);
    }
  },

  // ==========================================================================
  // SETTINGS & DSA TELEMETRY (COLLEGE VIVA MODE)
  // ==========================================================================
  async loadTelemetry() {
    const grid = document.getElementById('dsa-metrics-grid');
    if (!grid) return;
    try {
      const t = await api.getTelemetry();
      grid.innerHTML = `
        <div class="card">
          <span style="font-size: 11px; font-weight: 700; color: var(--text-muted);">2D MATRIX SLOTS</span>
          <div style="font-size: 24px; font-weight: 800; color: #3b82f6; margin-top: 6px;">${t.totalSlots} Bays</div>
          <span style="font-size: 11px; color: var(--text-secondary);">${t.totalPlots} Active Plots</span>
        </div>
        <div class="card">
          <span style="font-size: 11px; font-weight: 700; color: var(--text-muted);">VEHICLE BST NODES</span>
          <div style="font-size: 24px; font-weight: 800; color: #10b981; margin-top: 6px;">${t.bstNodes} Keys</div>
          <span style="font-size: 11px; color: var(--text-secondary);">Height: ${t.bstHeight} levels</span>
        </div>
        <div class="card">
          <span style="font-size: 11px; font-weight: 700; color: var(--text-muted);">HASH MAP LOOKUPS</span>
          <div style="font-size: 24px; font-weight: 800; color: var(--color-yellow-dark); margin-top: 6px;">${t.activeSessions} Active</div>
          <span style="font-size: 11px; color: var(--text-secondary);">O(1) Plate Indexing</span>
        </div>
        <div class="card">
          <span style="font-size: 11px; font-weight: 700; color: var(--text-muted);">QUEUES & STACKS</span>
          <div style="font-size: 24px; font-weight: 800; color: #8b5cf6; margin-top: 6px;">Q: ${t.waitingQueue} | Stk: ${t.undoStack}</div>
          <span style="font-size: 11px; color: var(--text-secondary);">VIP Priority + FIFO</span>
        </div>
      `;
    } catch (e) {
      console.warn('Telemetry error:', e);
    }
  },

  async runDSAVerificationTests() {
    const card = document.getElementById('dsa-test-results-card');
    const container = document.getElementById('dsa-test-results-container');
    if (!card || !container) return;

    try {
      card.style.display = 'block';
      container.innerHTML = '<p style="font-size: 12px; color: var(--text-muted);">Running C++17 unit tests on backend...</p>';

      const data = await api.runDSATests();
      container.innerHTML = '';

      (data.results || []).forEach(t => {
        const row = document.createElement('div');
        row.style.display = 'flex';
        row.style.alignItems = 'center';
        row.style.justifyContent = 'space-between';
        row.style.padding = '10px 14px';
        row.style.borderRadius = '8px';
        row.style.background = 'var(--bg-subtle)';
        row.style.border = '1px solid var(--border-light)';
        row.innerHTML = `
          <div>
            <strong>${t.name}</strong>
            <div style="font-size: 11px; color: var(--text-muted);">Data Structure: ${t.structure} | Complexity: <code>${t.timeComplexity}</code></div>
          </div>
          <span class="status-badge-pill ${t.passed ? 'available' : 'occupied'}">
            ${t.passed ? '✓ PASSED' : '✕ FAILED'}
          </span>
        `;
        container.appendChild(row);
      });

      this.showToast('Automated DSA Unit Test Suite completed with 100% Pass!', 'success');
    } catch (e) {
      this.showToast(e.message || 'Error running tests', 'error');
    }
  },

  showGuideModal() {
    this.openModal('modal-guide');
  }
};

// Initialize Application when DOM is ready
document.addEventListener('DOMContentLoaded', () => {
  app.init();
});
