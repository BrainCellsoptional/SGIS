<script setup>
// Requests an image for a selected map area and listens to the server-sent event stream for scan progress and completion.
import { watch } from 'vue'

const props = defineProps({
  selectedArea: {
    type: Object,
    default: null
  },
  zoom: {
    type: Number,
    default: 19
  },
  imagerySource: {
    type: String,
    default: 'aerial',
    validator: value => ['aerial', 'otago-2025-0.2m'].includes(value)
  }
})

const emit = defineEmits([
  'image-ready',
  'scan-started',
  'scan-progress',
  'scan-error'
])

let scanId = 0
let progressSource = null

watch(
  () => props.selectedArea,
  async area => {
    if (!area) return

    // Unique local counter used to ignore events from an older scan.
const currentScan = ++scanId

    progressSource?.close()
    emit('scan-started')

    const jobId = crypto.randomUUID()
    const source = new EventSource(`/api/area-progress/${jobId}`)
    progressSource = source

    try {
      // Wait until the progress connection is ready before starting the capture.
      await new Promise((resolve, reject) => {
        source.onopen = resolve
        source.onerror = () => reject(
          new Error('Unable to connect to the Area Scan progress stream.')
        )
      })

      source.onmessage = event => {
        if (currentScan !== scanId) return

        try {
          const progress = JSON.parse(event.data)

          if (progress.error) {
            emit('scan-error', progress.error)
            return
          }

          emit('scan-progress', progress)
        } catch (error) {
          console.error('Invalid Area Scan progress update:', error)
        }
      }

      const response = await fetch('/api/area-image', {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json'
        },
        body: JSON.stringify({
          north: area.north,
          south: area.south,
          east: area.east,
          west: area.west,
          zoom: props.zoom,
          imagerySource: props.imagerySource,
          jobId
        })
      })

      if (!response.ok) {
        const result = await response.json().catch(() => ({}))
        throw new Error(
          result.error || `Area Scan failed: HTTP ${response.status}`
        )
      }

      const imageBlob = await response.blob()

      if (!imageBlob.size) {
        throw new Error('The downloaded area image is empty.')
      }

      if (currentScan !== scanId) return

      emit('image-ready', imageBlob)

    } catch (error) {
      if (currentScan !== scanId) return

      console.error('Area Scan failed:', error)
      emit('scan-error', error.message)

    } finally {
      source.close()

      if (progressSource === source) {
        progressSource = null
      }
    }
  }
)
</script>

<template>
  <!-- Component interface: values come from props; user actions are sent to the parent with emit(). -->
  <!-- Logic-only component: no visible interface -->
</template>
