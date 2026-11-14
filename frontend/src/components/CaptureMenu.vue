
<script setup>
// Presents screenshot and area-capture choices, including the area grid-size controls.
import { ref } from 'vue'

defineProps({
  open: {
    type: Boolean,
    default: false
  },
  captureMode: {
    type: String,
    default: null
  },
  areaGridSize: {
    type: Number,
    default: 32
  }
})

const emit = defineEmits(['close', 'select-mode', 'area-size-change', 'capture-area', 'recenter-area'])

</script>

<template>
  <!-- Component interface: values come from props; user actions are sent to the parent with emit(). -->
  <div
    v-if="open"
    ref="popupElement"
    class="capture-popup"
  >
    <div class="capture-popup-heading">
      <h3>Process Image</h3>
      <button @click="emit('close')">×</button>
    </div>

    <div v-if="captureMode !== 'area'" class="capture-options">
      <button
        class="capture-option"
        @click="emit('select-mode', 'screenshot')"
      >
        <span class="capture-icon screenshot-icon"></span>
        Screenshot
      </button>

      <button
        class="capture-option"
        @click="emit('select-mode', 'area')"
      >
        <span class="capture-icon area-icon"></span>
        Select Area
      </button>
    </div>

    <div v-else class="area-capture-options">
      <div class="area-size-options">
        <button
          v-for="size in [16, 32, 64, 128]"
          :key="size"
          type="button"
          :class="{ selected: areaGridSize === size }"
          @click="emit('area-size-change', size)"
        >
          {{ size }} × {{ size / 2 }}
        </button>
      </div>

      <div v-if="areaGridSize === 128"
           class="large-scan-warning" role="alert">
        <strong>Large scan warning</strong>
        <p>
          128 × 64 captures approximately 134 million pixels.
          Downloading and processing may take considerably longer
          and require substantial memory. Processing may fail if
          the host system runs out of available memory.
        </p>
      </div>

      <button
        class="recenter-area-button"
        type="button"
        @click="emit('recenter-area')"
      >
        Re-centre Box
      </button>

      <button
        class="capture-area-button"
        type="button"
        @click="emit('capture-area')"
      >
        Capture Area
      </button>
      <p class="area-zoom">Zoom: 20</p>
    </div>
  </div>
</template>

<style scoped>
.capture-popup {
  position: absolute;
  right: 15px;
  bottom: 15px;
  z-index: 1100;
  width: 400px;
  max-width: calc(100vw - 30px);
  padding: 15px;
  background-color: #202020;
  border: 1px solid #f3751a;
  border-radius: 5px;
  color: white;
  box-shadow: 0 4px 12px rgba(0, 0, 0, 0.4);
}

.capture-popup-heading {
  display: flex;
  justify-content: space-between;
  align-items: center;
  border-bottom: 1px solid #555;
  padding-bottom: 8px;
  margin-bottom: 10px;
}

.capture-popup-heading h3 {
  margin: 0;
  color: white;
}

.capture-popup-heading button {
  background: transparent;
  color: white;
  border: none;
  font-size: 20px;
  cursor: pointer;
}

.capture-options {
  display: flex;
  gap: 8px;
}

.capture-option {
  flex: 1;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 8px;
  min-height: 75px;
  background: #333;
  color: white;
  border: 1px solid #555;
  border-radius: 4px;
  cursor: pointer;
}

.capture-option:hover,
.capture-option.selected {
  background: #49301f;
  border-color: #f3751a;
}

.capture-icon {
  display: block;
  width: 23px;
  height: 18px;
}

.screenshot-icon {
  border: 2px solid #f3751a;
  position: relative;
}

.screenshot-icon::after {
  content: '';
  position: absolute;
  inset: 4px;
  border: 1px solid #f3751a;
}

.area-icon {
  border: 2px dashed #f3751a;
}

.area-capture-options {
  display: flex;
  flex-direction: column;
  gap: 10px;
}

.area-size-options {
  display: grid;
  grid-template-columns: repeat(4, minmax(0, 1fr));
  gap: 8px;
}

.area-size-options button,
.capture-area-button {
  padding: 10px 5px;
  background: #333;
  color: white;
  border: 1px solid #666;
  border-radius: 4px;
  cursor: pointer;
}

.area-size-options button.selected,
.area-size-options button:hover {
  background: #49301f;
  border-color: #f3751a;
}

.capture-area-button {
  width: 100%;
  background: #f3751a;
  color: #202020;
  font-weight: bold;
}

.large-scan-warning {
  padding: 10px;
  border: 1px solid #e87924;
  border-radius: 5px;
  background: rgba(232, 121, 36, 0.12);
  color: white;
  font-size: 12px;
  line-height: 1.5;
}
.large-scan-warning strong {
  display: block;
  color: #ffad66;
  margin-bottom: 4px;
}
.large-scan-warning p {
  margin: 0;
}

.area-zoom {
  margin: 0;
  color: #ccc;
  font-size: 12px;
  text-align: center;
}

</style>

<style scoped>
.recenter-area-button {
  width: 100%;
  padding: 9px;
  background: #333;
  color: white;
  border: 1px solid #777;
  border-radius: 4px;
  cursor: pointer;
}
.recenter-area-button:hover {
  border-color: #f3751a;
  background: #49301f;
}
</style>
