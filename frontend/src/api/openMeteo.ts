export type OpenMeteoLocation = {
  id: number
  name: string
  country?: string
  admin1?: string
  latitude: number
  longitude: number
  timezone?: string
}

type OpenMeteoGeocodingResponse = {
  results?: Array<{
    id: number
    name: string
    country?: string
    admin1?: string
    latitude: number
    longitude: number
    timezone?: string
  }>
}

type OpenMeteoForecastResponse = {
  daily?: {
    time?: string[]
    temperature_2m_max?: number[]
  }
}

async function fetchJson<T>(url: string, timeoutMs = 8000): Promise<T> {
  const controller = new AbortController()

  const timeoutId = window.setTimeout(() => {
    controller.abort()
  }, timeoutMs)

  try {
    const response = await fetch(url, {
      signal: controller.signal,
      cache: 'no-store',
    })

    if (!response.ok) {
      throw new Error(`Open-Meteo responded with HTTP ${response.status}`)
    }

    return (await response.json()) as T
  } finally {
    window.clearTimeout(timeoutId)
  }
}

export async function searchOpenMeteoLocations(
  query: string,
    countryCode = 'DE',
): Promise<OpenMeteoLocation[]> {
  const trimmedQuery = query.trim()

  if (!trimmedQuery) {
    throw new Error('Please enter a location first.')
  }

  const url = new URL('https://geocoding-api.open-meteo.com/v1/search')
  url.searchParams.set('name', trimmedQuery)
  url.searchParams.set('count', '5')
    url.searchParams.set('language', 'de')
  url.searchParams.set('format', 'json')

    // Important: prefer German locations for our Sippo prototype
    url.searchParams.set('countryCode', countryCode)

  const data = await fetchJson<OpenMeteoGeocodingResponse>(url.toString())

  if (!data.results?.length) {
      throw new Error(`No German location found for "${trimmedQuery}".`)
  }

  return data.results.map((result) => ({
    id: result.id,
    name: result.name,
    country: result.country,
    admin1: result.admin1,
    latitude: result.latitude,
    longitude: result.longitude,
    timezone: result.timezone,
  }))
}

export async function fetchTodayMaxTemperatureC(
  location: OpenMeteoLocation,
): Promise<number> {
  const url = new URL('https://api.open-meteo.com/v1/forecast')
  url.searchParams.set('latitude', String(location.latitude))
  url.searchParams.set('longitude', String(location.longitude))
  url.searchParams.set('daily', 'temperature_2m_max')
  url.searchParams.set('forecast_days', '1')
  url.searchParams.set('timezone', 'auto')

  const data = await fetchJson<OpenMeteoForecastResponse>(url.toString())

  const maxTempC = data.daily?.temperature_2m_max?.[0]

  if (typeof maxTempC !== 'number' || !Number.isFinite(maxTempC)) {
    throw new Error('Open-Meteo did not return a valid max temperature.')
  }

  return maxTempC
}

export async function fetchTodayMaxTemperatureForLocationQuery(
    query: string,
    countryCode = 'DE',
) {
    const locations = await searchOpenMeteoLocations(query, countryCode)

  const location = locations[0]
  const maxTempC = await fetchTodayMaxTemperatureC(location)

  return {
    location,
    maxTempC,
  }
}
export function formatOpenMeteoLocation(location: OpenMeteoLocation) {
  return [location.name, location.admin1, location.country]
    .filter(Boolean)
    .join(', ')
}