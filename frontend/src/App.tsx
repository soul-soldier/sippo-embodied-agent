import { useEffect, useRef, useState } from 'react'
import './App.css'

type SippoMood =
  | 'content'
  | 'happy'
  | 'sad1'
  | 'sad2'
  | 'sad3'
  | 'sleeping'
  | 'goal'
  | 'refill'
  | 'empty'

type SippoMode = 'awake' | 'sleeping'

type SippoGifKey = 'idle' | 'fallingAsleep' | 'sleeping' | 'wakingUp'

type SippoStateResponse = {
  status: 'ok' | 'error'
  message?: string
  mode: SippoMode
  mood: SippoMood
  colorHex: string
  reminderLevel: number
  totalDrankMl: number
  dailyGoalMl: number
  goalPercent: number
  bottleFillPercent: number
  goalReached: boolean
}

type SippoEventCommand = {
  key: string
  label: string
  path: string
}

const sippoGifPaths: Record<SippoGifKey, string> = {
  idle: '/sippo/idle.gif',
  fallingAsleep: '/sippo/fall-asleep.gif',
  sleeping: '/sippo/sleeping.gif',
  wakingUp: '/sippo/wake-up.gif',
}

const FALL_ASLEEP_DURATION_MS = 2500
const WAKE_UP_DURATION_MS = 2500
const HAPPY_REACTION_DURATION_MS = 5200
const REFILL_REACTION_DURATION_MS = 4200
const GOAL_REACTION_DURATION_MS = 8200

const STATE_POLL_INTERVAL_MS = 2000

const sippoEventCommands: SippoEventCommand[] = [
  {
    key: 'sip',
    label: 'Sip detected',
    path: '/api/event/sip',
  },
  {
    key: 'refill',
    label: 'Refill detected',
    path: '/api/event/refill',
  },
  {
    key: 'empty',
    label: 'Bottle low / empty',
    path: '/api/event/empty',
  },
  {
    key: 'reminder1',
    label: 'Reminder level 1',
    path: '/api/event/reminder1',
  },
  {
    key: 'reminder2',
    label: 'Reminder level 2',
    path: '/api/event/reminder2',
  },
  {
    key: 'reminder3',
    label: 'Reminder level 3',
    path: '/api/event/reminder3',
  },
  {
    key: 'sleep',
    label: 'Sleep mode',
    path: '/api/event/sleep',
  },
  {
    key: 'wake',
    label: 'Wake Sippo',
    path: '/api/event/wake',
  },
  {
    key: 'reset',
    label: 'Reset day',
    path: '/api/event/reset',
  },
]

function getFallbackGifForMood(mood: SippoMood): SippoGifKey {
  if (mood === 'sleeping') {
    return 'sleeping'
  }

  // Until you have happy/sad/refill/goal GIFs, all awake moods use idle.
  return 'idle'
}

function App() {
  const [arduinoBaseUrl, setArduinoBaseUrl] = useState(() => {
    return (
      localStorage.getItem('sippoArduinoBaseUrl') ||
      import.meta.env.VITE_ARDUINO_BASE_URL ||
      'http://192.168.178.114'
  )
  })

  const [statusMessage, setStatusMessage] = useState('Ready')
  const [errorMessage, setErrorMessage] = useState<string | null>(null)
  const [isSending, setIsSending] = useState(false)
  const [sippoState, setSippoState] = useState<SippoStateResponse | null>(null)

  const [currentGif, setCurrentGif] = useState<SippoGifKey>('idle')

  // Changing this forces the <img> to remount.
  // That helps restart transition GIFs like falling asleep / waking up.
  const [gifVersion, setGifVersion] = useState(0)
  const isGifTransitioningRef = useRef(false)

  function showGif(gif: SippoGifKey) {
    setCurrentGif(gif)
    setGifVersion((current) => current + 1)
  }

  function applyFrontendReaction(
    command: SippoEventCommand,
    data: SippoStateResponse,
  ) {
    if (command.key === 'sleep') {
      isGifTransitioningRef.current = true
      showGif('fallingAsleep')

      window.setTimeout(() => {
        showGif('sleeping')
        isGifTransitioningRef.current = false
      }, FALL_ASLEEP_DURATION_MS)

      return
    }

    if (command.key === 'wake') {
      isGifTransitioningRef.current = true
      showGif('wakingUp')

      window.setTimeout(() => {
        showGif(getFallbackGifForMood(data.mood))
        isGifTransitioningRef.current = false
      }, WAKE_UP_DURATION_MS)

      return
    }

    showGif(getFallbackGifForMood(data.mood))
  }

  async function sendSippoEvent(command: SippoEventCommand) {
    setIsSending(true)
    setErrorMessage(null)
    setStatusMessage(`Sending "${command.label}" event...`)

    try {
      const response = await fetch(`${arduinoBaseUrl}${command.path}`)

      if (!response.ok) {
        throw new Error(`Arduino responded with HTTP ${response.status}`)
      }

      const data = (await response.json()) as SippoStateResponse

      if (data.status !== 'ok') {
        throw new Error(data.message || 'Arduino returned an error')
      }

      setSippoState(data)
      applyFrontendReaction(command, data)
      if (command.key === 'sip') {
        scheduleStateRefresh(data.goalReached ? GOAL_REACTION_DURATION_MS : HAPPY_REACTION_DURATION_MS)
      }

      if (command.key === 'refill' || command.key === 'empty') {
        scheduleStateRefresh(REFILL_REACTION_DURATION_MS)
      }

      setStatusMessage(data.message || `Sippo event "${command.label}" applied`)
    } catch (error) {
      const message =
        error instanceof Error ? error.message : 'Unknown connection error'

      setErrorMessage(message)
      setStatusMessage('Could not reach Arduino')
    } finally {
      setIsSending(false)
    }
  }

  function scheduleStateRefresh(delayMs: number) {
    window.setTimeout(() => {
      refreshSippoState()
    }, delayMs)
  }

  async function refreshSippoState() {
    setIsSending(true)
    setErrorMessage(null)
    setStatusMessage('Refreshing Sippo state...')

    try {
      const response = await fetch(`${arduinoBaseUrl}/api/state`)

      if (!response.ok) {
        throw new Error(`Arduino responded with HTTP ${response.status}`)
      }

      const data = (await response.json()) as SippoStateResponse

      if (data.status !== 'ok') {
        throw new Error(data.message || 'Arduino returned an error')
      }

      setSippoState(data)
      showGif(getFallbackGifForMood(data.mood))

      setStatusMessage(data.message || 'Sippo state refreshed')
    } catch (error) {
      const message =
        error instanceof Error ? error.message : 'Unknown connection error'

      setErrorMessage(message)
      setStatusMessage('Could not reach Arduino')
    } finally {
      setIsSending(false)
    }
  }

  async function refreshSippoStateSilently() {
    try {
      const response = await fetch(`${arduinoBaseUrl}/api/state`)

      if (!response.ok) {
        return
      }

      const data = (await response.json()) as SippoStateResponse

      if (data.status !== 'ok') {
        return
      }

      setSippoState(data)

      if (!isGifTransitioningRef.current) {
        const nextGif = getFallbackGifForMood(data.mood)

        setCurrentGif((current) => {
          if (current === nextGif) {
            return current
          }

          setGifVersion((version) => version + 1)
          return nextGif
        })
      }
    } catch {
      // Silent polling should not spam the visible error box.
      // Manual button presses still show errors through sendSippoEvent/refreshSippoState.
    }
  }

  const displayColor = sippoState?.colorHex || '#00C9CC'
  const currentGifSrc = `${sippoGifPaths[currentGif]}?v=${gifVersion}`

  useEffect(() => {
    const intervalId = window.setInterval(() => {
      refreshSippoStateSilently()
    }, STATE_POLL_INTERVAL_MS)

    return () => {
      window.clearInterval(intervalId)
    }
  }, [arduinoBaseUrl])

  return (
    <main className="app-shell">
      <section className="card">
        <p className="eyebrow">Sippo Embodied Agent</p>

        <h1>Sippo Wizard-of-Oz Control</h1>

        <label className="field">
          <span>Arduino backend URL</span>
          <div className="ip-field-wrapper">
          <input
            value={arduinoBaseUrl}
              onChange={(event) => {
                const value = event.target.value
                setArduinoBaseUrl(value)
                localStorage.setItem('sippoArduinoBaseUrl', value)
              }}
              placeholder="http://192.168.178.114"
          />


            <button
              type="button"
              className="color-button"
              onClick={() => {
                localStorage.removeItem('sippoArduinoBaseUrl')
                setArduinoBaseUrl('')
                setStatusMessage('Cached Arduino URL cleared')
              }}
            >
              Clear saved IP
            </button>
          </div>
        </label>

        <section
          className="sippo-display"
          style={{
            backgroundColor: displayColor,
          }}
        >
          <div className="sippo-screen">
            <img
              key={`${currentGif}-${gifVersion}`}
              src={currentGifSrc}
              alt={`Sippo ${currentGif}`}
              className="sippo-fish"
            />
          </div>

          <div className="sippo-state-grid">
            <div>
              <span>Mood</span>
              <strong>{sippoState?.mood || 'content'}</strong>
            </div>

            <div>
              <span>Mode</span>
              <strong>{sippoState?.mode || 'awake'}</strong>
            </div>

            <div>
              <span>Reminder</span>
              <strong>{sippoState?.reminderLevel ?? 0}</strong>
            </div>

            <div>
              <span>Goal</span>
              <strong>{sippoState?.goalPercent ?? 0}%</strong>
            </div>

            <div>
              <span>Bottle fill</span>
              <strong>{sippoState?.bottleFillPercent ?? 100}%</strong>
            </div>

            <div>
              <span>Drank today</span>
              <strong>
                {sippoState?.totalDrankMl ?? 0}/
                {sippoState?.dailyGoalMl ?? 2000} ml
              </strong>
            </div>
          </div>
        </section>

        <div className="button-grid">
          {sippoEventCommands.map((command) => (
            <button
              key={command.key}
              type="button"
              className="color-button"
              onClick={() => sendSippoEvent(command)}
              disabled={isSending}
            >
              {command.label}
            </button>
          ))}

          <button
            type="button"
            className="color-button"
            onClick={refreshSippoState}
            disabled={isSending}
          >
            Refresh state
          </button>
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