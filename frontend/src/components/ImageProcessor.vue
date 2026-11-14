<script setup>
// Displays the original and processed images, filter controls, progress information and save options.
import { reactive, ref } from 'vue'

const props = defineProps({
  canSave: { type: Boolean, default: false },
  settings: {
    type: Object,
    required: true
  },
  captureMode: {
    type: String,
    default: null
  },
  originalImage: {
    type: String,
    default: null
  },
  processedImage: {
    type: String,
    default: null
  },
  saving: {
    type: Boolean,
    default: false
  },
  processing: {
    type: Boolean,
    default: false
  },
  progressPercent: { type: Number, default: 0 },
  progressStage: { type: String, default: 'Preparing image' },
  progressCurrent: { type: Number, default: 0 },
  progressTotal: { type: Number, default: 0 },
  saveStatus: {
    type: String,
    default: ''
  }
})

const emit = defineEmits(['process', 'back', 'save', 'reset'])
const filters = reactive({ ...props.settings })

const imageTitle = ref('')
const imageNotes = ref('')

const captureSettings = reactive({
  gridSize: 8,
  zoomLevel: 20
})

function resetFilters() {
  Object.assign(filters, props.settings)

  imageTitle.value = ''
  imageNotes.value = ''

  emit('reset')
}

function runProcessing() {
  emit('process', {
    ...filters,
    ...captureSettings
  })
}

function saveImage() {
  const title = imageTitle.value.trim()
  const notes = imageNotes.value

  if (!title || title.length > 120 || notes.length > 2000) return

  emit('save', { title, notes })
}
</script>

<template>
  <!-- Component interface: values come from props; user actions are sent to the parent with emit(). -->
  <section class="processor">
    <h2>Image Processing</h2>

    <div class="processing-layout">
     <div class="image-preview">
  <img
    v-if="originalImage"
    :src="originalImage"
    alt="Original satellite image"
  />
  <span v-else>Waiting for image capture</span>
</div>

      <div class="controls">
        <label>Title</label>
        <input
          type="text"
          v-model="imageTitle"
          placeholder="Requires Title"
          maxlength="120"
          required
        >

        <label>Notes</label>
        <textarea
          v-model="imageNotes"
          maxlength="2000"
        ></textarea>

        <div class="buttons">
          <div class="action-row">
            <button @click="runProcessing">Filter</button>
            <button @click="resetFilters">Reset</button>
            <button @click="emit('back')">Back</button>
          </div>

          <button
            class="save-button"
            @click="saveImage"
            :disabled="!canSave || !processedImage || !imageTitle.trim() || imageTitle.trim().length > 120 || imageNotes.length > 2000 || saving || saveStatus.startsWith('Saved')"
          >
            {{
              saving ? 'Saving...' :
              saveStatus.startsWith('Saved') ? 'Saved Successfully!' :
              saveStatus.startsWith('Save failed') ? 'Save Failed' :
              canSave ? 'Save to Database' : 'Log in to save to database'
            }}
          </button>
        </div>
      </div>

      <div class="image-preview">
        <img
          v-if="processedImage"
          :src="processedImage"
          alt="Processed satellite image"
        />
        <span v-else>Processed Image</span>
      </div>
    </div>

    <div class="filter-bar">
      <label v-for="filter in ['water', 'dirt', 'vegetation', 'noise', 'parallel', 'shadows', 'rocks']" :key="filter">
        <input type="checkbox" v-model="filters[filter]">
        {{ filter === 'rocks' ? 'Identify Rocks (Warning: This Adds Significant Processing Time)' : filter.charAt(0).toUpperCase() + filter.slice(1) }}
      </label>

      <div v-if="processing" class="processing-indicator">
        <div class="progress-label">
          <span>{{ progressStage }}</span>
          <strong>{{ progressPercent }}%</strong>
        </div>
        <div class="progress-track">
          <div
            class="progress-bar"
            :style="{ width: `${progressPercent}%` }"
          ></div>
        </div>
        <div v-if="progressTotal > 0" class="progress-count">
          {{ progressCurrent.toLocaleString() }} / {{ progressTotal.toLocaleString() }}
        </div>
      </div>
      </div>
  </section>
</template>

<style scoped>
.image-preview img {
  display: block;
  width: 100%;
  height: auto;
  min-height: 0;
  max-height: 500px;
  object-fit: contain;
}

.processor h2 {
  color: #f3751a;
  font-size: 16px;
}

.processing-layout {
  display: grid;
  grid-template-columns: minmax(0, 1fr) 235px minmax(0, 1fr);
  gap: 9px;
}

.image-preview {
  min-height: 185px;
  background: #404040;
  border: 1px solid #777;
  display: flex;
  align-items: center;
  justify-content: center;
  overflow: hidden;
  color: white;
}

.controls {
  padding: 9px;
  background: #292929;
  border: 1px solid #777;
  display: flex;
  flex-direction: column;
  gap: 6px;
}

.controls input,
.controls textarea {
  width: 100%;
  padding: 6px;
  background: #171717;
  color: white;
  border: 1px solid #777;
}

.controls textarea {
  min-height: 150px;
  resize: vertical;
}

.buttons {
  display: flex;
  gap: 4px;
  flex-wrap: wrap;
}

button {
  background: #f3751a;
  color: #111;
  border: 0;
  border-radius: 5px;
  padding: 7px 10px;
  font-weight: bold;
}

.filter-bar {
  display: flex;
  flex-wrap: wrap;
  gap: 15px;
  margin-top: 8px;
  padding: 8px;
  background: #202020;
  border: 1px solid #777;
}

.capture-settings {
  display: flex;
  align-items: center;
  gap: 12px;
  padding-right: 12px;
  margin-right: 5px;
  border-right: 1px solid #555;
}

.fixed-zoom {
  color: #ddd;
  font-size: 12px;
  white-space: nowrap;
}

.capture-settings label {
  display: flex;
  align-items: center;
  gap: 5px;
  color: #ddd;
  font-size: 12px;
  white-space: nowrap;
}

.capture-settings select {
  background: #202020;
  color: white;
  border: 1px solid #666;
  border-radius: 3px;
  padding: 3px 5px;
  font-size: 12px;
  cursor: pointer;
}

.filter-bar {
  display: flex;
  align-items: center;
  flex-wrap: wrap;
  gap: 12px;
}

.filter-bar input {
  accent-color: #f3751a;
}

@media (max-width: 850px) {
  .processing-layout {
    grid-template-columns: 1fr;
  }
}

.processing-indicator {
  margin-left: auto;
  flex: 2 1 440px;
  max-width: 640px;
  min-width: 180px;
  color: #f0f0f0;
  font-size: 12px;
}

.progress-track {
  height: 6px;
  margin-top: 6px;
  overflow: hidden;
  background: #444;
  border-radius: 5px;
}

.progress-label {
  display: flex;
  justify-content: space-between;
  gap: 12px;
}

.progress-label strong {
  font-variant-numeric: tabular-nums;
}

.progress-bar {
  width: 0%;
  height: 100%;
  background: #f3751a;
  border-radius: 5px;
  transition: width 0.2s ease;
}

.progress-count {
  margin-top: 4px;
  color: #aaa;
  font-size: 11px;
}

</style>

<style scoped>
.save-status {
  margin: 5px 0;
  font-size: 13px;
  color: #f0c36d;
}
.save-status.success {
  color: #62d68b;
}
.save-status.error {
  color: #ff7777;
}
button:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

button.save-success {
  background: #238b45;
  color: white;
}

button.save-error {
  background: #c0392b;
  color: white;
}

.buttons {
  display: flex;
  flex-direction: column;
  gap: 10px;
}

.action-row {
  display: flex;
  gap: 8px;
}

.action-row button {
  flex: 1;
}

.save-button {
  width: 100%;
}
</style>


<style scoped>
/* SGIS expandable image processing area */
.processor {
  display: flex;
  flex: 1;
  flex-direction: column;
  min-height: 0;
}

.processing-layout {
  flex: 1;
  min-height: 0;
}

.image-preview {
  height: 100%;
  min-height: 185px;
}

@media (max-width: 850px) {
  .processor {
    flex: initial;
  }

  .processing-layout {
    flex: initial;
  }

  .image-preview {
    height: auto;
    min-height: 185px;
  }
}
</style>
