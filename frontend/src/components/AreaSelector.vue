<script setup>
// Adds a draggable rectangular selection to the Leaflet map and converts its tile-based bounds into geographic coordinates.
import { watch, onBeforeUnmount } from 'vue'
import L from 'leaflet'

const props = defineProps({
  map: { type: Object, default: null },
  enabled: { type: Boolean, default: false },
  gridSize: { type: Number, default: 32 }
})

const emit = defineEmits(['selected'])

const ZOOM = 20
const TILE_COUNT = 2 ** ZOOM

let activeMap = null
let selectionRectangle = null
let startX = 0
let startY = 0
let dragging = false
let dragStart = null
let originalStart = null

// Convert longitude into an X coordinate in the Web Mercator tile grid.

function tileX(lon) {
  return Math.floor((lon + 180) / 360 * TILE_COUNT)
}

// Convert latitude into a Y coordinate in the Web Mercator tile grid.

function tileY(lat) {
  const radians = lat * Math.PI / 180
  return Math.floor(
    (1 - Math.asinh(Math.tan(radians)) / Math.PI) / 2 * TILE_COUNT
  )
}

// Convert a tile-row coordinate back into latitude.

function latitudeFromTile(y) {
  return Math.atan(Math.sinh(Math.PI * (1 - 2 * y / TILE_COUNT))) * 180 / Math.PI
}

// Convert a tile-column coordinate back into longitude.

function longitudeFromTile(x) {
  return x / TILE_COUNT * 360 - 180
}

// Keep the selected grid rectangle inside the valid tile range.

function clampStart(value, size) {
  return Math.max(0, Math.min(TILE_COUNT - size, value))
}

// Recalculate the rectangle bounds and update its position on the map.

function updateRectangle() {
  if (!activeMap) return

  const width = props.gridSize
  const height = width / 2
  const north = latitudeFromTile(startY)
  const south = latitudeFromTile(startY + height)
  const west = longitudeFromTile(startX)
  const east = longitudeFromTile(startX + width)

  const bounds = [
    [north, west],
    [south, east]
  ]

  if (!selectionRectangle) {
    selectionRectangle = L.rectangle(bounds, {
      color: '#f3751a',
      weight: 3,
      fillColor: '#f3751a',
      fillOpacity: 0.12,
      dashArray: '8, 6'
    }).addTo(activeMap)
  } else {
    selectionRectangle.setBounds(bounds)
  }
}

function positionAtMapCentre() {
  if (!activeMap) return

  const centre = activeMap.getCenter()
  const width = props.gridSize
  const height = width / 2

  startX = clampStart(tileX(centre.lng) - Math.floor(width / 2), width)
  startY = clampStart(tileY(centre.lat) - Math.floor(height / 2), height)

  updateRectangle()
}

function startDragging(event) {
  if (!props.enabled || !selectionRectangle || !activeMap) return
  if (!selectionRectangle.getBounds().contains(event.latlng)) return

  dragging = true
  dragStart = {
    x: tileX(event.latlng.lng),
    y: tileY(event.latlng.lat)
  }
  originalStart = { x: startX, y: startY }

  activeMap.dragging.disable()
}

function moveRectangle(event) {
  if (!dragging || !activeMap) return

  const deltaX = tileX(event.latlng.lng) - dragStart.x
  const deltaY = tileY(event.latlng.lat) - dragStart.y
  const width = props.gridSize
  const height = width / 2

  startX = clampStart(originalStart.x + deltaX, width)
  startY = clampStart(originalStart.y + deltaY, height)

  updateRectangle()
}

function stopDragging() {
  if (!dragging || !activeMap) return

  dragging = false
  activeMap.dragging.enable()
}

function confirmSelection() {
  if (!selectionRectangle) return

  const north = latitudeFromTile(startY)
  const south = latitudeFromTile(startY + props.gridSize / 2)
  const west = longitudeFromTile(startX)
  const east = longitudeFromTile(startX + props.gridSize)

  emit('selected', { north, south, east, west })
}

function attachMap() {
  if (!props.map || activeMap === props.map || !props.enabled) return

  detachMap()
  activeMap = props.map

  activeMap.on('mousedown', startDragging)
  activeMap.on('mousemove', moveRectangle)
  activeMap.on('mouseup', stopDragging)

  positionAtMapCentre()
}

function detachMap() {
  if (!activeMap) return

  activeMap.off('mousedown', startDragging)
  activeMap.off('mousemove', moveRectangle)
  activeMap.off('mouseup', stopDragging)

  if (dragging) activeMap.dragging.enable()

  if (selectionRectangle) {
    activeMap.removeLayer(selectionRectangle)
    selectionRectangle = null
  }

  activeMap = null
  dragging = false
}

watch(() => props.map, attachMap, { immediate: true })

watch(() => props.enabled, enabled => {
  if (enabled) attachMap()
  else detachMap()
})

watch(() => props.gridSize, () => {
  if (!activeMap || !selectionRectangle) return

  const oldSize = selectionRectangle.getBounds()
  const centre = oldSize.getCenter()

  const width = props.gridSize
  const height = width / 2
  startX = clampStart(tileX(centre.lng) - Math.floor(width / 2), width)
  startY = clampStart(tileY(centre.lat) - Math.floor(height / 2), height)

  updateRectangle()
})

onBeforeUnmount(detachMap)

defineExpose({ confirmSelection, recenter: positionAtMapCentre })
</script>

<template>
  <!-- Component interface: values come from props; user actions are sent to the parent with emit(). -->
  <div class="area-selector-status" v-if="enabled">
    Drag the orange box to reposition it, then capture the area
  </div>
</template>

<style scoped>
.area-selector-status {
  position: absolute;
  top: 15px;
  left: 50%;
  transform: translateX(-50%);
  z-index: 1000;
  padding: 9px 15px;
  background: #202020;
  border: 1px solid #f3751a;
  border-radius: 5px;
  color: white;
  font-size: 13px;
  pointer-events: none;
  white-space: nowrap;
}
</style>
