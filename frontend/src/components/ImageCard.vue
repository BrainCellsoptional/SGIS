
<script setup>
// Displays one image database record as a selectable thumbnail card.
defineProps({
  image: {
    type: Object,
    required: true
  }
})

const emit = defineEmits(['select'])
</script>

<template>
  <!-- Component interface: values come from props; user actions are sent to the parent with emit(). -->
  <button class="image-card" @click="emit('select', image)">
    <div class="thumbnail">
      <img
        v-if="image.file_url"
        :src="image.file_url"
        :alt="image.title"
      >
      <span v-else>Image unavailable</span>
    </div>

    <div class="image-caption">
      <strong>{{ image.title }}</strong>
      <span>{{ image.notes || 'No notes' }}</span>
    </div>
  </button>
</template>

<style scoped>
.image-card {
  min-width: 0;
  padding: 0;
  text-align: left;
  background: #292929;
  border: 1px solid #777;
  color: white;
  overflow: hidden;
}

.image-card:hover {
  border-color: #f3751a;
}

.thumbnail {
  height: 75px;
  display: grid;
  place-items: center;
  background: #444;
  color: #ccc;
  font-size: 20px;
}

.thumbnail img {
  width: 100%;
  height: 100%;
  object-fit: cover;
}

.image-caption {
  display: flex;
  flex-direction: column;
  gap: 3px;
  padding: 5px;
  font-size: var(--font-body);
}

.image-caption span {
  color: #aaa;
  font-size: var(--font-small);
}
</style>
