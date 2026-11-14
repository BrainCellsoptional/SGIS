<script setup>
// Renders the image gallery and forwards the selected image to the parent component.
import { ref, onMounted } from 'vue'
import ImageCard from './ImageCard.vue'

const props = defineProps({
  images: {
    type: Array,
    default: () => []
  }
})

const emit = defineEmits(['select'])
</script>

<template>
  <!-- Component interface: values come from props; user actions are sent to the parent with emit(). -->
  <section class="database">
    <h2>Image Database</h2>

    <p v-if="images.length === 0">No images found in the database.</p>

    <div class="gallery">
      <ImageCard
        v-for="image in images"
        :key="image.id"
        :image="image"
        @select="emit('select', image)"
      />
    </div>
  </section>
</template>

<style scoped>
.database h2 {
  font-size: var(--font-title);
  color: var(--sgis-orange);
}

.gallery {
  display: grid;
  grid-template-columns: repeat(6, minmax(0, 1fr));
  gap: 9px;
  width: 100%;
  padding-right: 3px;
}

@media (max-width: 1100px) {
  .gallery {
    grid-template-columns: repeat(4, minmax(0, 1fr));
  }
}

@media (max-width: 650px) {
  .gallery {
    grid-template-columns: repeat(2, minmax(0, 1fr));
  }
}
</style>
