/* TZPL docs site shell behavior: header theme toggle (shared with the
   pages' own `tzpl-docs-theme` localStorage convention), docs-menu
   dismissal, and scroll-spy highlighting for pages that carry their own
   sidebar table of contents. Injected by site/build.py. */

(function () {
  'use strict';

  /* ---- Theme toggle in the header ---- */
  var themeBtn = document.querySelector('.tzpl-theme');

  function syncThemeUI() {
    var isLight = document.documentElement.classList.contains('light');
    if (themeBtn) themeBtn.textContent = isLight ? '☀' : '☾';
    // Keep any in-page toggle icon (family-A sidebars) in sync.
    var pageIcon = document.getElementById('theme-icon');
    if (pageIcon) pageIcon.innerHTML = isLight ? '☀' : '☾';
  }

  if (themeBtn) {
    themeBtn.addEventListener('click', function () {
      document.documentElement.classList.toggle('light');
      localStorage.setItem('tzpl-docs-theme',
        document.documentElement.classList.contains('light') ? 'light' : 'dark');
      syncThemeUI();
    });
  }
  // The page's own toggle also flips the class; observe and stay in sync.
  new MutationObserver(syncThemeUI).observe(document.documentElement,
    { attributes: true, attributeFilter: ['class'] });
  syncThemeUI();

  /* ---- Docs menu: close on outside click or Escape ---- */
  var menu = document.querySelector('.tzpl-menu');
  if (menu) {
    document.addEventListener('click', function (e) {
      if (menu.open && !menu.contains(e.target)) menu.open = false;
    });
    document.addEventListener('keydown', function (e) {
      if (e.key === 'Escape') menu.open = false;
    });
  }

  /* ---- Scroll-spy for pages with their own sidebar TOC ---- */
  var sidebar = document.querySelector('body > nav.sidebar');
  if (!sidebar) return;

  var links = Array.prototype.slice.call(
    sidebar.querySelectorAll('a[href^="#"]'));
  var targets = links
    .map(function (a) {
      var el = document.getElementById(decodeURIComponent(a.hash.slice(1)));
      return el ? { link: a, el: el } : null;
    })
    .filter(Boolean);
  if (!targets.length) return;

  var current = null;
  function spy() {
    // The heading nearest above the reading line (just below the header).
    var line = 0.25 * window.innerHeight;
    var best = null;
    for (var i = 0; i < targets.length; i++) {
      var top = targets[i].el.getBoundingClientRect().top;
      if (top <= line) best = targets[i];
      else break;
    }
    if (best === current) return;
    if (current) current.link.classList.remove('tzpl-spy-current');
    current = best;
    if (current) {
      current.link.classList.add('tzpl-spy-current');
      // Keep the highlighted entry visible in a long sidebar.
      var r = current.link.getBoundingClientRect();
      var s = sidebar.getBoundingClientRect();
      if (r.top < s.top || r.bottom > s.bottom) {
        current.link.scrollIntoView({ block: 'nearest' });
      }
    }
  }

  var ticking = false;
  window.addEventListener('scroll', function () {
    if (ticking) return;
    ticking = true;
    requestAnimationFrame(function () { ticking = false; spy(); });
  }, { passive: true });
  window.addEventListener('resize', spy);
  spy();
})();
