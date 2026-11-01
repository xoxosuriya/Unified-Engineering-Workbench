/**
 * Unified Engineering Workbench - Custom Cursor Engine
 * 58x58px ring cursor with exponential smoothing and state transitions
 * Candidate: RA2311047010144 - PASS Science Works R&D Assessment
 */

window.Cursor = (function() {
  'use strict';

  let cursorEl = null;
  let ringEl = null;
  let dotEl = null;
  let labelEl = null;

  // Pointer positions
  let mx = window.innerWidth / 2;
  let my = window.innerHeight / 2;
  let cx = mx;
  let cy = my;

  let currentState = 'default'; // 'default' | 'spine' | 'btn' | 'open'
  let isFinePointer = window.matchMedia('(pointer: fine)').matches;

  function init() {
    cursorEl = document.querySelector('.custom-cursor');
    if (!cursorEl) return;

    ringEl = cursorEl.querySelector('.cursor-ring');
    dotEl = cursorEl.querySelector('.cursor-dot');
    labelEl = cursorEl.querySelector('.cursor-label');

    window.addEventListener('pointermove', onPointerMove, { passive: true });
    window.addEventListener('pointerdown', onPointerDown, { passive: true });
    window.addEventListener('pointerup', onPointerUp, { passive: true });

    // Track button and clickable element hovers
    document.addEventListener('mouseover', onElementOver, true);
    document.addEventListener('mouseout', onElementOut, true);
  }

  function onPointerMove(e) {
    mx = e.clientX;
    my = e.clientY;
  }

  function onPointerDown() {
    if (currentState === 'default') {
      ringEl.style.transform = 'scale(0.85)';
    }
  }

  function onPointerUp() {
    if (currentState === 'default') {
      ringEl.style.transform = 'scale(1)';
    }
  }

  function onElementOver(e) {
    const target = e.target.closest('button, a, input, select, .box, .shelf-arrow, .group-tab-btn');
    if (target && currentState !== 'open') {
      setState('btn');
    }
  }

  function onElementOut(e) {
    const target = e.target.closest('button, a, input, select, .box, .shelf-arrow, .group-tab-btn');
    if (target && currentState === 'btn') {
      setState('default');
    }
  }

  function setState(state) {
    if (!cursorEl || currentState === state) return;
    currentState = state;

    cursorEl.classList.remove('state-spine', 'state-btn', 'state-open');
    if (state !== 'default') {
      cursorEl.classList.add('state-' + state);
    }
  }

  function update(dt) {
    if (!cursorEl || !isFinePointer) return;

    // Exponential smoothing: 1 - e^(-16 * dt)
    const factor = 1 - Math.exp(-16 * dt);
    cx += (mx - cx) * factor;
    cy += (my - cy) * factor;

    cursorEl.style.transform = `translate3d(${cx}px, ${cy}px, 0)`;
  }

  return {
    init: init,
    update: update,
    setState: setState,
    getPointer: function() {
      return { mx, my, cx, cy };
    },
    getState: function() {
      return currentState;
    }
  };
})();
