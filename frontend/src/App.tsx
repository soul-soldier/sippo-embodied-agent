import { useState } from 'react'
import './App.css'

type ColorKey = 'cyan' | 'pink' | 'green' | 'off'

type ArduinoResponse = {
  status: 'ok' | 'error'
  color?: string
  message?: string
}

type ColorCommand = {
  key: ColorKey
  label: string
  hex?: string
  path: string
}

const colorCommands: ColorCommand[] = [
  {
    key: 'cyan',
    label: 'Cyan',
    hex: '#00C9CC',
    path: '/api/color/cyan',
  },
  {
    key: 'pink',
    label: 'Pink',
    hex: '#FC00B7',
    path: '/api/color/pink',
  },
  {
    key: 'green',
    label: 'Green',
    hex: '#01FF00',
    path: '/api/color/green',
  },
  {
    key: 'off',
    label: 'Off',
    path: '/api/off',
  },
]

function App() {
  const [arduinoBaseUrl, setArduinoBaseUrl] = useState(
    import.meta.env.VITE_ARDUINO_BASE_URL || 'http://192.168.178.76',
  )

  const [activeColor, setActiveColor] = useState<ColorKey | null>(null)
  const [statusMessage, setStatusMessage] = useState('Ready')
  const [errorMessage, setErrorMessage] = useState<string | null>(null)
  const [isSending, setIsSending] = useState(false)

  async function sendColorCommand(command: ColorCommand) {
    setIsSending(true)
    setErrorMessage(null)
    setStatusMessage(`Sending "${command.label}" command...`)

    try {
      const response = await fetch(`${arduinoBaseUrl}${command.path}`)

      if (!response.ok) {
        throw new Error(`Arduino responded with HTTP ${response.status}`)
      }

      const data = (await response.json()) as ArduinoResponse

      if (data.status !== 'ok') {
        throw new Error(data.message || 'Arduino returned an error')
      }

      setActiveColor(command.key)
      setStatusMessage(
        command.key === 'off'
          ? 'RGB LED turned off'
          : `RGB LED changed to ${command.label}`,
      )
    } catch (error) {
      const message =
        error instanceof Error ? error.message : 'Unknown connection error'

      setErrorMessage(message)
      setStatusMessage('Could not reach Arduino')
    } finally {
      setIsSending(false)
    }
  }

  return (
    <main className="app-shell">
      <section className="card">
        <p className="eyebrow">Sippo Embodied Agent</p>

        <h1>RGB LED Control</h1>

        <p className="description">
          This React app sends commands to the Arduino Wi-Fi backend.
        </p>

        <label className="field">
          <span>Arduino backend URL</span>
          <input
            value={arduinoBaseUrl}
            onChange={(event) => setArduinoBaseUrl(event.target.value)}
            placeholder="http://192.168.178.76"
          />
        </label>

        <div className="button-grid">
          {colorCommands.map((command) => (
            <button
              key={command.key}
              type="button"
              className={`color-button ${
                activeColor === command.key ? 'active' : ''
              }`}
              onClick={() => sendColorCommand(command)}
              disabled={isSending}
            >
              <span
                className="swatch"
                style={{
                  backgroundColor: command.hex || 'transparent',
                }}
              />
              {command.label}
            </button>
          ))}
        </div>

        <div className="status-box">
          <strong>Status:</strong> {statusMessage}
        </div>

        {errorMessage && (
          <div className="error-box">
            <strong>Error:</strong> {errorMessage}
          </div>
        )}
      </section>
    </main>
  )
}

export default App