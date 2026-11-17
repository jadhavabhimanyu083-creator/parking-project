const fs = require('fs');

let api = fs.readFileSync('public/js/api.js', 'utf8');

// 1. Update _getDB()
const oldDummyLine = `const dummyPlotIds = new Set(['plot_phoenix_marketcity', 'plot_sriook_park', 'plot_zoob_residences', 'plot_tower_43']);`;
const newDummyLine = `const dummyPlotIds = new Set(['plot_phoenix_marketcity', 'plot_sriook_park', 'plot_zoob_residences', 'plot_tower_43', 'plot_1792918456776495000']);`;

api = api.replace(oldDummyLine, newDummyLine);

const oldCampusCheck = `      // Ensure central campus plot has been renamed to Demo Parking in cache
      const campusPlot = db.plots.find(p => p.id === 'plot_central_campus');
      if (campusPlot && campusPlot.name === 'Central Campus Parking') {
        campusPlot.name = 'Demo Parking';
        changed = true;
      }`;

const newCampusCheck = `      // Ensure central campus plot has been renamed to Demo Parking in cache and is completely empty
      const campusPlot = db.plots.find(p => p.id === 'plot_central_campus');
      if (campusPlot) {
        if (campusPlot.name !== 'Demo Parking') {
          campusPlot.name = 'Demo Parking';
          changed = true;
        }
        if (campusPlot.grid) {
          campusPlot.grid.forEach(row => {
            row.forEach(slot => {
              if (slot.status === 'OCCUPIED' || slot.plate) {
                slot.status = 'AVAILABLE';
                slot.plate = '';
                changed = true;
              }
            });
          });
        }
      }`;

api = api.replace(oldCampusCheck, newCampusCheck);

// Clean sessions in _getDB
const oldSessClean = `db.sessions = db.sessions.filter(s => !dummyPlotIds.has(s.plotId));`;
const newSessClean = `db.sessions = db.sessions.filter(s => s.plotId !== 'plot_central_campus' && !dummyPlotIds.has(s.plotId));`;
api = api.replace(oldSessClean, newSessClean);

// 2. Remove sampleParked in _createDefaultDB() and keep sessions empty
const sampleParkedStart = api.indexOf('    // Seed realistic parked vehicles matching dashboard');
const defaultPlotStart = api.indexOf('    const defaultPlot = {');

if (sampleParkedStart !== -1 && defaultPlotStart !== -1) {
  api = api.substring(0, sampleParkedStart) + '    const sessions = [];\n\n' + api.substring(defaultPlotStart);
}

// 3. Keep ONLY vit in adminPlots (remove Metro City Deck)
const adminPlotsStart = api.indexOf('    const adminPlots = [');
const adminPlotsEnd = api.indexOf(';\n    const seededPlots = [defaultPlot, ...adminPlots];');

if (adminPlotsStart !== -1 && adminPlotsEnd !== -1) {
  const vitPlot = JSON.parse(fs.readFileSync('scratch/admin_plots.json', 'utf8')).find(p => p.id === 'plot_1792911489297940000');
  const newAdminPlots = `    const adminPlots = ${JSON.stringify([vitPlot])}`;
  api = api.substring(0, adminPlotsStart) + newAdminPlots + api.substring(adminPlotsEnd);
}

fs.writeFileSync('public/js/api.js', api, 'utf8');
console.log('Successfully updated public/js/api.js for completely empty Demo plot and removed Metro City Deck!');
