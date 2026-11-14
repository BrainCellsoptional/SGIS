// Application entry point: register the root Vue component and global styles, then mount SGIS.
import { createApp } from 'vue'
import App from './App.vue'
import './assets/main.css'
import 'leaflet/dist/leaflet.css'

createApp(App).mount('#app')