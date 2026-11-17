const fs = require('fs');

let apiJs = fs.readFileSync('public/js/api.js', 'utf8');
const adminPlots = JSON.parse(fs.readFileSync('scratch/admin_plots.json', 'utf8'));

// 1. Update _getDB()
const oldGetDB = `  _getDB() {
    let raw = localStorage.getItem(this.DB_KEY);
    if (!raw) {
      const defaultDB = this._createDefaultDB();
      this._saveDB(defaultDB);
      return defaultDB;
    }
    try {
      const parsed = JSON.parse(raw);
      if (!parsed.plots || !parsed.plots[0].gates) {
        const defaultDB = this._createDefaultDB();
        this._saveDB(defaultDB);
        return defaultDB;
      }
      return parsed;
    } catch (e) {
      const defaultDB = this._createDefaultDB();
      this._saveDB(defaultDB);
      return defaultDB;
    }
  },`;

const newGetDB = `  _getDB() {
    let raw = localStorage.getItem(this.DB_KEY);
    let db = null;
    if (!raw) {
      db = this._createDefaultDB();
      this._saveDB(db);
      return db;
    }
    try {
      db = JSON.parse(raw);
      if (!db.plots || !db.plots[0] || !db.plots[0].gates) {
        db = this._createDefaultDB();
        this._saveDB(db);
        return db;
      }
    } catch (e) {
      db = this._createDefaultDB();
      this._saveDB(db);
      return db;
    }

    // Proactively purge old dummy plots and preserve user/admin added plots
    const dummyPlotIds = new Set(['plot_phoenix_marketcity', 'plot_sriook_park', 'plot_zoob_residences', 'plot_tower_43']);
    let changed = false;
    if (db.plots) {
      const originalCount = db.plots.length;
      db.plots = db.plots.filter(p => !dummyPlotIds.has(p.id));
      if (db.plots.length !== originalCount) {
        changed = true;
      }
      if (dummyPlotIds.has(db.activePlotId) || !db.plots.some(p => p.id === db.activePlotId)) {
        db.activePlotId = db.plots[0] ? db.plots[0].id : 'plot_central_campus';
        changed = true;
      }
      // Ensure admin-added plots from initial seed exist
      const defaultDB = this._createDefaultDB();
      for (const defPlot of defaultDB.plots) {
        if (!db.plots.some(p => p.id === defPlot.id)) {
          db.plots.push(defPlot);
          changed = true;
        }
      }
    }
    if (db.sessions) {
      const origSessCount = db.sessions.length;
      db.sessions = db.sessions.filter(s => !dummyPlotIds.has(s.plotId));
      if (db.sessions.length !== origSessCount) {
        changed = true;
      }
    }
    if (changed) {
      this._saveDB(db);
    }
    return db;
  },`;

if (!apiJs.includes(oldGetDB)) {
  console.error("Could not find oldGetDB!");
  process.exit(1);
}
apiJs = apiJs.replace(oldGetDB, newGetDB);

// 2. Replace seededPlots in _createDefaultDB()
// Search from "const seededPlots = [{"
const seedStartIdx = apiJs.indexOf('const seededPlots = [');
const seedEndIdx = apiJs.indexOf(';\n    // Ensure defaultPlot is present', seedStartIdx);

if (seedStartIdx === -1 || seedEndIdx === -1) {
  console.error("Could not find seededPlots boundaries!", { seedStartIdx, seedEndIdx });
  process.exit(1);
}

const newSeededPlotsStr = `const adminPlots = ${JSON.stringify(adminPlots)};
    const seededPlots = [defaultPlot, ...adminPlots]`;

apiJs = apiJs.substring(0, seedStartIdx) + newSeededPlotsStr + apiJs.substring(seedEndIdx);

fs.writeFileSync('public/js/api.js', apiJs, 'utf8');
console.log('Successfully updated public/js/api.js!');
