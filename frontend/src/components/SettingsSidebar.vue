<script setup>
// Provides the settings panel, account controls, map-location controls and quick-location manager.
import QuickLocationManager from './QuickLocationManager.vue'

defineProps({
  settings: {
    type: Object,
    required: true
  },
  open: Boolean,
  authenticated: { type: Boolean, default: false },
  user: { type: Object, default: null },
  customLocations: {
    type: Array,
    default: () => []
  }
})

const emit = defineEmits([
  'toggle',
  'close',
  'location',
  'save',
  'add-location',
  'remove-location',
  'login',
  'logout'
])

// Create a short label for a location button in the collapsed sidebar.
function compactLocationName(name) {
  const words = name.trim().split(/\\s+/)

  if (words.length > 1) {
    return words.map(word => word.charAt(0)).join('').toUpperCase()
  }

  if (name.length > 5) {
    return name.substring(0, 2).toUpperCase()
  }

  return name.toUpperCase()
}

// Pass a copy of the edited settings back to the parent component.
function save(settings) {
  emit('save', { ...settings })
}
</script>


<template>
  <!-- Component interface: values come from props; user actions are sent to the parent with emit(). -->
  <aside class="sidebar" :class="{ expanded: open }">
    <div v-if="!open" class="collapsed">
      <button class="sidebar-toggle" type="button" aria-label="Open settings" title="Open settings" @click="emit('toggle')">>></button>
      <button
        class="sidebar-auth-button collapsed-auth-button"
        type="button"
        :title="authenticated ? 'Log out' : 'Log in to access restricted controls'"
        @click="authenticated ? emit('logout') : emit('login')"
      >
        {{ authenticated ? 'Log out' : 'Log in' }}
      </button>

      <button
  v-for="location in customLocations"
  :key="location.id"
  class="quick-button"
  :title="location.name"
  @click="emit('location', location)"
>
  {{ compactLocationName(location.name) }}
</button>
    </div>

    <div v-else class="expanded-content">
      <div class="sidebar-heading">
        <h2>Settings</h2>
        <button class="close" @click="emit('close')">×</button>
      </div>

      <div class="sidebar-account">
        <span v-if="authenticated && user" class="sidebar-username">Signed in as {{ user.username }}</span>
        <button type="button" class="sidebar-auth-button" @click="authenticated ? emit('logout') : emit('login')">
          {{ authenticated ? 'Log out' : 'Log in to access restricted controls' }}
        </button>
      </div>

      <label for="longitude">Longitude</label>
      <input id="longitude" type="number" step="any" v-model.number="settings.longitude" :disabled="!authenticated">

      <label for="latitude">Latitude</label>
      <input id="latitude" type="number" step="any" v-model.number="settings.latitude" :disabled="!authenticated">

      <h3>Default Filters</h3>

      <div class="filters">
        <label v-for="filter in ['water', 'dirt', 'vegetation', 'noise', 'parallel', 'shadows', 'rocks']" :key="filter">
          <input type="checkbox" v-model="settings[filter]" :disabled="!authenticated">
          {{ filter === 'rocks' ? 'Identify Rocks' : filter.charAt(0).toUpperCase() + filter.slice(1) }}
        </label>
      </div>

      <button class="save-button" :disabled="!authenticated" @click="save(settings)">{{ authenticated ? 'Save Settings' : 'Log in to edit settings' }}</button>



      <QuickLocationManager
        :locations="customLocations"
        :authenticated="authenticated"
        @add="emit('add-location', $event)"
        @remove="emit('remove-location', $event)"
        @select="emit('location', $event)"
      />
    </div>
  </aside>
</template>

<style scoped>

.sidebar {
  position: absolute;
  top: 0;
  bottom: 0;
  left: 0;
  width: 96px;
  min-height: 100%;
  background: #171717;
  border-right: 3px solid #f3751a;
  color: white;
  z-index: 1000;
  transition: width 0.2s ease;
}

.sidebar.expanded {
  width: 237px;
}

.collapsed {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 12px;
  padding-top: 12px;
}

.sidebar .sidebar-toggle {
  width: 72px;
  min-height: 64px !important;
  padding: 0 !important;
  border: none !important;
  border-radius: 0;
  background: transparent !important;
  color: #ffffff !important;
  font-size: 40px !important;
  line-height: 1;
  font-weight: 600;
  cursor: pointer;
  transition: color 0.2s ease, transform 0.2s ease;
}

.sidebar .sidebar-toggle:hover {
  background: transparent !important;
  color: #cccccc !important;
  transform: scale(1.12);
}

.quick-button,
.save-button,
.sidebar-auth-button {
  background: #f3751a;
  color: #111;
  border: 0;
  border-radius: 5px;
  font-weight: bold;
  font-size: 12px;
  padding: 8px;
}

.quick-button {
  width: 72px;
}

.sidebar-auth-button {
  width: 100%;
  background: #f3751a;
  color: #111;
  border: 0;
  border-radius: 5px;
  font-weight: bold;
  padding: 8px 6px;
}

.sidebar-account {
  display: flex;
  flex-direction: column;
  gap: 7px;
  margin: 12px 0 4px;
}

.sidebar-username {
  color: #ccc;
  font-size: 12px;
  overflow-wrap: anywhere;
}

.expanded-content {
  display: flex;
  flex-direction: column;
  padding: 14px;
  height: 100%;
  overflow-y: auto;
}

.sidebar-heading {
  display: flex;
  align-items: center;
  justify-content: space-between;
  border-bottom: 1px solid #f3751a;
}

.sidebar-heading h2 {
  font-size: 20px;
  margin: 5px 0 12px;
}

.close {
  color: white;
  background: transparent;
  border: 0;
  font-size: 26px;
}

h3 {
  font-size: 15px;
  margin: 16px 0 10px;
}

.expanded-content > label {
  display: block;
  font-size: 13px;
  font-weight: bold;
  margin: 12px 0 5px;
}

.expanded-content > input {
  width: 100%;
  padding: 8px;
  background: #222;
  border: 1px solid #777;
  color: white;
  font-size: 13px;
}

.filters {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 10px 5px;
  font-size: 13px;
}

.filters label {
  display: flex;
  align-items: center;
  gap: 5px;
}

.filters input {
  accent-color: #f3751a;
}

.save-button {
  margin-top: 16px;
  padding: 9px 14px;
}

/* Larger settings panel controls */
.sidebar {
  font-size: 15px;
}

.sidebar button {
  font-size: 14px !important;
  min-height: 34px;
  padding: 8px 11px;
}

.sidebar input:not([type="checkbox"]) {
  font-size: 15px !important;
  min-height: 38px;
}

.sidebar h2 {
  font-size: 22px;
}

.sidebar h3 {
  font-size: 17px;
}


/* Keep the login control compact when the sidebar is collapsed. */
.sidebar .collapsed-auth-button {
  box-sizing: border-box;
  flex: 0 0 auto;
  width: 72px;
  min-width: 0;
  min-height: 30px;
  padding: 5px 3px;
  font-size: 11px !important;
  line-height: 1.1;
  white-space: nowrap;
}

/* The expanded login prompt is intentionally allowed to wrap. */
.sidebar.expanded .sidebar-account .sidebar-auth-button {
  box-sizing: border-box;
  width: 100%;
  min-height: 34px;
  padding: 7px 8px;
  font-size: 12px !important;
  line-height: 1.25;
  white-space: normal;
}


/* Grey settings button when the user is logged out */
.sidebar .save-button:disabled {
  background: #555 !important;
  background-color: #555 !important;
  color: #bdbdbd !important;
  border: 1px solid #666 !important;
  cursor: not-allowed !important;
  opacity: 1 !important;
}

</style>
