/* The developer details: the switch in the top bar sets data-dev on <html> (the stylesheet
   hides the developer blocks when off), keeps the choice for the next visit and puts it in the
   address, so a link opens with the details shown. The script in the page's head applies it
   before the page draws; without scripts the switch stays hidden and every detail shows. */
(function () {
  'use strict';
  var root = document.documentElement;
  var button = document.querySelector('.dev-switch');
  if (!button) return;
  function show(on) {
    root.dataset.dev = on ? 'on' : 'off';
    button.setAttribute('aria-checked', on ? 'true' : 'false');
  }
  button.addEventListener('click', function () {
    var on = root.dataset.dev !== 'on';
    show(on);
    try { localStorage.setItem('x2-dev', on ? '1' : '0'); } catch (e) { /* this visit only */ }
    var url = new URL(location.href);
    if (on) url.searchParams.set('dev', '1');
    else url.searchParams.delete('dev');
    history.replaceState(null, '', url);
  });
  show(root.dataset.dev === 'on');
  button.hidden = false;
})();

/* The frame-rate comparison: the same synthetic scene in two panes (stars, blocks of scenery
   scrolling down, a ship moving side to side), each redrawn only when its ST would show a new
   picture. VBLs come from the clock, 20 ms each. "orig": the game steps and shows every third
   VBL, the world a line a step. "smooth": a picture every VBL, the world a line a VBL, the ship
   at the same speed as in "orig", drawn where it is between two steps. Nothing of the game is
   drawn: the scene is made up here. */
(function () {
  'use strict';
  var figure = document.getElementById('compare');
  if (!figure) return;
  var canvases = figure.querySelectorAll('canvas');
  var button = figure.querySelector('.play');
  var W = 160, H = 120, VBL_MS = 20;

  // A fixed scene: the same stars and blocks in both panes.
  var seed = 7;
  function rnd() { seed = (seed * 1103515245 + 12345) & 0x7fffffff; return seed / 0x7fffffff; }
  var stars = [];
  for (var i = 0; i < 36; i++) {
    stars.push({ x: Math.floor(rnd() * W), y: rnd() * 4096, k: 0.5 + rnd() * 0.5,
                 c: ['#494949', '#929292', '#dbdbdb'][i % 3] });
  }
  var BLOCK = 12;
  var shades = ['#24246d', '#24496d', '#496d92'];
  function blockAt(row, side) {
    var h = (row * 2654435761 + side * 40503) >>> 0;
    return { w: 1 + (h % 3), c: shades[(h >>> 5) % 3] };
  }

  function draw(ctx, lines, shipX) {
    ctx.fillStyle = '#000000';
    ctx.fillRect(0, 0, W, H);
    for (var i = 0; i < stars.length; i++) {
      var s = stars[i];
      ctx.fillStyle = s.c;
      ctx.fillRect(s.x, Math.floor((s.y + lines * s.k) % H), 1, 1);
    }
    // Scenery: blocks along both edges, scrolling down a line per world line.
    var first = Math.floor(lines / BLOCK);
    for (var r = -1; r <= H / BLOCK + 1; r++) {
      var row = first - r;
      var y = r * BLOCK + (lines % BLOCK);
      for (var side = 0; side < 2; side++) {
        var b = blockAt(row, side);
        var w = b.w * BLOCK;
        var x = side ? W - w : 0;
        ctx.fillStyle = b.c;
        ctx.fillRect(x, y, w, BLOCK);
        ctx.fillStyle = '#6d92b6';
        ctx.fillRect(x, y, w, 1);
        ctx.fillStyle = '#000024';
        ctx.fillRect(x, y + BLOCK - 1, w, 1);
      }
    }
    // The ship: a chevron, its flame.
    var x = Math.round(shipX), y = 92;
    ctx.fillStyle = '#dbdbdb';
    for (var j = 0; j < 9; j++) ctx.fillRect(x - Math.floor(j / 2), y + j, 1 + 2 * Math.floor(j / 2), 1);
    ctx.fillStyle = '#6d6dff';
    ctx.fillRect(x - 1, y + 3, 3, 3);
    ctx.fillStyle = (Math.floor(lines) & 1) ? '#ffb600' : '#ff6d00';
    ctx.fillRect(x - 1, y + 9, 3, 2);
  }

  // The ship's x at a time, at the same speed in both panes: there and back in 3.2 s.
  function shipX(ms) {
    var p = (ms % 3200) / 3200;
    var t = p < 0.5 ? p * 2 : 2 - p * 2;
    return 52 + t * 56;
  }

  var panes = [];
  for (var c = 0; c < canvases.length; c++) {
    panes.push({ ctx: canvases[c].getContext('2d'), mode: canvases[c].dataset.mode, shown: -1 });
  }

  function render(vbl) {
    for (var i = 0; i < panes.length; i++) {
      var p = panes[i];
      var v = p.mode === 'orig' ? vbl - vbl % 3 : vbl;
      if (v === p.shown) continue;
      p.shown = v;
      var lines = p.mode === 'orig' ? v / 3 : v;
      draw(p.ctx, lines, shipX(v * VBL_MS));
    }
  }

  var running = !(window.matchMedia && window.matchMedia('(prefers-reduced-motion: reduce)').matches);
  var visible = true;
  var start = performance.now();
  var held = 0; // the VBL shown when paused
  var looping = false;
  function loop() {
    if (looping) return;
    looping = true;
    requestAnimationFrame(tick);
  }
  function tick(now) {
    if (!running || !visible) { looping = false; return; }
    held = Math.floor((now - start) / VBL_MS);
    render(held);
    requestAnimationFrame(tick);
  }
  function setRunning(on) {
    running = on;
    button.setAttribute('aria-pressed', on ? 'true' : 'false');
    button.textContent = on ? 'Pause' : 'Play';
    if (on) {
      start = performance.now() - held * VBL_MS;
      loop();
    }
  }
  button.addEventListener('click', function () { setRunning(!running); });
  if ('IntersectionObserver' in window) {
    new IntersectionObserver(function (entries) {
      var was = visible;
      visible = entries[0].isIntersecting;
      if (visible && !was && running) setRunning(true);
    }).observe(figure);
  }
  render(0);
  setRunning(running);
})();
