
<script setup>
// Displays saved quick locations and allows authenticated users to add, select or remove them.
import { ref } from 'vue'

defineProps({
  authenticated: { type: Boolean, default: false },
  locations: {
    type: Array,
    default: () => []
  }
})

const emit = defineEmits(['add', 'remove', 'select'])

const locationName = ref('')
const error = ref('')

// Validate the entered name before asking the parent component to save it.
function addLocation() {
  const name = locationName.value.trim()

  if (!name) {
    error.value = 'Please enter a location name.'
    return
  }

  emit('add', name)
  locationName.value = ''
  error.value = ''
}
</script>

<template>
  <!-- Component interface: values come from props; user actions are sent to the parent with emit(). -->
  <div class="quick-location-manager">
    <div class="add-location">
      <h3>Add Quick Location</h3>

      <template v-if="authenticated">
        <label for="quick-location-name">Location name</label>
        <input
          id="quick-location-name"
          v-model="locationName"
          type="text"
          maxlength="60"
          placeholder="e.g. Rock Site 1"
          @keyup.enter="addLocation"
        >
        <button class="add-button" @click="addLocation">+ Add Location</button>
      </template>
      <p v-else class="login-required">Log in to add Quick Locations</p>

      <p v-if="error" class="error">{{ error }}</p>
    </div>

   <h3 class="locations-heading">Quick Locations</h3>
   <div class="custom-locations">
      <div
        v-for="location in locations"
        :key="location.id"
        class="custom-location"
      >
        <button
          class="location-select"
          @click="emit('select', location)"
          :title="`${location.latitude}, ${location.longitude}`"
        >
          {{ location.name }}
        </button>

        <button
          v-if="authenticated"
          class="location-delete"
          title="Delete location"
          @click="emit('remove', location.id)"
        >
          ×
        </button>
      </div>
    </div>
  </div>
</template>

<style scoped>
.quick-location-manager {
  margin-top: 12px;
}

.locations-heading {
  font-size: 17px;
  margin: 16px 0 10px;
  color: white;
}

.custom-locations {
  display: flex;
  flex-direction: column;
  align-items: stretch;
  gap: 7px;
  width: 100%;
  overflow-y: visible;
  min-height: 0;
  padding-right: 3px;
}

.custom-location {
  display: flex;
  align-items: stretch;
  gap: 5px;
  width: 100%;
}

.location-select {
  flex: 1;
  min-width: 0;
  width: 100%;
  text-align: center;
  background: #f3751a;
  color: #111;
  border: 0;
  border-radius: 5px;
  font-weight: bold;
  padding: 9px 7px;
  overflow-wrap: anywhere;
}

.location-delete {
  background: #49251c;
  color: #ff9b65;
  border: 0;
  border-radius: 4px;
  min-width: 36px;
  cursor: pointer;
  font-size: 18px;
  font-weight: bold;
}

.add-location {
  display: flex;
  flex-direction: column;
  gap: 7px;
  margin-top: 14px;
  padding-top: 12px;
  border-top: 1px solid #555;
}

.add-location h3 {
  margin: 0 0 4px;
}

.add-location label {
  font-size: 14px;
  font-weight: bold;
}

.add-location input {
  width: 100%;
  background: #222;
  color: white;
  border: 1px solid #777;
  border-radius: 4px;
  padding: 9px;
}

.add-button {
  background: #f3751a;
  color: #111;
  border: 0;
  border-radius: 5px;
  padding: 9px;
  font-weight: bold;
}

.login-required {
  margin: 2px 0 0;
  padding: 10px;
  border: 1px solid #555;
  border-radius: 4px;
  background: #222;
  color: #ccc;
  font-size: 13px;
  text-align: center;
}

.error {
  color: #ff9b65;
  font-size: 13px;
  margin: 0;
}
</style>
