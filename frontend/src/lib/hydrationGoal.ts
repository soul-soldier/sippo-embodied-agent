export type GoalGender = 'female' | 'male' | 'other'

export type ActivityType = 'none' | 'light' | 'hard'

export type PersonalizedGoalInput = {
  gender: GoalGender
  heightCm: number
  weightKg: number
  activityType: ActivityType
  activityMinutes: number
  maxTempC: number | null
}

export type PersonalizedGoalCalculation = {
  baseGoalMl: number
  activityBonusMl: number
  weatherBonusMl: number
  adjustedGoalMl: number
}

export const DEFAULT_GOAL_GENDER: GoalGender = 'female'
export const DEFAULT_GOAL_HEIGHT_CM = '170'
export const DEFAULT_GOAL_WEIGHT_KG = '70'
export const DEFAULT_GOAL_ACTIVITY_TYPE: ActivityType = 'none'
export const DEFAULT_GOAL_ACTIVITY_MINUTES = '0'

const GOAL_BMI_ADJUSTMENTS = [
  { maxBmi: 18.5, multiplier: 1.1 },
  { maxBmi: 25, multiplier: 1.0 },
  { maxBmi: 30, multiplier: 0.95 },
  { maxBmi: Number.POSITIVE_INFINITY, multiplier: 0.9 },
] as const

const ACTIVITY_BONUS_PER_MINUTE_ML: Record<ActivityType, number> = {
  none: 0,
  light: 6,
  hard: 10,
}

const WEATHER_BONUS_BY_MAX_TEMP_C = [
  { minTempC: 30, bonusMl: 1000 },
  { minTempC: 25, bonusMl: 500 },
] as const

export function parseNumberInput(value: string) {
  const parsed = Number(value.replace(',', '.'))
  return Number.isFinite(parsed) ? parsed : Number.NaN
}

export function calculateBaselineGoalMl(
  gender: GoalGender,
  heightCm: number,
  weightKg: number,
) {
  if (!Number.isFinite(heightCm) || !Number.isFinite(weightKg)) {
    return Number.NaN
  }

  if (heightCm <= 0 || weightKg <= 0) {
    return Number.NaN
  }

  const heightM = heightCm / 100
  const bmi = weightKg / (heightM * heightM)

  const genderFactorMlPerKg =
    gender === 'male' ? 35 : gender === 'female' ? 31 : 33

  const bmiMultiplier =
    GOAL_BMI_ADJUSTMENTS.find((entry) => bmi < entry.maxBmi)?.multiplier ?? 1

  return Math.round(weightKg * genderFactorMlPerKg * bmiMultiplier)
}

export function calculateActivityBonusMl(
  activityType: ActivityType,
  activityMinutes: number,
) {
  if (!Number.isFinite(activityMinutes) || activityMinutes <= 0) {
    return 0
  }

  const perMinuteMl = ACTIVITY_BONUS_PER_MINUTE_ML[activityType] ?? 0

  return Math.round(activityMinutes * perMinuteMl)
}

export function calculateWeatherBonusMl(maxTempC: number) {
  for (const band of WEATHER_BONUS_BY_MAX_TEMP_C) {
    if (maxTempC >= band.minTempC) {
      return band.bonusMl
    }
  }

  return 0
}

export function calculatePersonalizedGoalMl({
  gender,
  heightCm,
  weightKg,
  activityType,
  activityMinutes,
  maxTempC,
}: PersonalizedGoalInput): PersonalizedGoalCalculation {
  const baseGoalMl = calculateBaselineGoalMl(gender, heightCm, weightKg)
  const activityBonusMl = calculateActivityBonusMl(activityType, activityMinutes)
  const weatherBonusMl =
    typeof maxTempC === 'number' && Number.isFinite(maxTempC)
      ? calculateWeatherBonusMl(maxTempC)
      : 0

  return {
    baseGoalMl,
    activityBonusMl,
    weatherBonusMl,
    adjustedGoalMl: baseGoalMl + activityBonusMl + weatherBonusMl,
  }
}
