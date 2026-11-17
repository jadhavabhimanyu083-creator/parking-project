const fs = require('fs');

// 1. data/plots.json
let plotsRaw = fs.readFileSync('data/plots.json', 'utf8');
plotsRaw = plotsRaw.replace('"name": "Central Campus Parking"', '"name": "Demo Parking"');
fs.writeFileSync('data/plots.json', plotsRaw, 'utf8');
console.log('1. Updated data/plots.json');

// 2. backend/src/ParkingManager.cpp
let pmCpp = fs.readFileSync('backend/src/ParkingManager.cpp', 'utf8');
pmCpp = pmCpp.replace('p.name = "Central Campus Parking";', 'p.name = "Demo Parking";');
// Also in loadFromDisk
const loadMarker = 'p.name = Json::getStringField(po, "name", "Parking Lot");';
const loadRepl = `p.name = Json::getStringField(po, "name", "Parking Lot");
        if (p.id == "plot_central_campus" && p.name == "Central Campus Parking") {
            p.name = "Demo Parking";
        }`;
if (pmCpp.includes(loadMarker) && !pmCpp.includes('p.name == "Central Campus Parking"')) {
  pmCpp = pmCpp.replace(loadMarker, loadRepl);
}
fs.writeFileSync('backend/src/ParkingManager.cpp', pmCpp, 'utf8');
console.log('2. Updated backend/src/ParkingManager.cpp');

// 3. public/js/api.js
let apiJs = fs.readFileSync('public/js/api.js', 'utf8');
apiJs = apiJs.replace("name: 'Central Campus Parking',", "name: 'Demo Parking',");
// Update _getDB sanitization
const getDBMarker = "const dummyPlotIds = new Set(['plot_phoenix_marketcity', 'plot_sriook_park', 'plot_zoob_residences', 'plot_tower_43']);";
const getDBRepl = `const dummyPlotIds = new Set(['plot_phoenix_marketcity', 'plot_sriook_park', 'plot_zoob_residences', 'plot_tower_43']);
    let changed = false;
    if (db.plots) {
      // Ensure central campus plot has been renamed to Demo Parking in cache
      const campusPlot = db.plots.find(p => p.id === 'plot_central_campus');
      if (campusPlot && campusPlot.name === 'Central Campus Parking') {
        campusPlot.name = 'Demo Parking';
        changed = true;
      }`;
apiJs = apiJs.replace(`const dummyPlotIds = new Set(['plot_phoenix_marketcity', 'plot_sriook_park', 'plot_zoob_residences', 'plot_tower_43']);
    let changed = false;
    if (db.plots) {`, getDBRepl);
fs.writeFileSync('public/js/api.js', apiJs, 'utf8');
console.log('3. Updated public/js/api.js');

// 4. public/index.html
let pIndex = fs.readFileSync('public/index.html', 'utf8');
pIndex = pIndex.replace('<div class="kpi-sub" id="dash-kpi-loc">Central Campus Parking</div>', '<div class="kpi-sub" id="dash-kpi-loc">Demo Parking</div>');
fs.writeFileSync('public/index.html', pIndex, 'utf8');
console.log('4. Updated public/index.html');

// 5. index.html
let rootIndex = fs.readFileSync('index.html', 'utf8');
rootIndex = rootIndex.replace('Central Campus Parking • Real-Time BFS Visualizer', 'Demo Parking • Real-Time BFS Visualizer');
fs.writeFileSync('index.html', rootIndex, 'utf8');
console.log('5. Updated index.html');

// 6. public/landing.html
let landing = fs.readFileSync('public/landing.html', 'utf8');
landing = landing.replace('Central Campus Parking • Real-Time BFS Visualizer', 'Demo Parking • Real-Time BFS Visualizer');
fs.writeFileSync('public/landing.html', landing, 'utf8');
console.log('6. Updated public/landing.html');
