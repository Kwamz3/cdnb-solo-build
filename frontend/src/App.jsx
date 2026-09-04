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

// Make sure this matches your backend URL
const BACKEND_URL = typeof window !== 'undefined' && window.location.hostname === 'localhost' 
  ? 'http://localhost:5000' 
  : 'https://cdnb-render-build.onrender.com';

console.log('[FRONTEND] Connecting to backend:', BACKEND_URL);

// Socket connection with more options
const socket = io(BACKEND_URL, {
  transports: ['websocket', 'polling'],
  reconnectionAttempts: 20,
  reconnectionDelay: 1000,
  reconnectionDelayMax: 5000,
  timeout: 10000
});

export default function Dashboard() {
  const [isConnected, setIsConnected] = useState(false);
  const [hasData, setHasData] = useState(false);
  const [socketError, setSocketError] = useState(null);

  const [data, setData] = useState({
    moisture: null,
    waterLevel: null,
    pumpStatus: false,
    autoMode: true,
    threshold: 30,
    rainExpected: false,
    rainProbability: 0,
    weatherCondition: '',
    alerts: [],
    sensorOnline: false
  });

  const [history, setHistory] = useState([]);
  const [thresholdInput, setThresholdInput] = useState(30);
  const [isSavingThreshold, setIsSavingThreshold] = useState(false);
  const [simMoisture, setSimMoisture] = useState(24);
  const [simWater, setSimWater] = useState(85);
  const [simLoading, setSimLoading] = useState(false);
  const [lastUpdate, setLastUpdate] = useState(null);

  // Socket event listeners
  useEffect(() => {
    console.log('[FRONTEND] Setting up Socket.IO listeners');

    socket.on('connect', () => {
      console.log('[FRONTEND] Connected to backend');
      setIsConnected(true);
      setSocketError(null);
      // Request current state
      socket.emit('requestTelemetry');
    });

    socket.on('disconnect', (reason) => {
      console.log('[FRONTEND] Disconnected from backend:', reason);
      setIsConnected(false);
    });

    socket.on('connect_error', (err) => {
      console.error('[FRONTEND] Connection error:', err);
      setIsConnected(false);
      setSocketError(err.message);
    });

    socket.on('telemetryUpdate', (updatedState) => {
      console.log('[FRONTEND] Received telemetry update:', updatedState);
      setIsConnected(true);
      setLastUpdate(new Date());
      
      if (updatedState && updatedState.moisture !== undefined && updatedState.moisture !== null) {
        setHasData(true);
        console.log(`[FRONTEND] Updating moisture: ${updatedState.moisture}%`);
      }

      setData((prev) => ({ ...prev, ...updatedState }));
      
      if (updatedState.threshold !== undefined) {
        setThresholdInput(updatedState.threshold);
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
    });

    return () => {
      console.log('[FRONTEND] Cleaning up Socket.IO listeners');
      socket.off('connect');
      socket.off('disconnect');
      socket.off('connect_error');
      socket.off('telemetryUpdate');
    };
  }, []);

  // Initial data fetch
  useEffect(() => {
    let isMounted = true;

    const fetchInitialData = async () => {
      try {
        console.log('[FRONTEND] Fetching initial data...');
        const [stateRes, historyRes] = await Promise.all([
          fetch(`${BACKEND_URL}/api/state`),
          fetch(`${BACKEND_URL}/api/history?limit=20`)
        ]);

        if (stateRes.ok) {
          const stateJson = await stateRes.json();
          console.log('[FRONTEND] Initial state:', stateJson);
          if (isMounted) {
            setData((prev) => ({ ...prev, ...stateJson }));
            setThresholdInput(stateJson.threshold || 30);
            if (stateJson.moisture !== null && stateJson.moisture !== undefined) {
              setHasData(true);
            }
          }
        }

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
          }
        }
      } catch (err) {
        console.error('[FRONTEND] Error fetching initial data:', err);
        setSocketError(err.message);
      }
    };

    fetchInitialData();

    return () => {
      isMounted = false;
    };
  }, []);

  // ... rest of your component functions (togglePump, toggleAutoMode, etc.) remain the same ...
  // Just add this debug function at the end of your component:

  // Debug function to test connection
  const testConnection = async () => {
    console.log('[FRONTEND] Testing connection...');
    try {
      const response = await fetch(`${BACKEND_URL}/api/state`);
      const data = await response.json();
      console.log('[FRONTEND] Test response:', data);
      alert(`Connection OK! Moisture: ${data.moisture}%, Pump: ${data.pumpStatus ? 'ON' : 'OFF'}`);
    } catch (err) {
      console.error('[FRONTEND] Test failed:', err);
      alert(`Connection failed: ${err.message}`);
    }
  };

  // Add a debug button in your UI
  // ... inside the return statement, add this button somewhere:

  return (
    <div className="p-6 md:p-10 bg-gray-900 text-white min-h-screen font-sans">
      <div className="max-w-7xl mx-auto">
        {/* Add this debug button at the top */}
        <div className="mb-4 flex justify-end">
          <button 
            onClick={testConnection}
            className="px-3 py-1 bg-gray-700 hover:bg-gray-600 rounded text-xs"
          >
            Test Connection
          </button>
          {lastUpdate && (
            <span className="text-xs text-gray-400 ml-2">
              Last update: {lastUpdate.toLocaleTimeString()}
            </span>
          )}
        </div>
        
        {/* Rest of your existing JSX */}
        {/* ... */}
      </div>
    </div>
  );
}