
<script setup>
// Uses html2canvas to capture the visible map as a PNG image and emits the resulting image data.
import html2canvas from 'html2canvas'

const props = defineProps({
  mapElement: {
    type: Object,
    default: null
  }
})

const emit = defineEmits(['captured', 'error'])

// Capture the map element at higher resolution and emit its PNG data URL.
async function captureScreenshot() {
  if (!props.mapElement) {
    emit('error', 'Map element could not be found.')
    return
  }

  try {
    const canvas = await html2canvas(props.mapElement, {
      useCORS: true,
      allowTaint: false,
      backgroundColor: '#202020',
      scale: 2
    })

    const imageData = canvas.toDataURL('image/png')

    emit('captured', imageData)
  } catch (error) {
    console.error('Screenshot capture failed:', error)
    emit('error', 'Unable to capture the map. Please try again.')
  }
}

defineExpose({
  captureScreenshot
})
</script>

<template>
  <!-- Component interface: values come from props; user actions are sent to the parent with emit(). -->
  <div style="display: none;"></div>
</template>
