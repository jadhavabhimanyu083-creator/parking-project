const fs = require('fs');

// 1. Clean data/plots.json
const plots = JSON.parse(fs.readFileSync('data/plots.json', 'utf8'));
const filteredPlots = plots.filter(p => p.id !== 'plot_1792918456776495000' && p.id !== 'plot_phoenix_marketcity' && p.id !== 'plot_sriook_park' && p.id !== 'plot_zoob_residences' && p.id !== 'plot_tower_43');

for (const plot of filteredPlots) {
  if (plot.id === 'plot_central_campus') {
    plot.name = 'Demo Parking';
    if (plot.grid) {
      for (const row of plot.grid) {
        for (const cell of row) {
          if (cell.type === 'SLOT_CAR' || cell.type === 'SLOT_BIKE') {
            cell.status = 'AVAILABLE';
            cell.plate = '';
          }
        }
      }
    }
  }
}

fs.writeFileSync('data/plots.json', JSON.stringify(filteredPlots, null, 2), 'utf8');
console.log('1. Cleaned data/plots.json:', filteredPlots.map(p => ({ id: p.id, name: p.name })));

// 2. Clean data/sessions.json
fs.writeFileSync('data/sessions.json', '[]\n', 'utf8');
console.log('2. Emptied data/sessions.json');
