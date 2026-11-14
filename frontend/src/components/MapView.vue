
<script setup>
// Creates and manages the Leaflet map, loads LINZ imagery and exposes map information to the parent component.
import { ref, onMounted, onBeforeUnmount, watch } from 'vue'
import L from 'leaflet'

const props = defineProps({
  latitude: {
    type: Number,
    default: -45.515
  },
  longitude: {
    type: Number,
    default: 169.785
  },
  imagerySource: {
    type: String,
    default: 'aerial',
    validator: value => ['aerial', 'otago-2025-0.2m'].includes(value)
  }
})

const mapElement = ref(null)
const mapError = ref('')
let leafletMap = null
let imageryLayer = null
let imageryBaseUrl = ''

// Return the current map centre as rounded latitude and longitude values.

function getCentre() {
  if (!leafletMap) {
    return null
  }

  const centre = leafletMap.getCenter()

  return {
    latitude: Number(centre.lat.toFixed(7)),
    longitude: Number(centre.lng.toFixed(7))
  }
}

// Expose the map container so another component can capture it.

function getMapElement() {
  return mapElement.value
}

// Expose the Leaflet instance for map tools that need direct access.

function getLeafletMap() {
  return leafletMap
}


defineExpose({
  getCentre,
  getMapElement,
  getLeafletMap
})

// Create the Leaflet map, load saved settings and add the selected imagery layer.

async function loadMap() {
leafletMap = L.map(mapElement.value, {
  zoomControl: false,
  attributionControl: true
}).setView(
    [props.latitude, props.longitude],
    13
  )


  try {
    const response = await fetch('/api/settings')

    if (!response.ok) {
      throw new Error(`Settings API returned HTTP ${response.status}`)
    }

    const settings = await response.json()

    if (!settings.linzUrl) {
      throw new Error('LINZ tile URL was not returned by the API')
    }

    // Keep the backend-provided LINZ API key and switch only
    // between the two approved aerial imagery tilesets.
    imageryBaseUrl = settings.linzUrl

    const tileUrl = imageryBaseUrl.replace(
      /\/v1\/tiles\/[^/]+\/WebMercatorQuad\//,
      `/v1/tiles/${props.imagerySource}/WebMercatorQuad/`
    )

    imageryLayer = L.tileLayer(tileUrl, {
      maxZoom: 22,
      attribution: '&copy; LINZ'
    })

    imageryLayer.addTo(leafletMap)

    // Use the saved backend coordinates when available.
    if (
      Number.isFinite(Number(settings.lat)) &&
      Number.isFinite(Number(settings.lng))
    ) {
      leafletMap.setView(
        [Number(settings.lat), Number(settings.lng)],
        13
      )
    }
  } catch (error) {
    console.error('Map loading failed:', error)
    mapError.value = error.message
  }
}

// Update the displayed aerial imagery when the toolbar selection changes.
watch(
  () => props.imagerySource,
  source => {
    if (!imageryLayer || !imageryBaseUrl) return

    const tileUrl = imageryBaseUrl.replace(
      /\/v1\/tiles\/[^/]+\/WebMercatorQuad\//,
      `/v1/tiles/${source}/WebMercatorQuad/`
    )

    imageryLayer.setUrl(tileUrl)
  }
)

// Update the map when the parent changes coordinates.
watch(
  () => [props.latitude, props.longitude],
  ([latitude, longitude]) => {
    if (leafletMap) {
      leafletMap.setView([latitude, longitude], leafletMap.getZoom())
    }
  }
)

onMounted(() => {
  loadMap()
})

onBeforeUnmount(() => {
  if (leafletMap) {
    leafletMap.remove()
    leafletMap = null
  }

})
</script>

<template>
  <!-- Component interface: values come from props; user actions are sent to the parent with emit(). -->
  <div class="map-container">
    <div ref="mapElement" class="map-canvas"></div>

    <div v-if="mapError" class="map-error">
      Unable to load LINZ imagery: {{ mapError }}
    </div>
  </div>
</template>


<style scoped>
:deep(.leaflet-top.leaflet-left) {
  left: auto !important;
  right: 10px !important;
}

:deep(.leaflet-top.leaflet-right) {
  right: 10px !important;
  left: auto !important;
}

.map-container {
  position: relative;
  width: 100%;
  height: 52vh;
  min-height: 300px;
  border: 2px solid #7c7c7c;
  overflow: hidden;
  background: #333;
}

.map-canvas {
  width: 100%;
  height: 100%;
}

.map-error {
  position: absolute;
  bottom: 12px;
  left: 12px;
  z-index: 1000;
  padding: 10px;
  background: #181818;
  color: #ff9b65;
  border: 1px solid #f3751a;
  max-width: 90%;
  font-size: 13px;
}


</style>
