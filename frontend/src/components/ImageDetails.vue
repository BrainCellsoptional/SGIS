
<script setup>
// Shows a selected database image and provides editable title/notes fields, zoom controls and save/delete events.
import { reactive, ref, watch } from 'vue'

const props = defineProps({
  canEdit: { type: Boolean, default: false },
  image: {
    type: Object,
    required: true
  }
})

const emit = defineEmits(['save', 'delete', 'back'])
const zoomOpen = ref(false)
const zoom = ref(1)
function changeZoom(amount) {
  zoom.value = Math.min(4, Math.max(0.5, Number((zoom.value + amount).toFixed(2))))
}
function closeZoom() {
  zoomOpen.value = false
  zoom.value = 1
}

const form = reactive({
  id: props.image.id,
  title: props.image.title,
  notes: props.image.notes
})

watch(() => props.image, (image) => {
  Object.assign(form, image)
})
</script>

<template>
  <!-- Component interface: values come from props; user actions are sent to the parent with emit(). -->
  <section class="details">
    <h2>Viewing Image From Database</h2>

    <div class="details-layout">
      <div class="preview">
        <img
          v-if="image.file_url"
          :src="image.file_url"
          :alt="image.title"
          title="Click to zoom"
          @click="zoomOpen = true"
        >
        <span v-else>Image unavailable</span>
      </div>

      <div class="edit-form">
        <label>Title</label>
        <input v-if="canEdit" v-model="form.title">
        <p v-else class="read-value">{{ form.title || 'Untitled' }}</p>

        <p>ID: {{ form.id }}</p>

        <label>Notes</label>
        <textarea v-if="canEdit" v-model="form.notes"></textarea>
        <p v-else class="read-value notes-value">{{ form.notes || 'No notes saved.' }}</p>

        <div class="buttons">
          <template v-if="canEdit">
            <button @click="emit('save', { ...form })">Save</button>
            <button @click="emit('delete')">Delete</button>
          </template>
          <button @click="emit('back')">Back</button>
        </div>
      </div>
    </div>

    <div v-if="zoomOpen" class="zoom-overlay" @click.self="closeZoom" tabindex="0">
      <div class="zoom-toolbar">
        <button type="button" @click="changeZoom(-0.25)" :disabled="zoom <= 0.5">−</button>
        <span>{{ Math.round(zoom * 100) }}%</span>
        <button type="button" @click="changeZoom(0.25)" :disabled="zoom >= 4">+</button>
        <button type="button" @click="zoom = 1">Reset</button>
        <button type="button" @click="closeZoom">Close ✕</button>
      </div>
      <div class="zoom-viewport">
        <img :src="image.file_url" :alt="image.title" :style="{ transform: `scale(${zoom})` }">
      </div>
    </div>
  </section>
</template>

<style scoped>
h2 {
  color: #f3751a;
  font-size: 16px;
  margin: 0 0 8px;
}

.details-layout {
  display: grid;
  grid-template-columns: minmax(0, 2fr) minmax(220px, .8fr);
  gap: 10px;
}

.preview {
  min-height: 200px;
  background: #404040;
  border: 1px solid #777;
  display: grid;
  place-items: center start;
  color: #ccc;
  font-size: 12px;
}

.preview img {
  max-width: 100%;
  max-height: 500px;
  width: 100%;
  height: 100%;
  object-fit: contain;
  cursor: zoom-in;
}

.zoom-overlay {
  position: fixed; inset: 0; z-index: 5000;
  display: flex; flex-direction: column;
  background: rgba(0, 0, 0, 0.94); color: white;
}
.zoom-toolbar {
  display: flex; align-items: center; justify-content: center;
  gap: 12px; padding: 12px; background: #202020;
}
.zoom-viewport {
  flex: 1; overflow: auto; display: grid; place-items: center; padding: 24px;
}
.zoom-viewport img {
  max-width: 90%; max-height: 85%; object-fit: contain;
  transform-origin: center; transition: transform 0.15s ease;
}

.edit-form {
  background: #292929;
  border: 1px solid #777;
  padding: 10px;
  font-size: 11px;
}

.edit-form label {
  display: block;
  margin: 4px 0;
}

.edit-form input,
.edit-form textarea {
  width: 100%;
  padding: 6px;
  background: #171717;
  border: 1px solid #777;
  color: white;
}

.edit-form textarea {
  min-height: 150px;
  resize: vertical;
}

.read-value {
  box-sizing: border-box;
  width: 100%;
  min-height: 36px;
  margin: 0 0 12px;
  padding: 8px;
  border: 1px solid #555;
  background: #202020;
  color: #f2f2f2;
  font-size: 13px;
  overflow-wrap: anywhere;
}

.notes-value {
  min-height: 150px;
  white-space: pre-wrap;
}

.buttons {
  display: flex;
  gap: 4px;
  flex-wrap: wrap;
  margin-top: 8px;
}

button {
  background: #f3751a;
  color: #111;
  border: 0;
  border-radius: 5px;
  padding: 7px 12px;
  font-size: 11px;
  font-weight: bold;
}

@media (max-width: 650px) {
  .details-layout {
    grid-template-columns: 1fr;
  }
}
</style>
