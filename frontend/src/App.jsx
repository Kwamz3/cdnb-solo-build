// src/App.jsx
import { useState, useEffect } from 'react';
import { Droplets, Power, Activity, Play, Square, WifiOff } from 'lucide-react';
import { LineChart, Line, XAxis, YAxis, Tooltip, ResponsiveContainer } from 'recharts';
import './App.css';

function App() {
  const [moisture, setMoisture] = useState(null);
  const [pumpStatus, setPumpStatus] = useState('OFF');
  const [history, setHistory] = useState([]);
  const [lastUpdate, setLastUpdate] = useState(null);

  // Fetch real data from backend
  useEffect(() => {
    const fetchData = async () => {
      try {
        const response = await fetch('https://cdnb-render-build.onrender.com/api/moisture/latest');
        const data = await response.json();
        
        if (data && data.moisture !== undefined) {
          setMoisture(data.moisture);
          setPumpStatus(data.pump_status || 'OFF');
          setLastUpdate(new Date(data.timestamp || Date.now()));
          
          setHistory(prev => [
            ...prev.slice(-9), 
            { 
              time: new Date(data.timestamp || Date.now()).toLocaleTimeString(), 
              moisture: data.moisture 
            }
          ]);
        }
      } catch {
        // Backend not available - that's okay, we'll just show "No data"
        console.log('Waiting for backend connection...');
      }
    };

    fetchData(); 
    const interval = setInterval(fetchData, 5000); 
    
    return () => clearInterval(interval);
  }, []);

  const togglePump = async (action) => {
    try {
      await fetch('https://cdnb-render-build.onrender.com/api/pump/control', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ action })
      });
      setPumpStatus(action);
    } catch (error) {
      console.error('Failed to control pump:', error);
      alert('Backend not connected. Pump control unavailable.');
    }
  };

  return (
    <div className="dashboard-container">
      <header className="dashboard-header">
        <div className="logo">
          <Droplets size={28} className="logo-icon" />
          <h1>Smart Irrigation Control</h1>
        </div>
        <div className="system-status">
          <span className={`status-dot ${moisture !== null ? 'live' : 'offline'}`}></span>
          {moisture !== null ? 'System Online' : 'Waiting for Sensor'}
        </div>
      </header>

      <main className="dashboard-grid">
        {/* Moisture Card */}
        <div className="card moisture-card">
          <div className="card-header">
            <h2>Soil Moisture</h2>
            <Droplets size={24} className="card-icon" />
          </div>
          
          {moisture !== null ? (
            <>
              <div className="big-data">
                <span className="value">{moisture}</span>
                <span className="unit">%</span>
              </div>
              <div className={`status-text ${moisture < 40 ? 'warning' : 'good'}`}>
                {moisture < 40 ? '⚠️ Soil is Dry' : '✅ Moisture Optimal'}
              </div>
              {lastUpdate && (
                <div className="last-update">
                  Updated: {lastUpdate.toLocaleTimeString()}
                </div>
              )}
            </>
          ) : (
            <div className="no-data">
              <WifiOff size={48} className="no-data-icon" />
              <p>No sensor data available</p>
              <span>Waiting for ESP32 connection...</span>
            </div>
          )}
        </div>

        {/* Pump Control Card */}
        <div className="card pump-card">
          <div className="card-header">
            <h2>Water Pump</h2>
            <Power size={24} className={`card-icon ${pumpStatus === 'ON' ? 'active' : ''}`} />
          </div>
          
          {moisture !== null ? (
            <>
              <div className="big-data">
                <span className={`value ${pumpStatus === 'ON' ? 'active-text' : ''}`}>
                  {pumpStatus}
                </span>
              </div>
              <div className="controls">
                <button 
                  className={`btn start ${pumpStatus === 'ON' ? 'disabled' : ''}`} 
                  onClick={() => togglePump('ON')}
                  disabled={pumpStatus === 'ON'}
                >
                  <Play size={16} /> Start
                </button>
                <button 
                  className={`btn stop ${pumpStatus === 'OFF' ? 'disabled' : ''}`} 
                  onClick={() => togglePump('OFF')}
                  disabled={pumpStatus === 'OFF'}
                >
                  <Square size={16} /> Stop
                </button>
              </div>
            </>
          ) : (
            <div className="no-data">
              <WifiOff size={48} className="no-data-icon" />
              <p>Control unavailable</p>
              <span>Connect to backend to control pump</span>
            </div>
          )}
        </div>

        {/* Chart Card */}
        <div className="card chart-card">
          <div className="card-header">
            <h2>Moisture History</h2>
            <Activity size={24} className="card-icon" />
          </div>
          
          {history.length > 0 ? (
            <div className="chart-wrapper">
              <ResponsiveContainer width="100%" height="100%">
                <LineChart data={history}>
                  <XAxis dataKey="time" stroke="#8B9A8B" fontSize={12} tickLine={false} axisLine={false} />
                  <YAxis stroke="#8B9A8B" fontSize={12} tickLine={false} axisLine={false} domain={[0, 100]} />
                  <Tooltip 
                    contentStyle={{ backgroundColor: '#04120a', border: '1px solid #10b981', borderRadius: '8px', color: '#fff' }}
                    labelStyle={{ color: '#fbbf24' }}
                  />
                  <Line 
                    type="monotone" 
                    dataKey="moisture" 
                    stroke="#10b981" 
                    strokeWidth={3} 
                    dot={{ fill: '#fbbf24', r: 4 }} 
                    activeDot={{ r: 6 }} 
                  />
                </LineChart>
              </ResponsiveContainer>
            </div>
          ) : (
            <div className="no-data chart-no-data">
              <WifiOff size={48} className="no-data-icon" />
              <p>No historical data</p>
              <span>Data will appear once sensor is connected</span>
            </div>
          )}
        </div>
      </main>
    </div>
  );
}

export default App;