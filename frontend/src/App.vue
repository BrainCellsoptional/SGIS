
<script setup>
// Main SGIS application shell. Coordinates the map, database, image processing, capture tools, settings and login state. Child components receive data through props and report user actions through emitted events.
import { ref, reactive, nextTick, onMounted } from 'vue'

import AppHeader from './components/AppHeader.vue'
import SettingsSidebar from './components/SettingsSidebar.vue'
import MapView from './components/MapView.vue'
import ActionToolbar from './components/ActionToolbar.vue'
import CaptureMenu from './components/CaptureMenu.vue'
import ImageDatabase from './components/ImageDatabase.vue'
import ImageDetails from './components/ImageDetails.vue'
import ImageProcessor from './components/ImageProcessor.vue'
import ScreenshotCapture from './components/ScreenshotCapture.vue'
import AreaSelector from './components/AreaSelector.vue'
import AreaScan from './components/AreaScan.vue'

// Reactive application state is shared with the child components through props.
// Vue refs track values that affect the interface; reactive() is used for grouped settings.
const activePanel = ref('database')
const imagerySource = ref('aerial')
const settingsOpen = ref(false)
const selectedImage = ref(null)
const images = ref([])
const authUser = ref(null)
const loginOpen = ref(false)
const loginUsername = ref('')
const loginPassword = ref('')
const loginError = ref('')
const loginLoading = ref(false)

// Capture workflow state: controls the capture menu and the selected capture method.
const captureMenuOpen = ref(false)
const captureMode = ref(null)

const mapRef = ref(null)
const customLocations = ref([])
const screenshotCaptureRef = ref(null)
const originalImage = ref(null)
const processedImage = ref(null)
const savingImage = ref(false)
const processingImage = ref(false)
const processingPercent = ref(0)
const processingStage = ref('Preparing image')
const processingCurrent = ref(0)
const processingTotal = ref(0)
let progressSource = null
let activeProcessingJobId = null

let processingController = null
const saveStatus = ref('')
const selectedArea = ref(null)
const areaGridSize = ref(32)
const areaSelectorRef = ref(null)

const areaCaptureLoading = ref(false)
const areaCapturePercent = ref(0)
const areaCaptureStage = ref('Preparing satellite capture')
const areaCaptureCurrent = ref(0)
const areaCaptureTotal = ref(0)

const settings = reactive({
  latitude: -45.510314,
  longitude: 169.7843164,
  water: true,
  vegetation: true,
  dirt: true,
  noise: true,
  parallel: true,
  shadows: true,
  rocks: true
})

// Ask the backend whether the current browser session is authenticated and update the displayed user state.

async function fetchAuthentication() {
  try {
    const response = await fetch('/api/auth/me')
    const result = await response.json()
    authUser.value = result.authenticated ? result.user : null
  } catch (error) {
    console.error('Unable to check login status:', error)
    authUser.value = null
  }
}

// Submit the entered credentials to the login endpoint and update the interface when login succeeds.

async function login() {
  if (loginLoading.value) return
  loginLoading.value = true
  loginError.value = ''
  try {
    const response = await fetch('/api/auth/login', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ username: loginUsername.value, password: loginPassword.value })
    })
    const result = await response.json()
    if (!response.ok || !result.success) throw new Error(result.error || 'Login failed.')
    authUser.value = result.user
    loginPassword.value = ''
    loginOpen.value = false
  } catch (error) {
    loginError.value = error.message
  } finally {
    loginLoading.value = false
  }
}

// End the current login session and clear the user state in the frontend.

async function logout() {
  try {
    await fetch('/api/auth/logout', { method: 'POST' })
  } finally {
    authUser.value = null
    loginPassword.value = ''
    if (activePanel.value === 'details') activePanel.value = 'database'
  }
}

// Load the saved quick locations from the backend.

async function fetchQuickLocations() {
  try {
    const response = await fetch('/api/quick_locations')
    if (!response.ok) throw new Error(`HTTP ${response.status}`)

    customLocations.value = await response.json()
  } catch (error) {
    console.error('Unable to load quick locations:', error)
    alert('Unable to load saved quick locations.')
  }
}

// Send a new named quick location to the backend and refresh the list.

async function addQuickLocation(name) {
  await nextTick()

  const coordinates = mapRef.value?.getCentre()

  if (!coordinates) {
    alert('Unable to retrieve the map centre.')
    return
  }

  try {
    const response = await fetch('/api/quick_locations', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        name: name.trim(),
        latitude: coordinates.latitude,
        longitude: coordinates.longitude
      })
    })

    const result = await response.json()

    if (!response.ok) {
      throw new Error(result.error || `HTTP ${response.status}`)
    }

    customLocations.value.push(result)
  } catch (error) {
    console.error('Unable to save quick location:', error)
    alert(error.message)
  }
}

// Delete a saved quick location by its database ID and refresh the list.

async function removeQuickLocation(id) {
  try {
    const response = await fetch(`/api/quick_locations/${id}`, {
      method: 'DELETE'
    })

    const result = await response.json()

    if (!response.ok || !result.success) {
      throw new Error(result.error || 'Location could not be deleted.')
    }

    customLocations.value = customLocations.value.filter(
      location => location.id !== id
    )
  } catch (error) {
    console.error('Unable to delete quick location:', error)
    alert(error.message)
  }
}

// Move the map to the selected saved location.

function changeLocation(location) {
  settings.latitude = location.latitude
  settings.longitude = location.longitude
}

// Retrieve image records from the backend and store them for the database gallery.

async function fetchImages() {
  try {
    const response = await fetch('/api/images')
    if (!response.ok) throw new Error(`HTTP ${response.status}`)

    const records = await response.json()

    images.value = records.map(image => {
      let imagePath = image.file_url

      if (imagePath?.startsWith('http')) {
        const parsedUrl = new URL(imagePath)

        const isLocal =
          window.location.hostname === 'localhost' ||
          window.location.hostname === '127.0.0.1'

        imagePath = isLocal
          ? `/api${parsedUrl.pathname}`
          : imagePath
      } else if (imagePath) {
        imagePath = `/api${imagePath.startsWith('/') ? '' : '/'}${imagePath}`
      }

      return {
        ...image,
        file_url: imagePath
      }
    })
  } catch (error) {
    console.error('Unable to load image database:', error)
    alert('Unable to load images from the database.')
  }
}

// Load the saved processing and map settings.

async function fetchSettings() {
  try {
    const response = await fetch('/api/settings')
    if (!response.ok) throw new Error(`HTTP ${response.status}`)
    const saved = await response.json()
    Object.assign(settings, {
      water: saved.water, vegetation: saved.vegetation, dirt: saved.dirt,
      noise: saved.noise, parallel: saved.parallel, shadows: saved.shadows,
      rocks: saved.rocks, latitude: saved.lat, longitude: saved.lng
    })
  } catch (error) {
    console.error('Unable to load saved settings:', error)
  }
}

// Load the initial session, saved data and settings when the root component is mounted.
onMounted(async () => {
  await fetchAuthentication()
  await Promise.all([fetchImages(), fetchQuickLocations(), fetchSettings()])
})

// Set the chosen database record as the image currently being viewed.

function selectImage(image) {
  selectedImage.value = image
  activePanel.value = 'details'
}

// Send edits to the selected image record to the backend.

async function saveImage(image) {
  if (!authUser.value) return
  try {
    const response = await fetch('/api/update_image', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        id: image.id,
        title: image.title,
        notes: image.notes
      })
    })

    if (!response.ok) throw new Error(`HTTP ${response.status}`)

    const result = await response.json()
    if (!result.success) throw new Error('Image was not updated.')

    await fetchImages()
    selectedImage.value = images.value.find(item => item.id === image.id) || null
    alert('Image details saved.')
  } catch (error) {
    console.error('Unable to save image:', error)
    alert('Unable to save image details.')
  }
}

// Ask the backend to delete the selected image record.

async function deleteImage() {
  if (!authUser.value) return
  if (!selectedImage.value) return
  if (!window.confirm('Are you sure you want to delete this image?')) return

  try {
    const response = await fetch('/api/delete_image', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ id: selectedImage.value.id })
    })

    if (!response.ok) throw new Error(`HTTP ${response.status}`)

    const result = await response.json()
    if (!result.success) throw new Error('Image was not deleted.')

    selectedImage.value = null
    await fetchImages()
    activePanel.value = 'database'
  } catch (error) {
    console.error('Unable to delete image:', error)
    alert('Unable to delete image.')
  }
}

// Clear the current processing state and return the processor to its initial state.

function resetProcessing() {
  const jobIdToCancel = activeProcessingJobId
  activeProcessingJobId = null

  if (jobIdToCancel) {
    fetch(`/api/process/${jobIdToCancel}`, {
      method: 'DELETE',
      keepalive: true
    }).catch(error => {
      console.error('Unable to cancel processing job:', error)
    })
  }

  if (progressSource) {
    progressSource.close()
    progressSource = null
  }
  processingPercent.value = 0
  processingStage.value = 'Preparing image'
  processingCurrent.value = 0
  processingTotal.value = 0

  if (processingController) {
    processingController.abort()
    processingController = null
  }
  processingImage.value = false

  // Release the processed image from browser memory
  if (processedImage.value?.startsWith('blob:')) {
    URL.revokeObjectURL(processedImage.value)
  }

  // Clear image previews and save feedback
  originalImage.value = null
  processedImage.value = null
  saveStatus.value = ''

  // Reset capture state
  captureMode.value = null
  captureMenuOpen.value = false
  selectedArea.value = null
}

// Switch the main content area back to the image database.

function showDatabase() {
  resetProcessing()
  activePanel.value = 'database'
  fetchImages()
}

// Open or close the settings sidebar.

function toggleSettings() {
  settingsOpen.value = !settingsOpen.value
}

// Leave the processing view and return to the previous application panel.

function backFromProcessor() {
  resetProcessing()
  activePanel.value = 'database'
}

// Upload the processed image and its title/notes to the backend.

async function saveProcessedImage(details) {
  if (!authUser.value) { loginOpen.value = true; return }
  if (!processedImage.value || savingImage.value) return

  savingImage.value = true
  saveStatus.value = 'Saving image...'

  try {
    const imageResponse = await fetch(processedImage.value)
    if (!imageResponse.ok) throw new Error('Could not read processed image.')

    const imageBlob = await imageResponse.blob()
    const formData = new FormData()

    formData.append('image', imageBlob, 'processed-image.jpg')
    formData.append('title', details.title)
    formData.append('notes', details.notes)

    const response = await fetch('/api/upload', {
      method: 'POST',
      body: formData
    })

    const result = await response.json()

    if (!response.ok || !result.success) {
      throw new Error(result.error || `HTTP ${response.status}`)
    }

    await fetchImages()

    saveStatus.value = `Saved successfully! Database record #${result.id}`

  } catch (error) {
    console.error('Image save failed:', error)
    saveStatus.value = `Save failed: ${error.message}`
  } finally {
    savingImage.value = false
  }
}

// Save updated map and image-processing settings.

async function updateSettings(updatedSettings) {
  if (!authUser.value) return
  Object.assign(settings, updatedSettings)
  try {
    const response = await fetch('/api/settings', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ ...settings, lat: settings.latitude, lng: settings.longitude })
    })
    if (!response.ok) throw new Error(`HTTP ${response.status}`)
    const result = await response.json()
    if (!result.success) throw new Error('Settings were not saved.')
  } catch (error) {
    console.error('Unable to save settings:', error)
    window.alert('Settings could not be saved. Check the server and try again.')
  }
}

// Open the menu that lets the user choose a map capture method.

function openCaptureMenu() {
  captureMode.value = null
  captureMenuOpen.value = true
  activePanel.value = 'process'
}

// Set the selected capture method and close or update the capture menu as appropriate.

function chooseCaptureMode(mode) {
  captureMode.value = mode

  if (mode === 'screenshot') {
    captureMapScreenshot()
  }
}

// Retrieve the Leaflet map instance exposed by the map component.

function getLeafletMap() {
  return mapRef.value?.getLeafletMap() ?? null
}

// Receive the geographic bounds emitted by the area selector.

function handleAreaSelected(bounds) {
  selectedArea.value = bounds
}

// Confirm the selected map rectangle before starting the area capture.

function confirmAreaSelection() {
  areaSelectorRef.value?.confirmSelection()
}

// Reposition the selected area relative to the current map view.

function recenterAreaSelection() {
  areaSelectorRef.value?.recenter()
}

// Start the area imagery capture workflow.

function startAreaCapture() {
  areaCaptureLoading.value = true
  areaCapturePercent.value = 0
  areaCaptureStage.value = 'Connecting to satellite imagery'
  areaCaptureCurrent.value = 0
  areaCaptureTotal.value = 0
}

// Update the visible progress state using progress events from the area scan.

function updateAreaCaptureProgress(progress) {
  areaCapturePercent.value = Math.max(0, Math.min(100, progress.percent || 0))
  areaCaptureStage.value = progress.stage || 'Capturing satellite imagery'
  areaCaptureCurrent.value = progress.current || 0
  areaCaptureTotal.value = progress.total || 0
}

// Display an error reported by the area scan component.

function handleAreaScanError(message) {
  areaCaptureLoading.value = false
  window.alert('Area Scan failed: ' + message)
}

// Receive the completed area image and prepare it for processing.

function handleAreaImageReady(imageBlob) {
  areaCaptureLoading.value = false
  if (originalImage.value?.startsWith('blob:')) {
    URL.revokeObjectURL(originalImage.value)
  }

  originalImage.value = URL.createObjectURL(imageBlob)

  if (processedImage.value?.startsWith('blob:')) {
    URL.revokeObjectURL(processedImage.value)
  }

  processedImage.value = null
  saveStatus.value = ''

}

// Request a screenshot of the map through the screenshot capture component.

async function captureMapScreenshot() {
  const mapElement = mapRef.value?.getMapElement()

  if (!mapElement) {
    alert('Unable to access the map.')
    return
  }

  screenshotCaptureRef.value?.captureScreenshot()
}

// Return the map DOM element used by screenshot capture.

function getMapElement() {
  return mapRef.value?.getMapElement() ?? null
}

// Close the capture menu and clear its transient selection state.

function closeCaptureMenu() {
  captureMenuOpen.value = false
}

// Send the selected image and filter settings to the backend processing workflow.

async function processImage(configuration) {
  saveStatus.value = ''
  captureMenuOpen.value = false

  if (!originalImage.value) {
    alert('Please capture a map screenshot first.')
    return
  }

  processingController?.abort()
  progressSource?.close()

  const controller = new AbortController()
  const jobId = crypto.randomUUID()

  processingController = controller
  activeProcessingJobId = jobId
  processingImage.value = true
  processingPercent.value = 0
  processingStage.value = 'Preparing image'
  processingCurrent.value = 0
  processingTotal.value = 0

  const eventSource = new EventSource(`/api/process-progress/${jobId}`)
  progressSource = eventSource

  eventSource.onmessage = event => {
    try {
      const progress = JSON.parse(event.data)

      if (typeof progress.percent === 'number') processingPercent.value = progress.percent
      if (progress.stage) processingStage.value = progress.stage
      if (typeof progress.current === 'number') processingCurrent.value = progress.current
      if (typeof progress.total === 'number') processingTotal.value = progress.total

      if (progress.error) {
        console.error('C++ processing error:', progress.error)
      }
    } catch (error) {
      console.error('Invalid progress update:', error)
    }
  }

  try {
    await new Promise((resolve, reject) => {
      eventSource.onopen = resolve
      eventSource.onerror = () => reject(
        new Error('Unable to connect to processing progress stream.')
      )
    })

    const imageResponse = await fetch(originalImage.value, {
      signal: controller.signal
    })

    if (!imageResponse.ok) {
      throw new Error('Unable to load the original image.')
    }

    const imageBlob = await imageResponse.blob()
    const formData = new FormData()

    formData.append('image', imageBlob, 'screenshot.png')
    formData.append('config', JSON.stringify(configuration))
    formData.append('jobId', jobId)

    const response = await fetch('/api/process', {
      method: 'POST',
      body: formData,
      signal: controller.signal
    })

    if (!response.ok) {
      const error = await response.json().catch(() => ({}))
      throw new Error(error.error || `HTTP ${response.status}`)
    }

    const jobResponse = await response.json()

    if (jobResponse.jobId !== jobId || jobResponse.status !== 'processing') {
      throw new Error('The server returned an unexpected processing response.')
    }

    // Poll for the finished image without keeping one HTTP request open.
    while (!controller.signal.aborted) {
      await new Promise(resolve => setTimeout(resolve, 2000))

      if (controller.signal.aborted) return

      const resultResponse = await fetch(
        `/api/process-result/${jobId}`,
        { signal: controller.signal }
      )

      if (resultResponse.status === 202) {
        continue
      }

      if (!resultResponse.ok) {
        const error = await resultResponse.json().catch(() => ({}))
        throw new Error(error.error || `HTTP ${resultResponse.status}`)
      }

      const resultBlob = await resultResponse.blob()

      if (controller.signal.aborted) return

      if (processedImage.value?.startsWith('blob:')) {
        URL.revokeObjectURL(processedImage.value)
      }

      processedImage.value = URL.createObjectURL(resultBlob)
      processingPercent.value = 100
      processingStage.value = 'Image ready'
      activeProcessingJobId = null

      break
    }

  } catch (error) {
    if (error.name === 'AbortError') {
    } else {
      console.error('Image processing failed:', error)
      alert(`Image processing failed: ${error.message}`)
    }
  } finally {
    if (progressSource === eventSource) {
      eventSource.close()
      progressSource = null
    }

    if (processingController === controller) {
      processingController = null
      processingImage.value = false
    }
  }
}
</script>

<template>
  <!-- Component interface: values come from props; user actions are sent to the parent with emit(). -->
  <div class="sgis-app">
    <AppHeader />

    <div v-if="loginOpen" class="login-overlay" @click.self="loginOpen = false">
      <form class="login-dialog" @submit.prevent="login">
        <button class="login-close" type="button" aria-label="Close login" @click="loginOpen = false">×</button>
        <h2>SGIS Editor Login</h2>
        <p>Login to manage data.</p>
        <label for="login-username">Username</label>
        <input id="login-username" v-model.trim="loginUsername" autocomplete="username" required maxlength="40">
        <label for="login-password">Password</label>
        <input id="login-password" v-model="loginPassword" type="password" autocomplete="current-password" required maxlength="200">
        <p v-if="loginError" class="login-error" role="alert">{{ loginError }}</p>
        <button class="login-submit" type="submit" :disabled="loginLoading">{{ loginLoading ? 'Signing in…' : 'Log in' }}</button>
      </form>
    </div>

    <div class="workspace">

      <SettingsSidebar
        :settings="settings"
        :open="settingsOpen"
        :authenticated="Boolean(authUser)"
        :user="authUser"
        :custom-locations="customLocations"
        @toggle="toggleSettings"
        @close="settingsOpen = false"
        @location="changeLocation"
        @save="updateSettings"
        @add-location="addQuickLocation"
        @remove-location="removeQuickLocation"
        @login="loginOpen = true"
        @logout="logout"
      />

      <main class="main-content">

        <section class="map-section">
          <MapView
            ref="mapRef"
            :latitude="settings.latitude"
            :longitude="settings.longitude"
            :imagery-source="imagerySource"
          />

          <AreaSelector
            ref="areaSelectorRef"
            :map="getLeafletMap()"
            :enabled="captureMode === 'area' && !originalImage"
            :grid-size="areaGridSize"
            @selected="handleAreaSelected"
          />

          <AreaScan
            :selected-area="selectedArea"
            :imagery-source="imagerySource"
            @image-ready="handleAreaImageReady"
            @scan-started="startAreaCapture"
            @scan-progress="updateAreaCaptureProgress"
            @scan-error="handleAreaScanError"
          />

          <div
            v-if="areaCaptureLoading"
            class="area-capture-overlay"
            role="status"
            aria-live="polite"
          >
            <div class="area-capture-panel">
              <div class="area-capture-icon">
                <span class="capture-spinner"></span>
              </div>

              <h2>Capturing satellite imagery</h2>
              <p class="area-capture-stage">{{ areaCaptureStage }}</p>

              <div class="area-capture-progress">
                <div
                  class="area-capture-progress-fill"
                  :style="{ width: areaCapturePercent + '%' }"
                ></div>
              </div>

              <div class="area-capture-details">
                <span>
                  {{ areaCaptureCurrent.toLocaleString() }}
                  <template v-if="areaCaptureTotal">
                    / {{ areaCaptureTotal.toLocaleString() }} tiles
                  </template>
                </span>
                <strong>{{ areaCapturePercent }}%</strong>
              </div>

              <p class="area-capture-hint">
                Please wait while your selected area is downloaded and assembled.
              </p>
            </div>
          </div>

          <ScreenshotCapture
  ref="screenshotCaptureRef"
  :map-element="getMapElement()"
  @captured="originalImage = $event"
  @error="message => window.alert(message)"
/>

          <CaptureMenu
            :open="captureMenuOpen"
            :capture-mode="captureMode"
            :area-grid-size="areaGridSize"
            @close="closeCaptureMenu"
            @select-mode="chooseCaptureMode"
            @area-size-change="areaGridSize = $event"
            @capture-area="confirmAreaSelection"
            @recenter-area="recenterAreaSelection"
          />
        </section>

        <ActionToolbar
          :active-panel="captureMenuOpen ? 'process' : activePanel"
          :imagery-source="imagerySource"
          @update-imagery="imagerySource = $event"
          @show-database="showDatabase"
          @show-processor="openCaptureMenu"
        />

        <section class="content-panel">

          <ImageDatabase
            v-if="activePanel === 'database'"
            :images="images"
            @select="selectImage"
          />

          <ImageProcessor
            v-else-if="activePanel === 'process'"
            :settings="settings"
            :capture-mode="captureMode"
            :original-image="originalImage"
            :processed-image="processedImage"
            :saving="savingImage"
            :processing="processingImage"
          :progress-percent="processingPercent"
          :progress-stage="processingStage"
          :progress-current="processingCurrent"
          :progress-total="processingTotal"

            :save-status="saveStatus"
            :can-save="Boolean(authUser)"
            @process="processImage"
            @save="saveProcessedImage"
            @back="backFromProcessor"
            @reset="resetProcessing"
          />

          <ImageDetails
            v-else-if="activePanel === 'details' && selectedImage"
            :image="selectedImage"
            :can-edit="Boolean(authUser)"
            @save="saveImage"
            @delete="deleteImage"
            @back="activePanel = 'database'"
          />

        </section>
      </main>
    </div>
  </div>
</template>

<style scoped>
.login-overlay { position: fixed; inset: 0; z-index: 5000; display: grid; place-items: center; padding: 20px; background: rgba(0,0,0,.72); }
.login-dialog { position: relative; width: min(390px, 100%); display: flex; flex-direction: column; gap: 10px; padding: 28px; border: 1px solid #f3751a; border-radius: 10px; background: #191919; color: #f5f5f5; box-shadow: 0 18px 55px rgba(0,0,0,.5); }
.login-dialog h2 { margin: 0; color: #f3751a; }
.login-dialog p { margin: 0 0 8px; color: #bbb; font-size: 13px; }
.login-dialog label { font-size: 13px; }
.login-dialog input { box-sizing: border-box; width: 100%; padding: 10px; border: 1px solid #555; border-radius: 5px; background: #292929; color: white; }
.login-submit { margin-top: 8px; padding: 10px; border: 0; border-radius: 5px; background: #f3751a; color: #111; font-weight: bold; cursor: pointer; }
.login-submit:disabled { opacity: .6; cursor: wait; }
.login-close { position: absolute; top: 8px; right: 10px; border: 0; background: transparent; color: white; font-size: 24px; cursor: pointer; }
.login-error { color: #ff8b75 !important; }
.map-section {
  position: relative;
}

.area-capture-overlay {
  position: absolute;
  inset: 0;
  z-index: 2000;
  display: flex;
  align-items: center;
  justify-content: center;
  padding: 24px;
  background: rgba(8, 13, 22, 0.76);
  backdrop-filter: blur(5px);
}

.area-capture-panel {
  width: min(440px, 100%);
  padding: 32px;
  border: 1px solid rgba(243, 117, 26, 0.38);
  border-radius: 12px;
  background: linear-gradient(145deg, #202a35, #151c25);
  color: #f4f7fb;
  box-shadow: 0 18px 55px rgba(0, 0, 0, 0.5), 0 0 24px rgba(243, 117, 26, 0.07);
  text-align: center;
}

.area-capture-icon {
  display: flex;
  justify-content: center;
  margin-bottom: 20px;
}

.capture-spinner {
  width: 42px;
  height: 42px;
  border: 4px solid rgba(255, 255, 255, 0.18);
  border-top-color: #f3751a;
  box-shadow: 0 0 12px rgba(243, 117, 26, 0.18);
  border-radius: 50%;
  animation: capture-spin 0.9s linear infinite;
}

.area-capture-panel h2 {
  margin: 0 0 10px;
  font-size: 21px;
  font-weight: 650;
  letter-spacing: 0.2px;
}

.area-capture-stage {
  min-height: 22px;
  margin: 0 0 24px;
  color: #f39a58;
  font-size: 14px;
  font-weight: 500;
}

.area-capture-progress {
  width: 100%;
  height: 10px;
  overflow: hidden;
  border-radius: 10px;
  background: rgba(255, 255, 255, 0.13);
}

.area-capture-progress-fill {
  height: 100%;
  border-radius: inherit;
  background: linear-gradient(90deg, #e96516, #ff9a45);
  box-shadow: 0 0 12px rgba(243, 117, 26, 0.3);
  transition: width 0.25s ease;
}

.area-capture-details {
  display: flex;
  justify-content: space-between;
  margin-top: 12px;
  color: #aeb8c3;
  font-size: 13px;
  font-variant-numeric: tabular-nums;
}

.area-capture-details strong {
  color: #f39a58;
  font-size: 16px;
  font-weight: 700;
}

.area-capture-hint {
  margin: 22px 0 0;
  color: #8995a2;
  font-size: 12px;
  line-height: 1.5;
}

@keyframes capture-spin {
  to {
    transform: rotate(360deg);
  }
}

@media (max-width: 600px) {
  .area-capture-panel {
    padding: 24px 20px;
  }
}
</style>
