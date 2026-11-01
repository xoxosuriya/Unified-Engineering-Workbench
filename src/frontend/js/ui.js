/**
 * Unified Engineering Workbench - Common UI Components
 * Status lines, masked error banners, masked toasts, exit dialog, ink reveal
 * Candidate: RA2311047010144 - PASS Science Works R&D Assessment
 */

window.UI = (function() {
  'use strict';

  let toastContainer = null;
  let exitModal = null;
  let shutdownScreen = null;

  function init() {
    toastContainer = document.querySelector('.toast-container');
    exitModal = document.querySelector('#exit-modal');
    shutdownScreen = document.querySelector('#shutdown-screen');

    // Bind Exit button in top bar
    const exitBtn = document.querySelector('#btn-exit');
    if (exitBtn) {
      exitBtn.addEventListener('click', () => showExitConfirm());
    }

    const cancelExitBtn = document.querySelector('#btn-cancel-exit');
    if (cancelExitBtn) {
      cancelExitBtn.addEventListener('click', () => hideExitConfirm());
    }

    const confirmExitBtn = document.querySelector('#btn-confirm-exit');
    if (confirmExitBtn) {
      confirmExitBtn.addEventListener('click', () => executeShutdown());
    }
  }

  // Toast Notification via Mask Slide Reveal (NO OPACITY FADES)
  function showToast(message, durationMs = 3200) {
    if (!toastContainer) return;

    const mask = document.createElement('div');
    mask.className = 'toast-mask';

    const toast = document.createElement('div');
    toast.className = 'toast-msg';
    toast.textContent = message;

    mask.appendChild(toast);
    toastContainer.appendChild(mask);

    // Slide up through mask
    requestAnimationFrame(() => {
      toast.classList.add('is-visible');
    });

    setTimeout(() => {
      // Slide out through mask
      toast.style.transform = 'translateY(110%)';
      setTimeout(() => {
        mask.remove();
      }, 450);
    }, durationMs);
  }

  // Error Banner via Mask Slide Reveal
  function showErrorBanner(container, errorText, hintText) {
    if (!container) return;
    const banner = container.querySelector('.error-banner');
    if (!banner) return;

    const textEl = banner.querySelector('.error-banner-text');
    const hintEl = banner.querySelector('.error-banner-hint');

    if (textEl) textEl.textContent = errorText || 'An unexpected error occurred.';
    if (hintEl) hintEl.textContent = hintText ? `Hint: ${hintText}` : '';

    banner.classList.add('is-visible');
  }

  function hideErrorBanner(container) {
    if (!container) return;
    const banner = container.querySelector('.error-banner');
    if (banner) {
      banner.classList.remove('is-visible');
    }
  }

  // Status Line Component Helper
  function createStatusLine(parentEl) {
    const el = document.createElement('div');
    el.className = 'status-line';
    el.innerHTML = `<span class="status-indicator">●</span> <span class="status-text">READY</span>`;
    parentEl.appendChild(el);

    let timerId = null;
    let startTime = 0;

    return {
      setIdle: function(msg = 'READY') {
        clearInterval(timerId);
        el.className = 'status-line';
        el.querySelector('.status-text').textContent = msg;
      },
      setRunning: function(label = 'COMPUTING') {
        clearInterval(timerId);
        startTime = Date.now();
        el.className = 'status-line running';
        const updateTimer = () => {
          const sec = ((Date.now() - startTime) / 1000).toFixed(1);
          el.querySelector('.status-text').textContent = `${label} (${sec}s)`;
        };
        updateTimer();
        timerId = setInterval(updateTimer, 100);
      },
      setDone: function(msg = 'COMPLETED') {
        clearInterval(timerId);
        el.className = 'status-line done';
        el.querySelector('.status-text').textContent = msg;
      },
      setError: function(msg = 'FAILED') {
        clearInterval(timerId);
        el.className = 'status-line error';
        el.querySelector('.status-text').textContent = msg;
      }
    };
  }

  // Numeric Validation Helper
  function validateNumericInput(inputEl, options, onChange) {
    const min = options.min ?? -Infinity;
    const max = options.max ?? Infinity;
    const isInteger = options.isInteger ?? false;
    const errorEl = options.errorEl;

    function check() {
      const rawVal = inputEl.value.trim();
      if (rawVal === '') {
        showError('Value is required');
        return false;
      }

      const num = Number(rawVal);
      if (isNaN(num) || !isFinite(num)) {
        showError('Must be a valid finite number');
        return false;
      }

      if (isInteger && !Number.isInteger(num)) {
        showError('Must be a whole integer');
        return false;
      }

      if (num < min || num > max) {
        showError(`Must be between ${min} and ${max}`);
        return false;
      }

      clearError();
      if (onChange) onChange(num);
      return true;
    }

    function showError(msg) {
      inputEl.classList.add('is-invalid');
      if (errorEl) {
        errorEl.textContent = msg;
        errorEl.style.display = 'block';
      }
    }

    function clearError() {
      inputEl.classList.remove('is-invalid');
      if (errorEl) {
        errorEl.textContent = '';
        errorEl.style.display = 'none';
      }
    }

    inputEl.addEventListener('input', check);
    return {
      isValid: check
    };
  }

  // Exit & Shutdown Workflow
  function showExitConfirm() {
    if (exitModal) exitModal.classList.add('is-active');
  }

  function hideExitConfirm() {
    if (exitModal) exitModal.classList.remove('is-active');
  }

  async function executeShutdown() {
    hideExitConfirm();
    try {
      await API.shutdown();
    } catch (e) {
      // Continue to show shutdown screen even if already off
    }
    if (shutdownScreen) {
      shutdownScreen.classList.add('is-active');
    }
  }

  // Ink Reveal Effect Overlay
  // Seven ragged ink blots spreading on paper via feTurbulence & feDisplacementMap
  function createInkReveal(container) {
    const overlay = document.createElement('div');
    overlay.className = 'ink-reveal-overlay';

    // SVG with mask and displacement filter
    const maskId = 'ink-mask-' + Math.random().toString(36).substr(2, 9);
    const filterId = 'ink-filter-' + Math.random().toString(36).substr(2, 9);

    overlay.innerHTML = `
      <svg viewBox="0 0 474 900" width="100%" height="100%" preserveAspectRatio="none">
        <defs>
          <filter id="${filterId}" x="-50%" y="-50%" width="200%" height="200%">
            <feTurbulence type="fractalNoise" baseFrequency="0.011" numOctaves="3" seed="7" result="noise" />
            <feDisplacementMap in="SourceGraphic" in2="noise" scale="120" xChannelSelector="R" yChannelSelector="G" />
          </filter>
          <mask id="${maskId}">
            <!-- Initial black mask hides holes, white holes reveal content -->
            <rect x="-200" y="-200" width="874" height="1300" fill="white" />
            <g filter="url(#${filterId})">
              <!-- Blobs that grow in black so paper overlay becomes transparent where they grow -->
              <circle class="ink-blob" cx="150" cy="330" r="0" fill="black" data-delay="0" />
              <circle class="ink-blob" cx="340" cy="600" r="0" fill="black" data-delay="120" />
              <circle class="ink-blob" cx="250" cy="120" r="0" fill="black" data-delay="220" />
              <circle class="ink-blob" cx="90"  cy="760" r="0" fill="black" data-delay="300" />
              <circle class="ink-blob" cx="400" cy="250" r="0" fill="black" data-delay="360" />
              <circle class="ink-blob" cx="260" cy="860" r="0" fill="black" data-delay="420" />
              <circle class="ink-blob" cx="230" cy="470" r="0" fill="black" data-delay="180" />
            </g>
          </mask>
        </defs>
        <!-- The paper sheet rectangle with the growing ink hole mask -->
        <rect x="0" y="0" width="474" height="900" fill="#EDEBE6" mask="url(#${maskId})" />
      </svg>
    `;

    container.appendChild(overlay);

    // Prefer reduced motion: instant reveal
    const prefersReduced = window.matchMedia('(prefers-reduced-motion: reduce)').matches;
    if (prefersReduced) {
      overlay.remove();
      return;
    }

    const blobs = Array.from(overlay.querySelectorAll('.ink-blob'));
    const startTime = performance.now() + (CONFIG.inkStartDelayMs || 520);
    const duration = CONFIG.inkDurationMs || 1250;
    const maxRadius = 720;

    let animId = null;

    function animate(now) {
      if (now < startTime) {
        animId = requestAnimationFrame(animate);
        return;
      }

      let allDone = true;

      for (const blob of blobs) {
        const delay = parseInt(blob.dataset.delay, 10);
        const elapsed = now - startTime - delay;

        if (elapsed <= 0) {
          allDone = false;
          continue;
        }

        const p = Math.min(1, elapsed / duration);
        // Cubic ease-out: 1 - (1 - p)^3
        const ease = 1 - Math.pow(1 - p, 3);
        const r = ease * maxRadius;
        blob.setAttribute('r', r.toFixed(1));

        if (p < 1) allDone = false;
      }

      if (allDone || (now - startTime > 2400)) {
        // Remove overlay cleanly from DOM so live content is completely interactive!
        overlay.remove();
      } else {
        animId = requestAnimationFrame(animate);
      }
    }

    animId = requestAnimationFrame(animate);

    return {
      cancel: function() {
        if (animId) cancelAnimationFrame(animId);
        overlay.remove();
      }
    };
  }

  return {
    init: init,
    showToast: showToast,
    showErrorBanner: showErrorBanner,
    hideErrorBanner: hideErrorBanner,
    createStatusLine: createStatusLine,
    validateNumericInput: validateNumericInput,
    createInkReveal: createInkReveal,
    showExitConfirm: showExitConfirm,
    hideExitConfirm: hideExitConfirm
  };
})();
