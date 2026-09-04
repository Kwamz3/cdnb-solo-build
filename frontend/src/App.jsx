import React, { useState, useEffect } from 'react';
import { io } from 'socket.io-client';
import { LineChart, Line, XAxis, YAxis, Tooltip, ResponsiveContainer, CartesianGrid } from 'recharts';
import { 
  Droplets, 
  Power, 
  CloudRain, 
  Sun, 
  AlertTriangle, 
  Sliders, 
  RefreshCw,
  WifiOff
} from 'lucide-react';

// DYNAMIC BACKEND URL - Change this based on your setup
// For local development:
const BACKEND_URL = 'http://localhost:5000';
// For production (Render):
// const BACKEND_URL = 'https://cdnb-render-build.onrender.com';

console.log('[FRONTEND] Connecting to backend:', BACKEND_URL);

// Socket connection with more options
const socket = io(BACKEND_URL, {
  transports: ['websocket', 'polling'],
  reconnectionAttempts: 20,
  reconnectionDelay: 1000,
  reconnectionDelayMax: 5000,
  timeout: 10000,
  autoConnect: true
});

export default function Dashboard() {
  const [isConnected, setIsConnected] = useState(false);
  const [hasData, setHasData] = useState(false);
  const [socketError, setSocketError] = useState(null);
  const [loading, setLoading] = useState(true);

  const [data, setData] = useState({
    moisture: 0,
    waterLevel: 100,
    pumpStatus: false,
    autoMode: true,
    threshold: 30,
    rainExpected: false,
    rainProbability: 0,
    weatherCondition: 'Clear',
    alerts: [],
    sensorOnline: false,
    temperature: 0
  });

  const [history, setHistory] = useState([]);
  const [thresholdInput, setThresholdInput] = useState(30);
  const [isSavingThreshold, setIsSavingThreshold] = useState(false);
  const [simMoisture, setSimMoisture] = useState(25);
  const [simWater, setSimWater] = useState(85);
  const [simLoading, setSimLoading] = useState(false);
  const [lastUpdate, setLastUpdate] = useState(null);
  const [debugInfo, setDebugInfo] = useState('');

  // Socket event listeners
  useEffect(() => {
    console.log('[FRONTEND] Setting up Socket.IO listeners');

    const onConnect = () => {
      console.log('[FRONTEND] ✅ Connected to backend');
      setIsConnected(true);
      setSocketError(null);
      setDebugInfo('Connected to backend');
      // Request current state
      socket.emit('requestTelemetry');
    };

    const onDisconnect = (reason) => {
      console.log('[FRONTEND] ❌ Disconnected from backend:', reason);
      setIsConnected(false);
      setDebugInfo(`Disconnected: ${reason}`);
    };

    const onConnectError = (err) => {
      console.error('[FRONTEND] ❌ Connection error:', err);
      setIsConnected(false);
      setSocketError(err.message);
      setDebugInfo(`Connection error: ${err.message}`);
    };

    const onTelemetryUpdate = (updatedState) => {
      console.log('[FRONTEND] 📡 Received telemetry update:', updatedState);
      setIsConnected(true);
      setLastUpdate(new Date());
      setDebugInfo(`Last update: ${new Date().toLocaleTimeString()}`);
      
      // Update data
      if (updatedState) {
        // Ensure we have valid numbers
        const newState = {
          ...data,
          ...updatedState,
          moisture: updatedState.moisture !== undefined && updatedState.moisture !== null 
            ? Number(updatedState.moisture) 
            : data.moisture,
          waterLevel: updatedState.waterLevel !== undefined && updatedState.waterLevel !== null
            ? Number(updatedState.waterLevel)
            : data.waterLevel,
          pumpStatus: updatedState.pumpStatus !== undefined 
            ? Boolean(updatedState.pumpStatus)
            : data.pumpStatus,
          threshold: updatedState.threshold !== undefined
            ? Number(updatedState.threshold)
            : data.threshold
        };
        
        setData(newState);
        setHasData(true);
        
        if (updatedState.threshold !== undefined) {
          setThresholdInput(Number(updatedState.threshold));
        }

        // Update history chart
        if (updatedState.moisture !== undefined && updatedState.moisture !== null) {
          setHistory((prev) => {
            const newEntry = {
              time: new Date().toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' }),
              moisture: Number(updatedState.moisture) || 0
            };
            const newHistory = [...prev, newEntry];
            // Keep last 30 points
            return newHistory.slice(-30);
          });
        }
      }
    };

    socket.on('connect', onConnect);
    socket.on('disconnect', onDisconnect);
    socket.on('connect_error', onConnectError);
    socket.on('telemetryUpdate', onTelemetryUpdate);

    return () => {
      console.log('[FRONTEND] Cleaning up Socket.IO listeners');
      socket.off('connect', onConnect);
      socket.off('disconnect', onDisconnect);
      socket.off('connect_error', onConnectError);
      socket.off('telemetryUpdate', onTelemetryUpdate);
    };
  }, []);

  // Initial data fetch
  useEffect(() => {
    let isMounted = true;

    const fetchInitialData = async () => {
      try {
        setLoading(true);
        console.log('[FRONTEND] 🔄 Fetching initial data...');
        
        // Try to fetch state
        try {
          const stateRes = await fetch(`${BACKEND_URL}/api/state`, {
            method: 'GET',
            headers: {
              'Content-Type': 'application/json',
            },
          });
          
          if (stateRes.ok) {
            const stateJson = await stateRes.json();
            console.log('[FRONTEND] Initial state:', stateJson);
            if (isMounted) {
              setData(prev => ({ ...prev, ...stateJson }));
              setThresholdInput(stateJson.threshold || 30);
              if (stateJson.moisture !== null && stateJson.moisture !== undefined) {
                setHasData(true);
                setDebugInfo(`Loaded state: moisture=${stateJson.moisture}%`);
              }
            }
          } else {
            console.warn('[FRONTEND] State fetch returned:', stateRes.status);
            setDebugInfo(`State fetch failed: ${stateRes.status}`);
          }
        } catch (err) {
          console.warn('[FRONTEND] Could not fetch state:', err.message);
          setDebugInfo(`Cannot fetch state: ${err.message}`);
        }

        // Try to fetch history
        try {
          const historyRes = await fetch(`${BACKEND_URL}/api/history?limit=20`);
          if (historyRes.ok) {
            const historyJson = await historyRes.json();
            if (isMounted && Array.isArray(historyJson) && historyJson.length > 0) {
              const formatted = historyJson.map((item) => ({
                time: item.timestamp 
                  ? new Date(item.timestamp).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' }) 
                  : 'Earlier',
                moisture: Number(item.moisture) || 0
              }));
              setHistory(formatted);
              setHasData(true);
            }
          }
        } catch (err) {
          console.warn('[FRONTEND] Could not fetch history:', err.message);
        }
      } catch (err) {
        console.error('[FRONTEND] Error fetching initial data:', err);
        setSocketError(err.message);
        setDebugInfo(`Error: ${err.message}`);
      } finally {
        setLoading(false);
      }
    };

    fetchInitialData();

    return () => {
      isMounted = false;
    };
  }, []);

  // Controls: Toggle pump
  const togglePump = async () => {
    const newStatus = !data.pumpStatus;
    try {
      const response = await fetch(`${BACKEND_URL}/api/controls`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ pumpStatus: newStatus, autoMode: false })
      });
      if (response.ok) {
        const result = await response.json();
        console.log('[FRONTEND] Pump toggled:', result);
        setData(prev => ({ ...prev, pumpStatus: newStatus, autoMode: false }));
        setDebugInfo(`Pump ${newStatus ? 'ON' : 'OFF'}`);
      }
    } catch (err) {
      console.error('[FRONTEND] Failed to toggle pump:', err);
      setDebugInfo(`Error toggling pump: ${err.message}`);
    }
  };

  // Controls: Toggle Auto / Manual mode
  const toggleAutoMode = async () => {
    const newMode = !data.autoMode;
    try {
      const response = await fetch(`${BACKEND_URL}/api/controls`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ autoMode: newMode })
      });
      if (response.ok) {
        const result = await response.json();
        console.log('[FRONTEND] Auto mode toggled:', result);
        setData(prev => ({ ...prev, autoMode: newMode }));
        setDebugInfo(`Auto mode: ${newMode ? 'ON' : 'OFF'}`);
      }
    } catch (err) {
      console.error('[FRONTEND] Failed to toggle auto mode:', err);
      setDebugInfo(`Error toggling auto mode: ${err.message}`);
    }
  };

  // Controls: Update threshold
  const saveThreshold = async (newVal) => {
    setIsSavingThreshold(true);
    try {
      const response = await fetch(`${BACKEND_URL}/api/controls`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ threshold: Number(newVal) })
      });
      if (response.ok) {
        const result = await response.json();
        console.log('[FRONTEND] Threshold updated:', result);
        setData(prev => ({ ...prev, threshold: Number(newVal) }));
        setDebugInfo(`Threshold set to ${newVal}%`);
      }
    } catch (err) {
      console.error('[FRONTEND] Failed to update threshold:', err);
      setDebugInfo(`Error updating threshold: ${err.message}`);
    } finally {
      setIsSavingThreshold(false);
    }
  };

  // Hardware Simulator
  const sendSimulatedTelemetry = async () => {
    setSimLoading(true);
    try {
      const response = await fetch(`${BACKEND_URL}/api/telemetry`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          moisture: Number(simMoisture),
          waterLevel: Number(simWater)
        })
      });
      if (response.ok) {
        const result = await response.json();
        console.log('[FRONTEND] Simulated telemetry sent:', result);
        setIsConnected(true);
        setHasData(true);
        setDebugInfo(`Simulated: moisture=${simMoisture}%, water=${simWater}%`);
      }
    } catch (err) {
      console.error('[FRONTEND] Failed to post simulated telemetry:', err);
      setDebugInfo(`Simulation error: ${err.message}`);
    } finally {
      setSimLoading(false);
    }
  };

  // Debug function to test connection
  const testConnection = async () => {
    console.log('[FRONTEND] 🔍 Testing connection...');
    try {
      const response = await fetch(`${BACKEND_URL}/api/state`);
      if (response.ok) {
        const stateData = await response.json();
        console.log('[FRONTEND] ✅ Test response:', stateData);
        setDebugInfo(`✅ Connected! Moisture: ${stateData.moisture}%, Pump: ${stateData.pumpStatus ? 'ON' : 'OFF'}`);
        alert(`✅ Connection OK!\nMoisture: ${stateData.moisture}%\nPump: ${stateData.pumpStatus ? 'ON' : 'OFF'}\nAuto Mode: ${stateData.autoMode ? 'ON' : 'OFF'}`);
      } else {
        setDebugInfo(`❌ Test failed: ${response.status}`);
        alert(`❌ Connection failed!\nStatus: ${response.status}`);
      }
    } catch (err) {
      console.error('[FRONTEND] ❌ Test failed:', err);
      setDebugInfo(`❌ Test error: ${err.message}`);
      alert(`❌ Connection error!\n${err.message}\n\nMake sure the backend is running on ${BACKEND_URL}`);
    }
  };

  // Helper to check if data is available
  const isServerReady = isConnected && hasData;

  return (
    <div className="p-6 md:p-10 bg-gray-900 text-white min-h-screen font-sans">
      <div className="max-w-7xl mx-auto">
        
        {/* Top Header */}
        <div className="flex flex-col md:flex-row md:items-center justify-between gap-4 mb-8 pb-6 border-b border-gray-800">
          <div>
            <div className="flex items-center gap-3">
              <Droplets className="w-8 h-8 text-green-400" />
              <h1 className="text-3xl font-bold tracking-tight text-green-400">Smart Irrigation Control</h1>
            </div>
            <p className="text-gray-400 text-sm mt-1">
              Autonomous moisture threshold management
            </p>
            {debugInfo && (
              <p className="text-xs text-blue-400 mt-1 font-mono">{debugInfo}</p>
            )}
          </div>

          {/* Connection Status & Auto Mode Badges */}
          <div className="flex flex-wrap items-center gap-3">
            <div className={`px-3 py-1.5 rounded-full text-xs font-semibold flex items-center gap-2 border ${
              !isConnected 
                ? 'bg-amber-950/60 border-amber-500/40 text-amber-300 animate-pulse'
                : data.sensorOnline 
                  ? 'bg-green-950/60 border-green-500/40 text-green-300' 
                  : 'bg-amber-950/60 border-amber-500/40 text-amber-300'
            }`}>
              <span className={`w-2 h-2 rounded-full ${
                !isConnected 
                  ? 'bg-amber-400' 
                  : data.sensorOnline 
                    ? 'bg-green-400 animate-pulse' 
                    : 'bg-amber-400'
              }`}></span>
              {!isConnected 
                ? '⚠️ Connecting...' 
                : data.sensorOnline 
                  ? '✅ Hardware Online' 
                  : '⚠️ Hardware Offline'}
            </div>

            <button
              onClick={toggleAutoMode}
              className={`px-4 py-1.5 rounded-full text-xs font-bold transition flex items-center gap-2 border ${
                data.autoMode 
                  ? 'bg-emerald-600/20 border-emerald-500 text-emerald-300 hover:bg-emerald-600/30' 
                  : 'bg-yellow-600/20 border-yellow-500 text-yellow-300 hover:bg-yellow-600/30'
              }`}
            >
              <Sliders className="w-3.5 h-3.5" />
              Mode: {data.autoMode ? 'AUTOMATIC' : 'MANUAL'}
            </button>

            <button
              onClick={testConnection}
              className="px-3 py-1 bg-gray-700 hover:bg-gray-600 rounded text-xs"
            >
              🔍 Test
            </button>
          </div>
        </div>

        {/* Alert Notification Banner */}
        {data.alerts && data.alerts.length > 0 && (
          <div className="mb-6 space-y-2">
            {data.alerts.map((alert, idx) => (
              <div 
                key={idx} 
                className={`p-4 rounded-xl border flex items-center gap-3 ${
                  alert.severity === 'danger' 
                    ? 'bg-red-950/50 border-red-500/50 text-red-200' 
                    : 'bg-amber-950/50 border-amber-500/50 text-amber-200'
                }`}
              >
                <AlertTriangle className="w-5 h-5 flex-shrink-0" />
                <span className="text-sm font-medium">{alert.message}</span>
              </div>
            ))}
          </div>
        )}

        {/* Weather Rain-Skip Alert Banner */}
        <div className={`mb-8 p-4 rounded-xl border flex flex-col sm:flex-row sm:items-center justify-between gap-4 ${
          data.rainExpected 
            ? 'bg-blue-950/40 border-blue-500/40 text-blue-200' 
            : 'bg-gray-800/80 border-gray-700 text-gray-300'
        }`}>
          <div className="flex items-center gap-3">
            {data.rainExpected ? (
              <CloudRain className="w-6 h-6 text-blue-400 animate-bounce" />
            ) : (
              <Sun className="w-6 h-6 text-amber-400" />
            )}
            <div>
              <p className="font-semibold text-sm">
                Local Weather: {data.weatherCondition || 'Loading...'}
                {data.rainProbability !== undefined && data.rainProbability > 0 ? ` (Rain: ${data.rainProbability}%)` : ''}
              </p>
              <p className="text-xs text-gray-400">
                {data.rainExpected 
                  ? '🌧️ Rain forecast! Auto-watering SKIPPED to conserve water.' 
                  : '☀️ No rain forecast. Threshold-based watering active.'}
              </p>
            </div>
          </div>
          <span className={`px-3 py-1 rounded text-xs font-bold self-start sm:self-auto ${
            data.rainExpected ? 'bg-blue-600 text-white' : 'bg-gray-700 text-gray-300'
          }`}>
            {data.rainExpected ? 'Rain Skip: ACTIVE' : 'Rain Skip: OFF'}
          </span>
        </div>

        {/* Primary Metrics Row */}
        <div className="grid grid-cols-1 md:grid-cols-3 gap-6 mb-8">
          
          {/* Soil Moisture Card */}
          <div className="bg-gray-800 p-6 rounded-xl border border-gray-700 flex flex-col justify-between min-h-[220px]">
            <div className="flex justify-between items-start">
              <p className="text-gray-400 font-medium">Soil Moisture</p>
              <Droplets className="w-5 h-5 text-blue-400" />
            </div>

            {isServerReady ? (
              <>
                <div className="my-4">
                  <p className="text-4xl font-extrabold text-blue-400">{data.moisture || 0}%</p>
                  <div className="w-full bg-gray-700 h-2 rounded-full mt-3 overflow-hidden">
                    <div 
                      className={`h-full transition-all duration-500 ${
                        (data.moisture || 0) < (data.threshold || 30) ? 'bg-red-500' : 'bg-blue-400'
                      }`} 
                      style={{ width: `${Math.min(100, Math.max(0, data.moisture || 0))}%` }}
                    />
                  </div>
                </div>
                <p className="text-xs text-gray-400">
                  {(data.moisture || 0) < (data.threshold || 30) ? '⚠️ Soil is dry (Needs water)' : '✅ Moisture level optimal'}
                </p>
              </>
            ) : (
              <div className="my-auto py-6 text-center flex flex-col items-center justify-center">
                <WifiOff className="w-8 h-8 text-gray-500 mb-2 animate-pulse" />
                <p className="text-xs text-amber-300 font-medium">⏳ Waiting for data...</p>
                <p className="text-xs text-gray-500 mt-1">Connect to backend</p>
              </div>
            )}
          </div>

          {/* Water Reservoir Level Card */}
          <div className="bg-gray-800 p-6 rounded-xl border border-gray-700 flex flex-col justify-between min-h-[220px]">
            <div className="flex justify-between items-start">
              <p className="text-gray-400 font-medium">Water Reservoir</p>
              <Droplets className="w-5 h-5 text-cyan-400" />
            </div>

            {isServerReady ? (
              <>
                <div className="my-4">
                  <p className="text-4xl font-extrabold text-cyan-400">{data.waterLevel || 100}%</p>
                  <div className="w-full bg-gray-700 h-2 rounded-full mt-3 overflow-hidden">
                    <div 
                      className={`h-full transition-all duration-500 ${
                        (data.waterLevel || 100) < 25 ? 'bg-red-500' : 'bg-cyan-400'
                      }`} 
                      style={{ width: `${Math.min(100, Math.max(0, data.waterLevel || 100))}%` }}
                    />
                  </div>
                </div>
                <p className="text-xs text-gray-400">
                  {(data.waterLevel || 100) < 25 ? '⚠️ Refill needed soon' : '✅ Water supply sufficient'}
                </p>
              </>
            ) : (
              <div className="my-auto py-6 text-center flex flex-col items-center justify-center">
                <WifiOff className="w-8 h-8 text-gray-500 mb-2 animate-pulse" />
                <p className="text-xs text-amber-300 font-medium">⏳ Waiting for data...</p>
              </div>
            )}
          </div>

          {/* Pump Status & Control Card */}
          <div className="bg-gray-800 p-6 rounded-xl border border-gray-700 flex flex-col justify-between min-h-[220px]">
            <div className="flex justify-between items-start">
              <div>
                <p className="text-gray-400 font-medium">Pump Status</p>
                <p className={`text-2xl font-bold mt-1 ${
                  isServerReady 
                    ? (data.pumpStatus ? 'text-green-400' : 'text-red-400')
                    : 'text-gray-500'
                }`}>
                  {isServerReady ? (data.pumpStatus ? '🟢 ACTIVE' : '🔴 IDLE') : '⏳ LOADING...'}
                </p>
              </div>
              <Power className={`w-6 h-6 ${
                isServerReady && data.pumpStatus ? 'text-green-400 animate-pulse' : 'text-gray-500'
              }`} />
            </div>

            {isServerReady ? (
              <div className="mt-4">
                <button 
                  onClick={togglePump} 
                  className={`w-full px-4 py-2.5 rounded-lg font-bold transition shadow-md flex items-center justify-center gap-2 ${
                    data.pumpStatus 
                      ? 'bg-red-600 hover:bg-red-500 active:bg-red-700' 
                      : 'bg-green-600 hover:bg-green-500 active:bg-green-700'
                  }`}
                >
                  <Power className="w-4 h-4" />
                  {data.pumpStatus ? 'Stop Pump' : 'Start Pump'}
                </button>
                <p className="text-[11px] text-gray-400 text-center mt-2">
                  Clicking overrides to Manual Mode
                </p>
              </div>
            ) : (
              <div className="my-auto py-2 text-center flex flex-col items-center justify-center">
                <WifiOff className="w-6 h-6 text-gray-500 mb-1 animate-pulse" />
                <p className="text-xs text-amber-300 font-medium">⏳ Waiting for connection...</p>
              </div>
            )}
          </div>

        </div>

        {/* Real-time Telemetry History Chart */}
        <div className="bg-gray-800 p-6 rounded-xl border border-gray-700 mb-8">
          <div className="flex flex-col sm:flex-row sm:items-center justify-between gap-2 mb-6">
            <div>
              <h2 className="text-lg font-semibold text-white">Live Telemetry History</h2>
              <p className="text-xs text-gray-400">
                {isConnected ? '🟢 Real-time updates via WebSocket' : '🔴 Waiting for connection...'}
              </p>
            </div>
            <div className="flex items-center gap-4 text-xs">
              <span className="flex items-center gap-1.5 text-blue-400">
                <span className="w-3 h-0.5 bg-blue-400 inline-block"></span> Soil Moisture (%)
              </span>
            </div>
          </div>

          {history.length > 0 ? (
            <div className="h-72 w-full">
              <ResponsiveContainer width="100%" height="100%">
                <LineChart data={history}>
                  <CartesianGrid strokeDasharray="3 3" stroke="#374151" />
                  <XAxis dataKey="time" stroke="#9CA3AF" fontSize={12} tickLine={false} />
                  <YAxis domain={[0, 100]} stroke="#9CA3AF" fontSize={12} tickLine={false} />
                  <Tooltip 
                    contentStyle={{ backgroundColor: '#1F2937', border: '1px solid #374151', borderRadius: '8px', color: '#fff' }}
                    labelStyle={{ color: '#9CA3AF', marginBottom: '4px' }}
                  />
                  <Line 
                    type="monotone" 
                    dataKey="moisture" 
                    name="Moisture (%)"
                    stroke="#60A5FA" 
                    strokeWidth={3} 
                    dot={{ r: 3, fill: '#60A5FA' }} 
                    activeDot={{ r: 6 }} 
                  />
                </LineChart>
              </ResponsiveContainer>
            </div>
          ) : (
            <div className="h-72 w-full flex flex-col items-center justify-center border border-dashed border-gray-700/80 rounded-xl bg-gray-900/40">
              <WifiOff className="w-10 h-10 text-gray-600 mb-3 animate-pulse" />
              <p className="text-sm font-semibold text-amber-300">⏳ No data yet</p>
              <span className="text-xs text-gray-500 mt-1">Send telemetry or wait for sensor data</span>
            </div>
          )}
        </div>

        {/* Configuration & Simulation Row */}
        <div className="grid grid-cols-1 md:grid-cols-2 gap-6">
          
          {/* Threshold & Automation Controls */}
          <div className="bg-gray-800 p-6 rounded-xl border border-gray-700">
            <div className="flex items-center gap-2 mb-4">
              <Sliders className="w-5 h-5 text-green-400" />
              <h2 className="text-lg font-semibold">Irrigation Settings</h2>
            </div>
            
            <div className="space-y-4">
              <div>
                <div className="flex justify-between items-center mb-1">
                  <label className="text-sm text-gray-300 font-medium">Moisture Trigger Threshold</label>
                  <span className="text-sm font-bold text-green-400">{thresholdInput}%</span>
                </div>
                <input 
                  type="range" 
                  min="10" 
                  max="70" 
                  value={thresholdInput} 
                  onChange={(e) => setThresholdInput(e.target.value)}
                  onMouseUp={(e) => saveThreshold(e.target.value)}
                  onTouchEnd={(e) => saveThreshold(e.target.value)}
                  className="w-full h-2 bg-gray-700 rounded-lg appearance-none cursor-pointer accent-green-500"
                  disabled={isSavingThreshold}
                />
                <p className="text-xs text-gray-400 mt-1">
                  When soil moisture drops below {thresholdInput}%, pump activates automatically (unless rain is expected).
                </p>
                {isSavingThreshold && (
                  <p className="text-xs text-blue-400 mt-1">Saving...</p>
                )}
              </div>

              <div className="pt-2 border-t border-gray-700/60 flex items-center justify-between">
                <span className="text-sm text-gray-300">Auto Mode Override</span>
                <button
                  onClick={toggleAutoMode}
                  className={`px-4 py-2 rounded-lg font-bold text-xs transition ${
                    data.autoMode 
                      ? 'bg-emerald-600 hover:bg-emerald-500 text-white' 
                      : 'bg-gray-700 hover:bg-gray-600 text-gray-300'
                  }`}
                >
                  {data.autoMode ? '✅ Auto Mode ENABLED' : '✋ Manual Mode ENABLED'}
                </button>
              </div>
            </div>
          </div>

          {/* IoT Telemetry Simulator */}
          <div className="bg-gray-800 p-6 rounded-xl border border-gray-700">
            <div className="flex items-center justify-between mb-4">
              <div className="flex items-center gap-2">
                <RefreshCw className="w-5 h-5 text-blue-400" />
                <h2 className="text-lg font-semibold">Hardware Telemetry Simulator</h2>
              </div>
              <span className="text-xs bg-blue-900/50 text-blue-300 px-2 py-0.5 rounded border border-blue-700/50">
                IoT Test
              </span>
            </div>

            <p className="text-xs text-gray-400 mb-4">
              Simulate hardware payload postings to <code className="bg-gray-900 px-1 py-0.5 rounded text-gray-300">POST /api/telemetry</code> to verify live WebSocket broadcasts.
            </p>

            <div className="grid grid-cols-2 gap-3 mb-4">
              <div>
                <label className="text-xs text-gray-400 block mb-1">Moisture (%)</label>
                <input 
                  type="number" 
                  value={simMoisture} 
                  onChange={(e) => setSimMoisture(Number(e.target.value))}
                  className="w-full bg-gray-900 border border-gray-700 rounded px-2.5 py-1.5 text-sm text-white"
                />
              </div>
              <div>
                <label className="text-xs text-gray-400 block mb-1">Water Reservoir (%)</label>
                <input 
                  type="number" 
                  value={simWater} 
                  onChange={(e) => setSimWater(Number(e.target.value))}
                  className="w-full bg-gray-900 border border-gray-700 rounded px-2.5 py-1.5 text-sm text-white"
                />
              </div>
            </div>

            <button 
              onClick={sendSimulatedTelemetry}
              disabled={simLoading}
              className="w-full bg-blue-600 hover:bg-blue-500 active:bg-blue-700 disabled:opacity-50 text-white font-bold py-2 rounded-lg text-xs transition flex items-center justify-center gap-2"
            >
              <RefreshCw className={`w-3.5 h-3.5 ${simLoading ? 'animate-spin' : ''}`} />
              {simLoading ? 'Sending...' : 'Send Simulated Hardware Telemetry'}
            </button>
          </div>

        </div>

      </div>
    </div>
  );
}