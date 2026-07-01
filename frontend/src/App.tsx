import { useEffect, useRef, useState } from 'react'
import type { CSSProperties, ReactNode } from 'react'
import './App.css'
import {
  fetchTodayMaxTemperatureForLocationQuery,
  formatOpenMeteoLocation,
} from './api/openMeteo'

import {
  calculatePersonalizedGoalMl,
  DEFAULT_GOAL_ACTIVITY_MINUTES,
  DEFAULT_GOAL_ACTIVITY_TYPE,
  DEFAULT_GOAL_GENDER,
  DEFAULT_GOAL_HEIGHT_CM,
  DEFAULT_GOAL_WEIGHT_KG,
  parseNumberInput,
  type ActivityType,
  type GoalGender,
} from './lib/hydrationGoal'

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
  | 'refillDetected'

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

  scaleReady?: boolean
  scaleTared?: boolean
  weightGrams?: number
  smoothedWeightGrams?: number
  scaleCalibrationFactor?: number
  scaleLastReadAgeMs?: number

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
  pendingBottleReturnEvaluation?: boolean
}

type SippoEventCommand = {
  key: string
  label: string
  path: string
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
  refillDetected: '/sippo/refill-detected.gif',
}

const FALL_ASLEEP_DURATION_MS = 2500
const WAKE_UP_DURATION_MS = 2500
const HAPPY_REACTION_DURATION_MS = 5200
const REFILL_REACTION_DURATION_MS = 5200
const GOAL_REACTION_DURATION_MS = 8200

const STATE_POLL_INTERVAL_MS = 1500

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

type StateCardVariant = 'sippo' | 'bottle' | 'debug'

type StateCardProps = {
  variant: StateCardVariant
  label: string
  value: ReactNode
}

function StateCard({ variant, label, value }: StateCardProps) {

  return (
    <div className={`state-card state-card--${variant}`}>
      <span className="state-card__label">{label}</span>
      <strong className="state-card__value">{value}</strong>
    </div>
  )
}

function shouldSuppressEmptyWarning(data: SippoStateResponse) {
  if (data.mood !== 'empty') {
    return false
  }

  if (!data.scaleAutoEventsEnabled) {
    return false
  }

  if (data.bottlePresent === false) {
    return true
  }

  if (data.lastScaleEvent === 'bottle_returned_waiting') {
    return true
  }

  if (data.pendingBottleReturnEvaluation) {
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


async function fetchWithTimeout(url: string, timeoutMs: number) {
  const controller = new AbortController()

  const timeoutId = window.setTimeout(() => {
    controller.abort()
  }, timeoutMs)

  try {
    return await fetch(url, {
      signal: controller.signal,
      cache: 'no-store',
    })
  } finally {
    window.clearTimeout(timeoutId)
  }
}

function App() {
  const [arduinoBaseUrl, setArduinoBaseUrl] = useState(() => {
    return (
      localStorage.getItem('sippoArduinoBaseUrl') ||
      import.meta.env.VITE_ARDUINO_BASE_URL ||
      'http://192.168.4.1'
    )
  })

  const [statusMessage, setStatusMessage] = useState('Ready')
  const [errorMessage, setErrorMessage] = useState<string | null>(null)
  const [isSending, setIsSending] = useState(false)
  const [sippoState, setSippoState] = useState<SippoStateResponse | null>(null)
  const sippoStateRef = useRef<SippoStateResponse | null>(null)

  const [maxTempInput, setMaxTempInput] = useState(() => {
    return localStorage.getItem('sippoMaxTempC') || ''
  })

  const [locationInput, setLocationInput] = useState(() => {
    return localStorage.getItem('sippoWeatherLocation') || ''
  })

  const [goalGender, setGoalGender] = useState<GoalGender>(() => {
    const savedValue = localStorage.getItem('sippoGoalGender') as GoalGender | null

    if (savedValue === 'female' || savedValue === 'male' || savedValue === 'other') {
      return savedValue
    }

    return DEFAULT_GOAL_GENDER
  })

  const [goalHeightCmInput, setGoalHeightCmInput] = useState(() => {
    return localStorage.getItem('sippoGoalHeightCm') || DEFAULT_GOAL_HEIGHT_CM
  })

  const [goalWeightKgInput, setGoalWeightKgInput] = useState(() => {
    return localStorage.getItem('sippoGoalWeightKg') || DEFAULT_GOAL_WEIGHT_KG
  })

  const [goalActivityType, setGoalActivityType] = useState<ActivityType>(() => {
    const savedValue = localStorage.getItem('sippoGoalActivityType') as ActivityType | null

    if (savedValue === 'none' || savedValue === 'light' || savedValue === 'hard') {
      return savedValue
    }

    return DEFAULT_GOAL_ACTIVITY_TYPE
  })

  const [goalActivityMinutesInput, setGoalActivityMinutesInput] = useState(() => {
    return (
      localStorage.getItem('sippoGoalActivityMinutes') ||
      DEFAULT_GOAL_ACTIVITY_MINUTES
    )
  })

  const [weatherLocationLabel, setWeatherLocationLabel] = useState<string | null>(
    null,
  )

  const [isFetchingWeather, setIsFetchingWeather] = useState(false)

  const [currentGif, setCurrentGif] = useState<SippoGifKey>('idle')
  const currentGifRef = useRef<SippoGifKey>('idle')
  const [temporaryDisplayEffect, setTemporaryDisplayEffect] =
    useState<'refillDetected' | null>(null)
  const [gifVersion, setGifVersion] = useState(0)
  const isGifTransitioningRef = useRef(false)
  const hasShownGoalAnimationRef = useRef(false)

  const lastHandledScaleEventCounterRef = useRef<number | null>(null)
  const temporaryGifTimeoutRef = useRef<number | null>(null)
  const isPollingRef = useRef(false)
  const isSendingRef = useRef(false)

  function showGif(gif: SippoGifKey) {
    currentGifRef.current = gif
    setCurrentGif(gif)
    setGifVersion((current) => current + 1)
  }

  function showTemporaryGif(
    gif: SippoGifKey,
    durationMs: number,
    finalState: SippoStateResponse,
    options: { displayEffect?: 'refillDetected' } = {},
  ) {
    if (temporaryGifTimeoutRef.current !== null) {
      window.clearTimeout(temporaryGifTimeoutRef.current)
      temporaryGifTimeoutRef.current = null
    }

    isGifTransitioningRef.current = true
    setTemporaryDisplayEffect(options.displayEffect ?? null)
    showGif(gif)

    temporaryGifTimeoutRef.current = window.setTimeout(() => {
      showGif(getActiveGifForState(sippoStateRef.current ?? finalState))
      setTemporaryDisplayEffect(null)
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

      currentGifRef.current = nextGif
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

    // If a tare command briefly showed an error, but the next Arduino state
    // confirms that the scale is actually tared, trust the state and clear
    // the stale error/status.
    if (data.scaleTared) {
      setErrorMessage((currentError) => {
        if (
          currentError?.includes('HX711 not ready') ||
          currentError?.toLowerCase().includes('tare')
        ) {
          return null
        }

        return currentError
      })

      setStatusMessage((currentStatus) => {
        if (
          currentStatus?.toLowerCase().includes('tare failed') ||
          currentStatus?.includes('HX711 not ready')
        ) {
          return 'Scale is tared'
        }

        return currentStatus
      })
    }

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

        const scaleDetectedSipCandidate = scaleEvent === 'scale_sip_candidate'

        const scaleDetectedSip =
          drankDelta > 0 &&
          scaleEvent === 'scale_sip'

        const scaleDetectedRefill =
          scaleEvent.startsWith('scale') &&
          scaleEvent.includes('refill')

        const scaleDetectedEmpty =
          scaleEvent === 'scale_empty' &&
          data.mood === 'empty'

        if (scaleDetectedSipCandidate) {
          // This is deliberately only an early acknowledgement, not a committed
          // state update. The Arduino still waits for the scale to settle before
          // increasing totalDrankMl. This keeps the interaction feeling causal
          // without letting the frontend become the source of truth.
          if (
            !isGifTransitioningRef.current ||
            currentGifRef.current !== 'happy'
          ) {
            showTemporaryGif('happy', HAPPY_REACTION_DURATION_MS, data)
          }

          setStatusMessage('Sippo noticed a possible sip… checking weight')
          return
        }

        if (scaleDetectedSip) {
          if (data.goalReached && !hasShownGoalAnimationRef.current) {
            hasShownGoalAnimationRef.current = true
            showTemporaryGif('goalReached', GOAL_REACTION_DURATION_MS, data)
          } else if (
            !isGifTransitioningRef.current ||
            currentGifRef.current !== 'happy'
          ) {
            showTemporaryGif('happy', HAPPY_REACTION_DURATION_MS, data)
          }

          setStatusMessage(`Scale confirmed sip: +${drankDelta} ml`)
          return
        }

        if (scaleDetectedRefill) {
          showTemporaryGif('refillDetected', REFILL_REACTION_DURATION_MS, data, {
            displayEffect: 'refillDetected',
          })
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

    if (command.key === 'sip') {
      showTemporaryGif('happy', HAPPY_REACTION_DURATION_MS, data)
      return
    }

    if (command.key === 'refill') {
      showTemporaryGif('refillDetected', REFILL_REACTION_DURATION_MS, data, {
        displayEffect: 'refillDetected',
      })
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
        showTemporaryGif('refillDetected', REFILL_REACTION_DURATION_MS, currentState, {
          displayEffect: 'refillDetected',
        })
      } else {
        setTemporaryDisplayEffect('refillDetected')
        showGif('refillDetected')

        window.setTimeout(() => {
          setTemporaryDisplayEffect(null)
          showGif('idle')
        }, REFILL_REACTION_DURATION_MS)
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

    if (!arduinoBaseUrl) {
      setErrorMessage('Please enter the Arduino backend URL first.')
      return
    }

    // Pause automatic polling while command is in flight
    isSendingRef.current = true
    setIsSending(true)
    setErrorMessage(null)
    setStatusMessage(`Sending "${command.label}" event...`)

    try {
      const commandTimeoutMs = command.path.startsWith('/api/scale/')
        ? 12000
        : 5000

      const response = await fetchWithTimeout(
        `${arduinoBaseUrl}${command.path}`,
        commandTimeoutMs,
      )

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

      if (command.path === '/api/scale/tare' && data.scaleTared) {
        setErrorMessage(null)
        setStatusMessage('Scale is tared')
      } else {
        setStatusMessage(data.message || `Sippo event "${command.label}" applied`)
      }
    } catch (error) {
      const message =
        error instanceof Error ? error.message : 'Unknown connection error'

      setErrorMessage(message)
      setStatusMessage('Could not reach Arduino')
    } finally {
      isSendingRef.current = false
      setIsSending(false)
    }
  }

  async function fetchWeatherFromOpenMeteo() {
    const locationQuery = locationInput.trim()

    if (!locationQuery) {
      setErrorMessage('Please enter a location first.')
      return
    }

    setIsFetchingWeather(true)
    setErrorMessage(null)
    setStatusMessage('Fetching weather from Open-Meteo...')

    try {
      const { location, maxTempC } =
        await fetchTodayMaxTemperatureForLocationQuery(locationQuery)

      const roundedMaxTemp = Math.round(maxTempC * 10) / 10
      const maxTempValue = String(roundedMaxTemp)
      const locationLabel = formatOpenMeteoLocation(location)

      setMaxTempInput(maxTempValue)
      setWeatherLocationLabel(locationLabel)

      localStorage.setItem('sippoMaxTempC', maxTempValue)
      localStorage.setItem('sippoWeatherLocation', locationQuery)

      setStatusMessage(
        `Weather loaded for ${locationLabel}: today's max temp is ${maxTempValue} °C`,
      )
    } catch (error) {
      const message =
        error instanceof Error ? error.message : 'Could not fetch weather.'

      setErrorMessage(
        `${message} You can still enter the max temperature manually.`,
      )
      setStatusMessage('Weather fetch failed')
    } finally {
      setIsFetchingWeather(false)
    }
  }

  async function updateGoalFromPersonalizedInputs() {
    if (!arduinoBaseUrl) {
      setErrorMessage('Please enter the Arduino backend URL first.')
      return
    }

    const heightCm = parseNumberInput(goalHeightCmInput)
    const weightKg = parseNumberInput(goalWeightKgInput)
    const activityMinutes = parseNumberInput(goalActivityMinutesInput)
    const parsedMaxTempC = parseNumberInput(maxTempInput)
    const hasWeatherInput = maxTempInput.trim().length > 0 && Number.isFinite(parsedMaxTempC)
    const maxTempC = hasWeatherInput ? parsedMaxTempC : null

    if (!Number.isFinite(heightCm) || heightCm < 120 || heightCm > 230) {
      setErrorMessage('Please enter a realistic height between 120 cm and 230 cm.')
      return
    }

    if (!Number.isFinite(weightKg) || weightKg < 30 || weightKg > 250) {
      setErrorMessage('Please enter a realistic bodyweight between 30 kg and 250 kg.')
      return
    }

    if (!Number.isFinite(activityMinutes) || activityMinutes < 0 || activityMinutes > 240) {
      setErrorMessage('Please enter activity time between 0 and 240 minutes.')
      return
    }

    if (maxTempInput.trim().length > 0 && (!Number.isFinite(parsedMaxTempC) || parsedMaxTempC < -30 || parsedMaxTempC > 60)) {
      setErrorMessage('Please enter a realistic max temperature between -30 °C and 60 °C.')
      return
    }

    const calculation = calculatePersonalizedGoalMl({
      gender: goalGender,
      heightCm,
      weightKg,
      activityType: goalActivityType,
      activityMinutes,
      maxTempC,
    })

    if (!Number.isFinite(calculation.baseGoalMl) || calculation.baseGoalMl <= 0) {
      setErrorMessage('Please enter valid body measurements to calculate the goal.')
      return
    }

    isSendingRef.current = true
    setIsSending(true)
    setErrorMessage(null)
    setStatusMessage('Updating drinking goal...')

    try {
      const newGoalMl = Math.round(calculation.adjustedGoalMl)

      const url = new URL(`${arduinoBaseUrl}/api/config/goal`)
      url.searchParams.set('ml', String(newGoalMl))

      const response = await fetchWithTimeout(
        url.toString(),
        5000,
      )

      if (!response.ok) {
        throw new Error(`Arduino responded with HTTP ${response.status}`)
      }

      const data = (await response.json()) as SippoStateResponse

      if (data.status !== 'ok') {
        throw new Error(data.message || 'Arduino returned an error')
      }

      applyStateFromArduino(data)
      setStatusMessage(
        `Goal updated: ${calculation.baseGoalMl} ml base + ${calculation.activityBonusMl} ml activity + ${calculation.weatherBonusMl} ml weather = ${data.dailyGoalMl} ml`,
      )
    } catch (error) {
      const message =
        error instanceof Error ? error.message : 'Unknown connection error'

      setErrorMessage(message)
      setStatusMessage('Could not update drinking goal')
    } finally {
      isSendingRef.current = false
      setIsSending(false)
    }
  }

  function scheduleStateRefresh(delayMs: number) {
    window.setTimeout(() => {
      refreshSippoStateSilently()
    }, delayMs)
  }

  async function refreshSippoStateSilently() {
    if (!arduinoBaseUrl) {
      return
    }

    // Do not overlap polls
    if (isPollingRef.current) {
      return
    }

    // Button commands have priority over background polling.
    if (isSendingRef.current) {
      return
    }

    isPollingRef.current = true

    try {
      const response = await fetchWithTimeout(`${arduinoBaseUrl}/api/state`, 900)

      if (!response.ok) {
        return
      }

      const data = (await response.json()) as SippoStateResponse

      if (data.status !== 'ok') {
        return
      }

      applyStateFromArduino(data, { allowScaleReaction: true })
    } catch {

    } finally {
      isPollingRef.current = false
    }
  }

  const isEmptyBottleWarning =
    sippoState?.mood === 'empty' && !shouldSuppressEmptyWarning(sippoState)
  const isRefillDetectedReaction =
    temporaryDisplayEffect === 'refillDetected' && !isEmptyBottleWarning

  const displayColor = isEmptyBottleWarning
    ? '#FF3700'
    : isRefillDetectedReaction
      ? '#063D24'
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

  const parsedHeightCm = parseNumberInput(goalHeightCmInput)
  const parsedWeightKg = parseNumberInput(goalWeightKgInput)
  const parsedActivityMinutes = parseNumberInput(goalActivityMinutesInput)
  const parsedWeatherTemp = parseNumberInput(maxTempInput)
  const weatherTempForPreview =
    maxTempInput.trim().length > 0 && Number.isFinite(parsedWeatherTemp)
      ? parsedWeatherTemp
      : null
  const goalPreview =
    Number.isFinite(parsedHeightCm) &&
      Number.isFinite(parsedWeightKg) &&
      Number.isFinite(parsedActivityMinutes)
      ? calculatePersonalizedGoalMl({
        gender: goalGender,
        heightCm: parsedHeightCm,
        weightKg: parsedWeightKg,
        activityType: goalActivityType,
        activityMinutes: parsedActivityMinutes,
        maxTempC: weatherTempForPreview,
      })
      : null
  const weatherBonusPreviewMl = goalPreview?.weatherBonusMl ?? 0
  const activityBonusPreviewMl = goalPreview?.activityBonusMl ?? 0
  const baseGoalPreviewMl = goalPreview?.baseGoalMl ?? null
  const adjustedGoalPreviewMl = goalPreview?.adjustedGoalMl ?? null

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
        <p className="app-eyebrow">Sippo Embodied Agent</p>

        <h1 className="app-title">Sippo Control Panel</h1>

        <div className="dashboard-layout">
          <section className="personalization-dashboard">

            <label className="field ip-field">
              <span>Arduino backend URL</span>
              <div className="ip-field-wrapper">
                <input
                  value={arduinoBaseUrl}
                  onChange={(event) => {
                    const value = event.target.value
                    setArduinoBaseUrl(value)
                    localStorage.setItem('sippoArduinoBaseUrl', value)
                  }}
                  placeholder="http://192.168.4.1"
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

            <section className="weather-goal-card">
              <div className="state-panel-header">
                <div>
                  <h2>Personalize drinking goal</h2>
                </div>
              </div>

              <p className="weather-goal-hint">
                Set your body profile and activity first. Then add today's weather or fetch it from Open-Meteo before applying the new goal.
              </p>

              <div className="goal-profile-row">
                <label className="field goal-profile-field">
                  <span>Gender</span>
                  <select
                    value={goalGender}
                    onChange={(event) => {
                      const value = event.target.value as GoalGender
                      setGoalGender(value)
                      localStorage.setItem('sippoGoalGender', value)
                    }}
                  >
                    <option value="female">Female</option>
                    <option value="male">Male</option>
                    <option value="other">Other / prefer not to say</option>
                  </select>
                </label>

                <label className="field goal-profile-field">
                  <span>Height (cm)</span>
                  <input
                    type="number"
                    min="120"
                    max="230"
                    step="0.5"
                    value={goalHeightCmInput}
                    onChange={(event) => {
                      const value = event.target.value
                      setGoalHeightCmInput(value)
                      localStorage.setItem('sippoGoalHeightCm', value)
                    }}
                    placeholder="170"
                  />
                </label>

                <label className="field goal-profile-field">
                  <span>Bodyweight (kg)</span>
                  <input
                    type="number"
                    min="30"
                    max="250"
                    step="0.1"
                    value={goalWeightKgInput}
                    onChange={(event) => {
                      const value = event.target.value
                      setGoalWeightKgInput(value)
                      localStorage.setItem('sippoGoalWeightKg', value)
                    }}
                    placeholder="70"
                  />
                </label>

                <label className="field goal-profile-field">
                  <span>Activity type</span>
                  <select
                    value={goalActivityType}
                    onChange={(event) => {
                      const value = event.target.value as ActivityType
                      setGoalActivityType(value)
                      localStorage.setItem('sippoGoalActivityType', value)
                    }}
                  >
                    <option value="none">None</option>
                    <option value="light">Light activity</option>
                    <option value="hard">Hard activity</option>
                  </select>
                </label>

                <label className="field goal-profile-field">
                  <span>Activity minutes</span>
                  <input
                    type="number"
                    min="0"
                    max="240"
                    step="15"
                    value={goalActivityMinutesInput}
                    onChange={(event) => {
                      const value = event.target.value
                      setGoalActivityMinutesInput(value)
                      localStorage.setItem('sippoGoalActivityMinutes', value)
                    }}
                    placeholder="30"
                  />
                </label>

                <StateCard
                  variant="sippo"
                  label="Baseline"
                  value={baseGoalPreviewMl === null ? '—' : `${baseGoalPreviewMl} ml`}
                />

                <StateCard
                  variant="bottle"
                  label="Activity bonus"
                  value={`+${activityBonusPreviewMl} ml`}
                />
              </div>

              <div className="weather-goal-row weather-goal-row--api">
                <label className="field weather-goal-location-field">
                  <span>Location</span>
                  <input
                    type="text"
                    value={locationInput}
                    onChange={(event) => {
                      const value = event.target.value
                      setLocationInput(value)
                      localStorage.setItem('sippoWeatherLocation', value)
                    }}
                    placeholder="Karlsruhe"
                  />
                </label>

                <label className="field weather-goal-temp-field">
                  <span>Max temp (°C)</span>
                  <input
                    type="number"
                    min="-30"
                    max="60"
                    step="0.5"
                    value={maxTempInput}
                    onChange={(event) => {
                      const value = event.target.value
                      setMaxTempInput(value)
                      localStorage.setItem('sippoMaxTempC', value)
                    }}
                    placeholder="28"
                  />
                </label>

                <StateCard
                  variant="sippo"
                  label="Weather bonus"
                  value={`+${weatherBonusPreviewMl} ml`}
                />

                <StateCard
                  variant="sippo"
                  label="New goal"
                  value={adjustedGoalPreviewMl === null ? '—' : `${adjustedGoalPreviewMl} ml`}
                />

                <div className="weather-goal-actions">
                  <button
                    type="button"
                    className="color-button weather-goal-button weather-goal-secondary-button"
                    onClick={fetchWeatherFromOpenMeteo}
                    disabled={isFetchingWeather}
                  >
                    {isFetchingWeather ? 'Fetching...' : 'Fetch'}
                  </button>

                  <button
                    type="button"
                    className="color-button weather-goal-button apply-button"
                    onClick={updateGoalFromPersonalizedInputs}
                    disabled={isSending || isFetchingWeather}
                  >
                    Apply
                  </button>
                </div>
              </div>

              {weatherLocationLabel && (
                <p className="weather-location-hint">
                  Weather location: {weatherLocationLabel}
                </p>
              )}
            </section>
          </section>

          <div className="sippo-dashboard">
            <div className="sippo-main-row">
              <section
                className={`sippo-display ${isEmptyBottleWarning ? 'sippo-display--empty-warning' : ''
                  } ${isRefillDetectedReaction ? 'sippo-display--refill-detected' : ''
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

              <aside className="sippo-sidebar sippo-sidebar--state-panel">
                <div className="state-panel-header">
                  <div>
                    <p className="state-panel-kicker">Live telemetry</p>
                    <h2>State overview</h2>
                  </div>

                  <div className="state-panel-legend" aria-label="State categories">
                    <span className="state-panel-legend__item state-panel-legend__item--sippo">
                      Sippo
                    </span>
                    <span className="state-panel-legend__item state-panel-legend__item--bottle">
                      Bottle
                    </span>
                    <span className="state-panel-legend__item state-panel-legend__item--debug">
                      Debug
                    </span>
                  </div>
                </div>

                <div className="sippo-state-grid sippo-state-grid--sidebar sippo-state-grid--compact">
                  <StateCard
                    variant="sippo"
                    label="Mood"
                    value={sippoState?.mood || 'content'}
                  />

                  <StateCard
                    variant="sippo"
                    label="Mode"
                    value={sippoState?.mode || 'awake'}
                  />

                  <StateCard
                    variant="sippo"
                    label="Reminder"
                    value={sippoState?.reminderLevel ?? 0}
                  />

                  <StateCard
                    variant="bottle"
                    label="Bottle present"
                    value={bottlePresentLabel}
                  />

                  <StateCard
                    variant="bottle"
                    label="Water"
                    value={formatMl(sippoState?.estimatedWaterMl)}
                  />

                  <StateCard
                    variant="bottle"
                    label="Scale fill"
                    value={`${sippoState?.estimatedBottleFillPercent ?? '—'}%`}
                  />

                  <StateCard
                    variant="bottle"
                    label="Weight"
                    value={formatWeight(sippoState?.smoothedWeightGrams)}
                  />

                  <StateCard
                    variant="debug"
                    label="Scale"
                    value={scaleReadyLabel}
                  />

                  <StateCard
                    variant="debug"
                    label="Tared"
                    value={scaleTaredLabel}
                  />

                  <StateCard
                    variant="debug"
                    label="Auto"
                    value={scaleAutoLabel}
                  />

                  <StateCard
                    variant="debug"
                    label="Raw"
                    value={formatWeight(sippoState?.weightGrams)}
                  />

                  <StateCard
                    variant="debug"
                    label="Delta"
                    value={formatDelta(sippoState?.scaleDeltaSinceLastBottleWeightGrams)}
                  />

                  <StateCard
                    variant="debug"
                    label="Last event"
                    value={sippoState?.lastScaleEvent || 'none'}
                  />

                  <StateCard
                    variant="debug"
                    label="Age"
                    value={formatAgeMs(sippoState?.scaleLastReadAgeMs)}
                  />
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
              </div>

              <div className="status-box">
                <strong>Status:</strong> {statusMessage}
              </div>

            </div>
          </div>
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