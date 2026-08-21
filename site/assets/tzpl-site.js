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

/* ---- ⌘K search modal backed by the Pagefind index built at deploy ---- */

(function () {
  'use strict';
  var btn = document.querySelector('.tzpl-search-btn');
  if (!btn) return;

  var overlay = null, input, chipsEl, resultsEl;
  var pagefind = null, unavailable = false;
  var activeFilter = null, debounceT = 0, hits = [], sel = -1;

  function buildModal() {
    overlay = document.createElement('div');
    overlay.className = 'tzpl-search-overlay';
    overlay.innerHTML =
      '<div class="tzpl-search-box" role="dialog" aria-label="Search documentation">' +
      '<input class="tzpl-search-input" type="search" ' +
      'placeholder="Search the documentation…" autocomplete="off" spellcheck="false">' +
      '<div class="tzpl-search-chips"></div>' +
      '<div class="tzpl-search-results"></div></div>';
    document.body.appendChild(overlay);
    input = overlay.querySelector('.tzpl-search-input');
    chipsEl = overlay.querySelector('.tzpl-search-chips');
    resultsEl = overlay.querySelector('.tzpl-search-results');
    overlay.addEventListener('click', function (e) {
      if (e.target === overlay) close();
    });
    input.addEventListener('input', function () {
      clearTimeout(debounceT);
      debounceT = setTimeout(run, 120);
    });
    input.addEventListener('keydown', function (e) {
      if (e.key === 'ArrowDown') { e.preventDefault(); move(1); }
      else if (e.key === 'ArrowUp') { e.preventDefault(); move(-1); }
      else if (e.key === 'Enter') {
        var target = hits[sel >= 0 ? sel : 0];
        if (target) window.location.href = target.getAttribute('href');
      }
    });
  }

  function close() { if (overlay) overlay.style.display = 'none'; }

  function isOpen() { return overlay && overlay.style.display === 'flex'; }

  async function open() {
    if (!overlay) buildModal();
    overlay.style.display = 'flex';
    input.focus();
    input.select();
    if (!pagefind && !unavailable) {
      try {
        // Resolve against the page URL: import() in a classic script would
        // otherwise resolve relative to this script's assets/ directory.
        pagefind = await import(new URL('pagefind/pagefind.js',
                                        document.baseURI).href);
        pagefind.init();
        var f = await pagefind.filters();
        renderChips(Object.keys((f && f.section) || {}));
      } catch (e) {
        unavailable = true;
        resultsEl.innerHTML = '<p class="tzpl-search-note">The search index ' +
          'is generated when the site is deployed and is not present in ' +
          'this local copy.</p>';
      }
    }
  }

  function renderChips(sections) {
    if (!sections.length) return;
    var names = ['All'].concat(sections);
    chipsEl.innerHTML = names.map(function (n, i) {
      return '<button class="tzpl-chip' + (i === 0 ? ' tzpl-chip-on' : '') +
        '" data-f="' + (i === 0 ? '' : n) + '">' + n + '</button>';
    }).join('');
    chipsEl.addEventListener('click', function (e) {
      var chip = e.target.closest('.tzpl-chip');
      if (!chip) return;
      activeFilter = chip.getAttribute('data-f') || null;
      chipsEl.querySelectorAll('.tzpl-chip').forEach(function (c) {
        c.classList.toggle('tzpl-chip-on', c === chip);
      });
      run();
    });
  }

  function move(dir) {
    if (!hits.length) return;
    sel = (sel + dir + hits.length) % hits.length;
    hits.forEach(function (h, i) { h.classList.toggle('tzpl-sel', i === sel); });
    hits[sel].scrollIntoView({ block: 'nearest' });
  }

  function rel(url) { return url.replace(/^\//, ''); }

  async function run() {
    if (!pagefind || unavailable) return;
    var q = input.value.trim();
    if (!q) { resultsEl.innerHTML = ''; hits = []; sel = -1; return; }
    var opts = activeFilter ? { filters: { section: activeFilter } } : {};
    var res = await pagefind.search(q, opts);
    var top = await Promise.all(res.results.slice(0, 10).map(function (r) {
      return r.data();
    }));
    resultsEl.innerHTML = top.map(function (d) {
      var subs = (d.sub_results || []).filter(function (s) {
        return s.url.indexOf('#') !== -1;
      }).slice(0, 3).map(function (s) {
        return '<a class="tzpl-sr-sub" href="' + rel(s.url) + '">' +
          s.title + '</a>';
      }).join('');
      return '<div class="tzpl-sr-group">' +
        '<a class="tzpl-sr" href="' + rel(d.url) + '">' +
        '<span class="tzpl-sr-section">' + (d.meta.section || '') + '</span>' +
        '<strong>' + (d.meta.title || d.url) + '</strong>' +
        '<p>' + d.excerpt + '</p></a>' + subs + '</div>';
    }).join('') || '<p class="tzpl-search-note">No results for “' +
      q.replace(/</g, '&lt;') + '”.</p>';
    hits = Array.prototype.slice.call(
      resultsEl.querySelectorAll('.tzpl-sr, .tzpl-sr-sub'));
    sel = -1;
  }

  btn.addEventListener('click', open);
  document.addEventListener('keydown', function (e) {
    if ((e.metaKey || e.ctrlKey) && e.key.toLowerCase() === 'k') {
      e.preventDefault();
      if (isOpen()) close(); else open();
    } else if (e.key === 'Escape' && isOpen()) {
      close();
    }
  });
})();
