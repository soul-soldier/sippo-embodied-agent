import { useEffect, useRef, useState } from 'react'
import type { CSSProperties } from 'react'
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

type SippoGifKey =
  | 'idle'
  | 'fallingAsleep'
  | 'sleeping'
  | 'wakingUp'
  | 'goalReached'
  | 'sad1'
  | 'sad2'
  | 'sad3'
  | 'happy'
  | 'bottleEmpty'

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

  // Added by the scale-enabled Arduino endpoint.
  // Marked optional so the frontend still works with older sketches.
  scaleReady?: boolean
  scaleTared?: boolean
  weightGrams?: number
  smoothedWeightGrams?: number
  scaleCalibrationFactor?: number
  scaleLastReadAgeMs?: number

  // Added by the scale-auto-detection Arduino endpoint.
  scaleAutoEventsEnabled?: boolean
  bottlePresent?: boolean
  estimatedWaterMl?: number
  estimatedBottleFillPercent?: number
  emptyBottleWeightGrams?: number
  bottleCapacityMl?: number
  lastKnownBottleWeightGrams?: number
  scaleDeltaSinceLastBottleWeightGrams?: number
  scaleEventCounter?: number
  lastScaleEvent?: string
  emptyWarningEligible?: boolean
}

type SippoEventCommand = {
  key: string
  label: string
  path: string

  // Some buttons are presentation-only. They show the animation in the
  // frontend but do not change the Arduino state, because the scale is the
  // source of truth for bottle fill/refill/empty.
  frontendOnlyAction?: 'refillDemo' | 'emptyDemo'
}

const sippoGifPaths: Record<SippoGifKey, string> = {
  idle: '/sippo/idle.gif',
  fallingAsleep: '/sippo/fall-asleep.gif',
  sleeping: '/sippo/sleeping.gif',
  wakingUp: '/sippo/wake-up.gif',
  goalReached: '/sippo/goal-reached.gif',
  sad1: '/sippo/sad1.gif',
  sad2: '/sippo/sad2.gif',
  sad3: '/sippo/sad3.gif',
  happy: '/sippo/happy.gif',
  bottleEmpty: '/sippo/bottle-empty.gif',
}

const FALL_ASLEEP_DURATION_MS = 2500
const WAKE_UP_DURATION_MS = 2500
const HAPPY_REACTION_DURATION_MS = 5200
const REFILL_REACTION_DURATION_MS = 4200
const GOAL_REACTION_DURATION_MS = 8200

const STATE_POLL_INTERVAL_MS = 1000

const sippoEventCommands: SippoEventCommand[] = [
  {
    key: 'sip',
    label: 'Sip detected',
    path: '/api/event/sip',
  },
  {
    key: 'refill',
    label: 'Refill demo',
    path: '/api/event/refill',
    frontendOnlyAction: 'refillDemo',
  },
  {
    key: 'empty',
    label: 'Bottle low / empty demo',
    path: '/api/event/empty',
    frontendOnlyAction: 'emptyDemo',
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
  {
    key: 'scaleTare',
    label: 'Tare scale',
    path: '/api/scale/tare',
  },
  {
    key: 'scaleBaseline',
    label: 'Set bottle baseline',
    path: '/api/scale/baseline',
  },
  {
    key: 'scaleAutoOn',
    label: 'Scale auto on',
    path: '/api/scale/auto/on',
  },
  {
    key: 'scaleAutoOff',
    label: 'Scale auto off',
    path: '/api/scale/auto/off',
  },
]

function formatWeight(value: number | undefined) {
  if (typeof value !== 'number' || Number.isNaN(value)) {
    return '—'
  }

  return `${value.toFixed(1)} g`
}

function formatMl(value: number | undefined) {
  if (typeof value !== 'number' || Number.isNaN(value)) {
    return '—'
  }

  return `${Math.round(value)} ml`
}

function formatDelta(value: number | undefined) {
  if (typeof value !== 'number' || Number.isNaN(value)) {
    return '—'
  }

  const sign = value > 0 ? '+' : ''
  return `${sign}${value.toFixed(1)} g`
}

function formatAgeMs(value: number | undefined) {
  if (typeof value !== 'number' || Number.isNaN(value)) {
    return '—'
  }

  if (value < 1000) {
    return `${Math.round(value)} ms`
  }

  return `${(value / 1000).toFixed(1)} s`
}

function shouldSuppressEmptyWarning(data: SippoStateResponse) {
  if (data.mood !== 'empty') {
    return false
  }

  // With scale auto detection active, the Arduino may briefly know that the
  // fill percentage is low while it is still waiting to decide whether the
  // latest bottle return was a sip or refill. During that window we should not
  // show the empty GIF/water warning yet.
  if (!data.scaleAutoEventsEnabled) {
    return false
  }

  if (data.bottlePresent === false) {
    return true
  }

  if (data.lastScaleEvent === 'bottle_returned_waiting') {
    return true
  }

  if (data.emptyWarningEligible === false) {
    return true
  }

  return false
}

function getActiveGifForState(data: SippoStateResponse): SippoGifKey {
  if (data.mode === 'sleeping' || data.mood === 'sleeping') {
    return 'sleeping'
  }

  if (data.mood === 'empty') {
    return shouldSuppressEmptyWarning(data) ? 'idle' : 'bottleEmpty'
  }

  if (data.mood === 'happy') {
    return 'happy'
  }

  if (data.reminderLevel === 3) {
    return 'sad3'
  } else if (data.reminderLevel === 2) {
    return 'sad2'
  } else if (data.reminderLevel === 1) {
    return 'sad1'
  }

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
  const sippoStateRef = useRef<SippoStateResponse | null>(null)

  const [currentGif, setCurrentGif] = useState<SippoGifKey>('idle')

  // Changing this forces the <img> to remount.
  // That helps restart transition GIFs like falling asleep / waking up.
  const [gifVersion, setGifVersion] = useState(0)
  const isGifTransitioningRef = useRef(false)
  const hasShownGoalAnimationRef = useRef(false)
  const lastHandledScaleEventCounterRef = useRef<number | null>(null)
  const temporaryGifTimeoutRef = useRef<number | null>(null)

  function showGif(gif: SippoGifKey) {
    setCurrentGif(gif)
    setGifVersion((current) => current + 1)
  }

  function showTemporaryGif(
    gif: SippoGifKey,
    durationMs: number,
    finalState: SippoStateResponse,
  ) {
    if (temporaryGifTimeoutRef.current !== null) {
      window.clearTimeout(temporaryGifTimeoutRef.current)
      temporaryGifTimeoutRef.current = null
    }

    isGifTransitioningRef.current = true
    showGif(gif)

    temporaryGifTimeoutRef.current = window.setTimeout(() => {
      showGif(getActiveGifForState(sippoStateRef.current ?? finalState))
      isGifTransitioningRef.current = false
      temporaryGifTimeoutRef.current = null
    }, durationMs)
  }

  function updateSippoState(data: SippoStateResponse) {
    sippoStateRef.current = data
    setSippoState(data)

    if (!data.goalReached) {
      hasShownGoalAnimationRef.current = false
    }
  }

  function updateGifFromState(data: SippoStateResponse) {
    if (isGifTransitioningRef.current) {
      return
    }

    const nextGif = getActiveGifForState(data)

    setCurrentGif((current) => {
      if (current === nextGif) {
        return current
      }

      setGifVersion((version) => version + 1)
      return nextGif
    })
  }

  function applyStateFromArduino(
    data: SippoStateResponse,
    options: { allowScaleReaction: boolean } = { allowScaleReaction: false },
  ) {
    const previous = sippoStateRef.current

    updateSippoState(data)

    if (options.allowScaleReaction && previous) {
      const scaleEvent = data.lastScaleEvent || ''
      const scaleEventCounter = data.scaleEventCounter ?? 0
      const previousScaleEventCounter = previous.scaleEventCounter ?? 0

      const hasNewScaleEvent =
        scaleEventCounter !== previousScaleEventCounter &&
        scaleEventCounter !== lastHandledScaleEventCounterRef.current

      if (hasNewScaleEvent) {
        lastHandledScaleEventCounterRef.current = scaleEventCounter

        const drankDelta = data.totalDrankMl - previous.totalDrankMl

        const scaleDetectedSip =
          drankDelta > 0 &&
          scaleEvent.startsWith('scale') &&
          scaleEvent.includes('sip')

        const scaleDetectedRefill =
          scaleEvent.startsWith('scale') &&
          scaleEvent.includes('refill')

        const scaleDetectedEmpty =
          scaleEvent === 'scale_empty' &&
          data.mood === 'empty'

        if (scaleDetectedSip) {
          if (data.goalReached && !hasShownGoalAnimationRef.current) {
            hasShownGoalAnimationRef.current = true
            showTemporaryGif('goalReached', GOAL_REACTION_DURATION_MS, data)
          } else {
            showTemporaryGif('happy', HAPPY_REACTION_DURATION_MS, data)
          }

          setStatusMessage(`Scale detected sip: +${drankDelta} ml`)
          return
        }

        if (scaleDetectedRefill) {
          showTemporaryGif('happy', REFILL_REACTION_DURATION_MS, data)
          setStatusMessage('Scale detected refill')
          return
        }

        if (scaleDetectedEmpty) {
          // Do not interrupt a reward reaction that is already playing, e.g.
          // sip detected -> happy fish. When that timeout ends, the current
          // backend state is already `empty`, so the UI will naturally switch
          // to the bottle-empty warning afterwards.
          if (isGifTransitioningRef.current) {
            setStatusMessage('Bottle became low / empty after the sip reward')
            return
          }

          showGif('bottleEmpty')
          setStatusMessage('Scale detected low / empty bottle')
          return
        }
      }
    }

    updateGifFromState(data)
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
        showGif(getActiveGifForState(sippoStateRef.current ?? data))
        isGifTransitioningRef.current = false
      }, WAKE_UP_DURATION_MS)

      return
    }

    if (data.goalReached && command.key === 'sip' && !hasShownGoalAnimationRef.current) {
      hasShownGoalAnimationRef.current = true
      isGifTransitioningRef.current = true
      showGif('goalReached')

      window.setTimeout(() => {
        showGif(getActiveGifForState(sippoStateRef.current ?? data))
        isGifTransitioningRef.current = false
      }, GOAL_REACTION_DURATION_MS)

      return
    }

    if (command.key === 'sip' || command.key === 'refill') {
      isGifTransitioningRef.current = true
      showGif('happy')

      window.setTimeout(() => {
        showGif(getActiveGifForState(sippoStateRef.current ?? data))
        isGifTransitioningRef.current = false
      }, HAPPY_REACTION_DURATION_MS)

      return
    }

    showGif(getActiveGifForState(data))
  }

  function runFrontendOnlyDemo(command: SippoEventCommand) {
    setErrorMessage(null)

    const currentState = sippoStateRef.current

    if (command.frontendOnlyAction === 'refillDemo') {
      setStatusMessage('Showing refill demo only. Arduino scale state was not changed.')

      if (currentState) {
        showTemporaryGif('happy', REFILL_REACTION_DURATION_MS, currentState)
      } else {
        showGif('happy')
      }

      return
    }

    if (command.frontendOnlyAction === 'emptyDemo') {
      setStatusMessage('Showing bottle low / empty demo only. Arduino scale state was not changed.')

      if (currentState) {
        showTemporaryGif('bottleEmpty', REFILL_REACTION_DURATION_MS, currentState)
      } else {
        showGif('bottleEmpty')
      }
    }
  }

  async function sendSippoEvent(command: SippoEventCommand) {
    if (command.frontendOnlyAction) {
      runFrontendOnlyDemo(command)
      return
    }

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

      applyStateFromArduino(data)
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

      applyStateFromArduino(data)

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

      applyStateFromArduino(data, { allowScaleReaction: true })
    } catch {
      // Silent polling should not spam the visible error box.
      // Manual button presses still show errors through sendSippoEvent/refreshSippoState.
    }
  }

  const isEmptyBottleWarning =
    sippoState?.mood === 'empty' && !shouldSuppressEmptyWarning(sippoState)
  const displayColor = isEmptyBottleWarning
    ? '#FF3700'
    : sippoState?.colorHex || '#00C9CC'

  const sippoDisplayStyle = {
    '--sippo-display-color': displayColor,
    backgroundColor: displayColor,
  } as CSSProperties

  const currentGifSrc = `${sippoGifPaths[currentGif]}?v=${gifVersion}`

  const scaleReadyLabel = sippoState?.scaleReady ? 'ready' : 'not ready'
  const scaleTaredLabel = sippoState?.scaleTared ? 'yes' : 'no'
  const scaleAutoLabel = sippoState?.scaleAutoEventsEnabled ? 'on' : 'off'
  const bottlePresentLabel = sippoState?.bottlePresent ? 'yes' : 'no'

  useEffect(() => {
    refreshSippoStateSilently()

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

        <h1>Sippo Control Panel</h1>

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

        <div className="sippo-dashboard">
          <div className="sippo-main-row">
            <section
              className={`sippo-display ${isEmptyBottleWarning ? 'sippo-display--empty-warning' : ''
                }`}
              style={sippoDisplayStyle}
            >
              <div className="sippo-screen">
                <img
                  key={`${currentGif}-${gifVersion}`}
                  src={currentGifSrc}
                  alt={`Sippo ${currentGif}`}
                  className="sippo-fish"
                />
              </div>

              <div className="sippo-state-grid sippo-state-grid--tank">
                <div>
                  <span>Drank today</span>
                  <strong>
                    {sippoState?.totalDrankMl ?? 0}/
                    {sippoState?.dailyGoalMl ?? 2000} ml
                  </strong>
                </div>

                <div>
                  <span>Bottle fill</span>
                  <strong>{sippoState?.bottleFillPercent ?? 100}%</strong>
                </div>

                <div>
                  <span>Goal</span>
                  <strong>{sippoState?.goalPercent ?? 0}%</strong>
                </div>
              </div>
            </section>

            <aside className="sippo-sidebar">
              <div className="sippo-state-grid sippo-state-grid--sidebar">
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
                  <span>Scale</span>
                  <strong>{scaleReadyLabel}</strong>
                </div>

                <div>
                  <span>Scale tared</span>
                  <strong>{scaleTaredLabel}</strong>
                </div>

                <div>
                  <span>Scale auto</span>
                  <strong>{scaleAutoLabel}</strong>
                </div>

                <div>
                  <span>Bottle present</span>
                  <strong>{bottlePresentLabel}</strong>
                </div>

                <div>
                  <span>Weight raw</span>
                  <strong>{formatWeight(sippoState?.weightGrams)}</strong>
                </div>

                <div>
                  <span>Weight smooth</span>
                  <strong>{formatWeight(sippoState?.smoothedWeightGrams)}</strong>
                </div>

                <div>
                  <span>Water by scale</span>
                  <strong>{formatMl(sippoState?.estimatedWaterMl)}</strong>
                </div>

                <div>
                  <span>Scale fill</span>
                  <strong>{sippoState?.estimatedBottleFillPercent ?? '—'}%</strong>
                </div>

                <div>
                  <span>Weight delta</span>
                  <strong>{formatDelta(sippoState?.scaleDeltaSinceLastBottleWeightGrams)}</strong>
                </div>

                <div>
                  <span>Last scale event</span>
                  <strong>{sippoState?.lastScaleEvent || 'none'}</strong>
                </div>

                <div>
                  <span>Scale age</span>
                  <strong>{formatAgeMs(sippoState?.scaleLastReadAgeMs)}</strong>
                </div>
              </div>
            </aside>
          </div>

          <div className="sippo-actions">
            <div className="button-grid sippo-action-grid">
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
          </div>
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