<!-- Displays the SGIS application header and logo. -->
<template>
  <!-- Component interface: values come from props; user actions are sent to the parent with emit(). -->
  <header class="topbar">
    <div class="logo">
      <img class="logo-symbol" src="/sgis-logo.png" alt="SGIS mountain logo">
      <span>SGIS</span>
    </div>
  </header>
</template>

<style scoped>
.topbar {
  position: relative; isolation: isolate; height: 55px; background: #111;
  display: flex; align-items: center; justify-content: center; width: 100%; box-sizing: border-box;
}
.topbar::after { content: ""; position: absolute; bottom: 0; left: 0; right: 0; height: 3px; background: #f3751a; z-index: 0; }
.logo {
  position: relative; z-index: 1; display: flex; align-items: center; gap: 10px;
  padding: 4px 14px; background: #fff; color: #111; border-radius: 6px;
  font-size: 26px; font-weight: bold; letter-spacing: 5px;
}
.logo-symbol { display: block; width: 40px; height: 40px; object-fit: contain; border-radius: 4px; }
@media (max-width: 600px) { .logo { font-size: 20px; gap: 5px; padding: 3px 8px; } .logo-symbol { width: 32px; height: 32px; } }
</style>
