/**
 * Unified Engineering Workbench - 3D VHS Shelf Motion Engine
 * Faithfully matches the Leon Vasari shelf layout, math, easing, and interactions
 * Candidate: RA2311047010144 - PASS Science Works R&D Assessment
 */

window.Shelf = (function() {
  'use strict';

  // Config & Constants
  const NUM_MODULES = 6;
  const COPIES = 7;
  const TOTAL_TAPES = NUM_MODULES * COPIES; // 42 tapes

  // State
  let s = 20.0; // Start on index 2 (Thermal FEA) in middle copy: 2 + 3 * 6 = 20
  let T = 20.0;
  let activeModuleIndex = 2;
  let isDragging = false;
  let dragStartX = 0;
  let dragStartT = 0;
  let wheelTimeout = null;
  let isSheetOpen = false;
  let isQuiet = false;

  // Layout Dimensions
  let H = 340;
  let ws = 57.5;
  let d = 179.1;
  let gap = 10.2;
  let W = window.innerWidth;
  let centerY = window.innerHeight / 2;

  // Tilt & Hover state
  let tiltX = 0;
  let tiltY = 0;
  let targetTiltX = 0;
  let targetTiltY = 0;
  let isOnCover = false;

  // DOM Elements
  let shelfSection = null;
  let shelfStage = null;
  let playRing = null;
  let tapeElements = [];
  let tapeLifts = [];
  let captionTitleEl = null;
  let captionSubEl = null;
  let captionTaskEl = null;
  let groupIndicatorEl = null;
  let groupButtons = [];

  let lastFrameTime = performance.now();
  let rAFId = null;

  function smoothstep(x) {
    const clamped = Math.max(0, Math.min(1, x));
    return clamped * clamped * (3 - 2 * clamped);
  }

  function measureDimensions() {
    W = window.innerWidth;
    centerY = window.innerHeight / 2;

    // Probe H from off-screen element or computed CSS
    const probe = document.createElement('div');
    probe.style.position = 'fixed';
    probe.style.visibility = 'hidden';
    probe.style.height = 'var(--H)';
    document.body.appendChild(probe);
    H = probe.getBoundingClientRect().height || (W > 767 ? Math.min(window.innerHeight * 0.46, W * 0.31) : Math.min(W * 0.60, window.innerHeight * 0.40));
    probe.remove();

    ws = H * (142 / 840);
    d = H * (474 / 900);
    gap = H * 0.03;

    if (shelfStage) {
      shelfStage.style.setProperty('--ws', `${ws.toFixed(2)}px`);
      shelfStage.style.setProperty('--d', `${d.toFixed(2)}px`);
    }
  }

  function buildTapes() {
    if (!shelfStage) return;
    shelfStage.innerHTML = '';
    tapeElements = [];
    tapeLifts = [];

    for (let v = 0; v < TOTAL_TAPES; v++) {
      const moduleIdx = v % NUM_MODULES;
      const mod = MODULES[moduleIdx];

      const tapeEl = document.createElement('div');
      tapeEl.className = 'tape';
      tapeEl.dataset.v = v;
      tapeEl.dataset.module = mod.id;

      // Spine face
      const spFace = document.createElement('div');
      spFace.className = 'tape-face sp';
      spFace.innerHTML = Art.renderSpineSvg(mod);

      // Cover face
      const cvFace = document.createElement('div');
      cvFace.className = 'tape-face cv';
      cvFace.innerHTML = Art.renderCoverSvg(mod);

      tapeEl.appendChild(spFace);
      tapeEl.appendChild(cvFace);
      shelfStage.appendChild(tapeEl);

      tapeElements.push(tapeEl);
      tapeLifts.push(0);

      // Hover events for spine lift
      tapeEl.addEventListener('pointerenter', () => {
        const r = normalizeRelative(v - s);
        if (Math.abs(r) > 0.5 && !isSheetOpen) {
          Cursor.setState('spine');
        }
      });

      tapeEl.addEventListener('pointerleave', () => {
        if (!isSheetOpen && Cursor.getState() === 'spine') {
          Cursor.setState('default');
        }
      });

      tapeEl.addEventListener('click', (e) => {
        if (isDragging || isSheetOpen) return;
        const r = normalizeRelative(v - s);
        if (Math.abs(r) < 0.35) {
          // Open active detail sheet when the book in center is clicked
          App.openModuleSheet(mod.id);
        } else {
          // Animate/glide to that book so it stays in center
          const targetDiff = Math.round(r);
          T = Math.round(T + targetDiff);
          const targetMod = ((Math.round(T) % NUM_MODULES) + NUM_MODULES) % NUM_MODULES;
          updateActiveCaption(targetMod, Math.sign(targetDiff) || 1);
        }
      });
    }
  }

  function normalizeRelative(diff) {
    // Wrap into [-TOTAL_TAPES/2, +TOTAL_TAPES/2] = [-9, +9]
    return ((diff % TOTAL_TAPES) + TOTAL_TAPES * 1.5) % TOTAL_TAPES - TOTAL_TAPES * 0.5;
  }

  // Smooth-step curve B(r)
  function calcB(r) {
    if (Math.abs(r) < 1) {
      return (r + Math.sin(Math.PI * r) / Math.PI) / 2.0;
    }
    return Math.sign(r) * 0.5;
  }

  // Horizontal position F(r)
  function calcX(r) {
    return r * (ws + gap) + (d - ws + 1.6 * gap) * calcB(r);
  }

  function updateActiveCaption(newIndex, dir = 1) {
    if (newIndex === activeModuleIndex) return;
    activeModuleIndex = newIndex;
    const mod = MODULES[newIndex];
    if (!mod) return;

    // Update group indicator
    updateGroupTabs(mod.group);

    // Swap caption lines through masks cleanly
    swapCaptionLine(captionTitleEl, mod.title, dir, 'caption-title');
    
    const subText = `${mod.label} / ${mod.group}`;
    swapCaptionLine(captionSubEl, subText, dir, 'caption-sub');
    swapCaptionLine(captionTaskEl, mod.taskDesc, dir, 'caption-task');
  }

  function swapCaptionLine(maskContainer, text, dir, extraClass) {
    if (!maskContainer) return;

    // Immediately remove any previously leaving spans so they never stack up
    maskContainer.querySelectorAll('.caption-line.is-leaving').forEach(el => el.remove());

    const oldSpan = maskContainer.querySelector('.caption-line:not(.is-leaving)');
    if (oldSpan && oldSpan.textContent === text) {
      return;
    }

    const newSpan = document.createElement('span');
    newSpan.className = `caption-line ${extraClass}`;
    newSpan.textContent = text;
    maskContainer.appendChild(newSpan);

    const prefersReduced = window.matchMedia('(prefers-reduced-motion: reduce)').matches;
    if (prefersReduced) {
      if (oldSpan) oldSpan.remove();
      return;
    }

    const ease = 'cubic-bezier(0.2, 0.8, 0.2, 1)';
    const duration = 520;

    // Slide new line in vertically within the centered grid cell
    newSpan.animate([
      { transform: `translateY(${dir * 115}%)` },
      { transform: 'translateY(0%)' }
    ], {
      duration: duration,
      easing: ease,
      fill: 'both'
    });

    if (oldSpan) {
      oldSpan.classList.add('is-leaving');
      const oldAnim = oldSpan.animate([
        { transform: 'translateY(0%)' },
        { transform: `translateY(${-dir * 115}%)` }
      ], {
        duration: duration,
        easing: ease,
        fill: 'both'
      });
      oldAnim.onfinish = () => oldSpan.remove();
    }
  }

  function updateGroupTabs(activeGroupId) {
    groupButtons.forEach(btn => {
      const isAct = btn.dataset.group === activeGroupId;
      btn.classList.toggle('is-active', isAct);
      if (isAct && groupIndicatorEl) {
        groupIndicatorEl.style.width = `${btn.offsetWidth}px`;
        groupIndicatorEl.style.transform = `translateX(${btn.offsetLeft}px)`;
      }
    });
  }

  function initGroupTabs() {
    const nav = document.querySelector('.group-tabs-nav');
    if (!nav) return;

    groupIndicatorEl = nav.querySelector('.group-tab-indicator');
    groupButtons = Array.from(nav.querySelectorAll('.group-tab-btn'));

    groupButtons.forEach(btn => {
      btn.addEventListener('click', () => {
        const grp = btn.dataset.group;
        if (grp === 'ALL') {
          jumpToModule(0);
        } else {
          const targetMod = MODULES.find(m => m.group === grp);
          if (targetMod) {
            jumpToModule(targetMod.index);
          }
        }
      });
    });

    // Set initial position
    setTimeout(() => {
      updateGroupTabs(MODULES[activeModuleIndex].group);
    }, 100);
  }

  function jumpToModule(modIndex) {
    const curMod = ((Math.round(s) % NUM_MODULES) + NUM_MODULES) % NUM_MODULES;
    let diff = modIndex - curMod;
    if (diff > NUM_MODULES / 2) diff -= NUM_MODULES;
    if (diff < -NUM_MODULES / 2) diff += NUM_MODULES;
    T = Math.round(T + diff);
    const targetMod = ((Math.round(T) % NUM_MODULES) + NUM_MODULES) % NUM_MODULES;
    updateActiveCaption(targetMod, Math.sign(diff) || 1);
  }

  function initInputs() {
    // Shelf arrows
    const prevBtn = document.querySelector('#shelf-arrow-prev');
    const nextBtn = document.querySelector('#shelf-arrow-next');

    if (prevBtn) {
      prevBtn.addEventListener('click', () => {
        T = Math.round(T) - 1;
        const targetMod = ((Math.round(T) % NUM_MODULES) + NUM_MODULES) % NUM_MODULES;
        updateActiveCaption(targetMod, -1);
      });
    }
    if (nextBtn) {
      nextBtn.addEventListener('click', () => {
        T = Math.round(T) + 1;
        const targetMod = ((Math.round(T) % NUM_MODULES) + NUM_MODULES) % NUM_MODULES;
        updateActiveCaption(targetMod, 1);
      });
    }

    // Play ring
    if (playRing) {
      playRing.addEventListener('click', () => {
        const mod = MODULES[activeModuleIndex];
        if (mod) App.openModuleSheet(mod.id);
      });
    }

    // Direct click on cover area of shelf
    if (shelfSection) {
      shelfSection.addEventListener('click', (e) => {
        if (isDragging || isSheetOpen) return;
        if (isOnCover) {
          const mod = MODULES[activeModuleIndex];
          if (mod) {
            App.openModuleSheet(mod.id);
          }
        }
      });
    }

    // Keyboard navigation
    window.addEventListener('keydown', (e) => {
      if (isSheetOpen) {
        if (e.key === 'Escape') {
          App.closeModuleSheet();
        }
        return;
      }

      if (e.key === 'ArrowLeft') {
        T = Math.round(T) - 1;
        const targetMod = ((Math.round(T) % NUM_MODULES) + NUM_MODULES) % NUM_MODULES;
        updateActiveCaption(targetMod, -1);
      } else if (e.key === 'ArrowRight') {
        T = Math.round(T) + 1;
        const targetMod = ((Math.round(T) % NUM_MODULES) + NUM_MODULES) % NUM_MODULES;
        updateActiveCaption(targetMod, 1);
      } else if (e.key === 'Enter') {
        const mod = MODULES[activeModuleIndex];
        if (mod) App.openModuleSheet(mod.id);
      }
    });

    // Wheel scrub
    window.addEventListener('wheel', (e) => {
      if (isSheetOpen) return;
      const delta = Math.abs(e.deltaX) > Math.abs(e.deltaY) ? e.deltaX : e.deltaY;
      T += (delta / (ws + gap)) * 0.5;

      clearTimeout(wheelTimeout);
      wheelTimeout = setTimeout(() => {
        T = Math.round(T);
        const targetMod = ((Math.round(T) % NUM_MODULES) + NUM_MODULES) % NUM_MODULES;
        updateActiveCaption(targetMod, Math.sign(delta) || 1);
      }, 160);
    }, { passive: true });

    // Drag scrub
    if (shelfSection) {
      let isPointerDown = false;
      let startX = 0;

      shelfSection.addEventListener('pointerdown', (e) => {
        if (isSheetOpen || e.button !== 0) return;
        isPointerDown = true;
        startX = e.clientX;
        dragStartX = e.clientX;
        dragStartT = T;
      });

      window.addEventListener('pointermove', (e) => {
        if (!isPointerDown) return;
        const dx = e.clientX - startX;
        if (!isDragging && Math.abs(dx) > 5) {
          isDragging = true;
          pendingOpenModuleId = null;
        }
        if (isDragging) {
          T = dragStartT - (e.clientX - dragStartX) / (ws + gap);
        }
      });

      window.addEventListener('pointerup', () => {
        if (isPointerDown) {
          isPointerDown = false;
          if (isDragging) {
            T = Math.round(T);
            setTimeout(() => { isDragging = false; }, 50);
          }
        }
      });
    }

    // Resize handler
    window.addEventListener('resize', () => {
      measureDimensions();
    });
  }

  function frame(now) {
    rAFId = requestAnimationFrame(frame);

    const rawDt = (now - lastFrameTime) / 1000.0;
    lastFrameTime = now;
    const dt = Math.min(0.05, Math.max(0.001, rawDt));

    // When sheet is open or quiet benchmark mode, pause shelf loop heavy DOM work
    if (isSheetOpen || isQuiet) {
      isOnCover = false;
      Cursor.update(dt);
      return;
    }

    const prefersReduced = window.matchMedia('(prefers-reduced-motion: reduce)').matches;

    // Spring interpolation for s -> T
    const k = isDragging ? 18.0 : 5.2;
    if (prefersReduced) {
      s = T;
    } else {
      const springFactor = 1 - Math.exp(-k * dt);
      s += (T - s) * springFactor;
      if (Math.abs(T - s) < 1e-4) {
        s = T;
      }
    }

    // Loop normalisation (keep s and T inside reasonable bounds)
    if (s > TOTAL_TAPES * 2) {
      s -= TOTAL_TAPES;
      T -= TOTAL_TAPES;
    } else if (s < -TOTAL_TAPES * 2) {
      s += TOTAL_TAPES;
      T += TOTAL_TAPES;
    }

    // Determine active tape module (update caption during drag or when settled)
    if (isDragging) {
      const currentActiveMod = (Math.round(s) % NUM_MODULES + NUM_MODULES) % NUM_MODULES;
      const dir = Math.sign(T - s) || 1;
      updateActiveCaption(currentActiveMod, dir);
    } else if (Math.abs(T - s) < 0.08) {
      const settledMod = (Math.round(s) % NUM_MODULES + NUM_MODULES) % NUM_MODULES;
      updateActiveCaption(settledMod, 1);
    }

    // Pointer & Tilt calculations
    const pointer = Cursor.getPointer();
    const mx = pointer.mx;
    const my = pointer.my;

    // On-cover test: within 0.55*d horizontally, 0.55*H vertically, shelf settled |T - s| < 0.3
    const isSettled = Math.abs(T - s) < 0.3;
    const dxCover = Math.abs(mx - W / 2);
    const dyCover = Math.abs(my - centerY);
    isOnCover = !isSheetOpen && isSettled && (dxCover < 0.55 * d) && (dyCover < 0.55 * H);

    if (isOnCover) {
      targetTiltY = ((mx - W / 2) / (0.55 * d)) * 9.0;
      targetTiltX = -((my - centerY) / (H / 2)) * 6.0;
      Cursor.setState('open');
    } else {
      targetTiltY = ((mx - W / 2) / W) * 5.0;
      targetTiltX = 0;
      if (Cursor.getState() === 'open') {
        Cursor.setState('default');
      }
    }

    // Smooth tilt angles
    const tiltEase = 1 - Math.exp(-7.0 * dt);
    tiltX += (targetTiltX - tiltX) * tiltEase;
    tiltY += (targetTiltY - tiltY) * tiltEase;

    // Update each tape
    const cullingThreshold = W / 2 + 1.5 * d;

    for (let v = 0; v < TOTAL_TAPES; v++) {
      const tapeEl = tapeElements[v];
      if (!tapeEl) continue;

      const r = normalizeRelative(v - s);
      const x = calcX(r);

      // Culling
      if (Math.abs(x) > cullingThreshold) {
        tapeEl.style.visibility = 'hidden';
        continue;
      }
      tapeEl.style.visibility = 'visible';

      // Openness
      const o = smoothstep(Math.max(0, 1 - Math.abs(r)));

      // Hover lift
      const isHovered = (Cursor.getState() === 'spine') && (Math.abs(normalizeRelative(v - Math.round(s))) < 0.5);
      const liftTarget = (isHovered && Math.abs(r) > 0.5) ? 1.0 : 0.0;
      tapeLifts[v] += (liftTarget - tapeLifts[v]) * (1 - Math.exp(-12.0 * dt));
      const lift = tapeLifts[v];

      // 3D Transforms
      const rotY = -90.0 * o + tiltY * o;
      const rotX = tiltX * o;
      const rotZ = -4.5 * o;
      const tz = o * 0.2 * H;
      const ty = -o * 0.025 * H - lift * 0.045 * H;

      tapeEl.style.transform = `translate3d(${x.toFixed(2)}px, ${ty.toFixed(2)}px, ${tz.toFixed(2)}px) rotateZ(${rotZ.toFixed(2)}deg) rotateX(${rotX.toFixed(2)}deg) rotateY(${rotY.toFixed(2)}deg)`;
      tapeEl.style.zIndex = Math.round(100 - Math.abs(r) * 4);

      // Dynamic Shading: Spine face o * 0.55, Cover face (1 - o) * 0.5
      const spFace = tapeEl.children[0];
      const cvFace = tapeEl.children[1];
      if (spFace) spFace.style.setProperty('--shade', (o * 0.55).toFixed(3));
      if (cvFace) cvFace.style.setProperty('--shade', ((1 - o) * 0.5).toFixed(3));
    }

    // Play Ring positioning & scale
    if (playRing) {
      const playLeft = W / 2 - 0.5 * d - 6;
      const playTop = centerY + 0.36 * H;
      const playScale = smoothstep(Math.max(0, 1 - Math.abs(T - s) * 3));

      playRing.style.left = `${playLeft.toFixed(1)}px`;
      playRing.style.top = `${playTop.toFixed(1)}px`;
      playRing.style.transform = `translate(-50%, -50%) scale(${playScale.toFixed(3)})`;
    }

    // Cursor position update
    Cursor.update(dt);
  }

  return {
    init: function() {
      shelfSection = document.querySelector('.shelf-section');
      shelfStage = document.querySelector('.shelf-stage');
      playRing = document.querySelector('#play-ring');
      captionTitleEl = document.querySelector('#caption-title-mask');
      captionSubEl = document.querySelector('#caption-sub-mask');
      captionTaskEl = document.querySelector('#caption-task-mask');

      measureDimensions();
      buildTapes();
      initGroupTabs();
      initInputs();

      // Set initial caption in pure English
      const initMod = MODULES[CONFIG.initialModuleIndex || 2];
      captionTitleEl.querySelector('.caption-title').textContent = initMod.title;
      captionSubEl.querySelector('.caption-sub').textContent = `${initMod.label} / ${initMod.group}`;
      captionTaskEl.querySelector('.caption-task').textContent = initMod.taskDesc;

      lastFrameTime = performance.now();
      rAFId = requestAnimationFrame(frame);
    },

    getActiveModule: function() {
      return MODULES[activeModuleIndex];
    },

    getActiveCoverRect: function() {
      // Find the currently active tape cover face DOM rect
      const activeTape = tapeElements.find(el => {
        const v = parseInt(el.dataset.v, 10);
        return Math.abs(normalizeRelative(v - s)) < 0.5;
      });

      if (activeTape && activeTape.children[1]) {
        return activeTape.children[1].getBoundingClientRect();
      }

      // Fallback
      return {
        left: W / 2 - d / 2,
        top: centerY - H / 2,
        width: d,
        height: H,
        right: W / 2 + d / 2,
        bottom: centerY + H / 2
      };
    },

    setSheetOpen: function(open) {
      isSheetOpen = open;
      isOnCover = false;
      if (open && playRing) {
        playRing.style.transform = 'translate(-50%, -50%) scale(0)';
      }
    },

    setQuiet: function(quiet) {
      isQuiet = quiet;
      if (quiet) {
        document.body.dataset.quiet = '1';
      } else {
        delete document.body.dataset.quiet;
      }
    }
  };
})();
