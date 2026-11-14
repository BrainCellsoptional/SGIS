
<script setup>
// Toolbar for switching between the image database and processing views, and choosing the aerial imagery source.
const props = defineProps({
  activePanel: {
    type: String,
    default: 'database'
  },
  imagerySource: {
    type: String,
    default: 'aerial'
  }
})

const emit = defineEmits([
  'show-database',
  'show-processor',
  'update-imagery'
])
</script>

<template>
  <!-- Component interface: values come from props; user actions are sent to the parent with emit(). -->
  <div class="toolbar">
    <button
      :class="{ active: activePanel === 'database' || activePanel === 'details' }"
      @click="emit('show-database')"
    >
      Database
    </button>

    <button
      :class="{ active: activePanel === 'process' }"
      @click="emit('show-processor')"
    >
      Process Image
    </button>

    <label class="imagery-selector">
      Aerial source
      <select
        :value="imagerySource"
        @change="emit('update-imagery', $event.target.value)"
        aria-label="Aerial imagery source"
      >
          <option value="aerial">LINZ Aerial</option>
          <option value="otago-2025-0.2m">Otago 0.2m Aerial</option>
          <option value="canterbury-2024-0.2m">Canterbury 2024 (0.2m)</option>
          <option value="bay-of-plenty-2024-0.2m">Bay of Plenty 2024-2025 (0.2m)</option>
          <option value="wellington-2025-0.2m">Wellington 2025 (0.2m)</option>
          <option value="west-coast-2024-2026-0.2m">West Coast 2024-2026 (0.2m)</option>
          <option value="hawkes-bay-2025-2026-0.2m">Hawke's Bay 2025-2026 (0.2m)</option>
      </select>
    </label>
  </div>
</template>

<style scoped>
.toolbar {
  display: flex;
  align-items: center;
  gap: 7px;
  min-height: 40px;
  padding: 6px;
  background: #191919;
  border: 1px solid #555;
}

button {
  padding: 6px 16px;
  border: 1px solid #111;
  border-radius: 6px;
  background: #f3751a;
  color: #111;
  font-weight: bold;
  font-size: 12px;
}

button:hover {
  background: #ff984b;
}

.imagery-selector {
  display: flex;
  align-items: center;
  gap: 7px;
  margin-left: 5px;
  color: #eee;
  font-size: 12px;
  font-weight: bold;
}

.imagery-selector select {
  max-width: 190px;
  padding: 6px 8px;
  border: 1px solid #777;
  border-radius: 5px;
  background: #202020;
  color: white;
  font-size: 12px;
}

button.active {
  box-shadow: inset 0 0 0 1px #fff;
}

body {
  font-size: 16px;
  line-height: 1.4;
}
</style>
