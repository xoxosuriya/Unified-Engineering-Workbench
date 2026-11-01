/**
 * Unified Engineering Workbench - Master Application Controller
 * Sheet grow animation, ink reveal orchestration, view lifecycle management
 * Candidate: RA2311047010144 - PASS Science Works R&D Assessment
 */

window.App = (function() {
  'use strict';

  let detailDialog = null;
  let detailContent = null;
  let currentViewModule = null;
  let isSheetOpen = false;

  const viewRegistry = {
    'geometry': () => window.GeometryView,
    'ansys': () => window.AnsysView,
    'thermal': () => window.ThermalView,
    'backprop': () => window.BackpropView,
    'performance': () => window.PerformanceView,
    'hardware': () => window.HardwareView
  };

  async function init() {
    detailDialog = document.querySelector('#detail-dialog');
    detailContent = document.querySelector('#detail-content');

    // Close button
    const closeBtn = document.querySelector('#btn-close-detail');
    if (closeBtn) {
      closeBtn.addEventListener('click', () => closeModuleSheet());
    }

    // Intercept native dialog cancel on Escape
    if (detailDialog) {
      detailDialog.addEventListener('cancel', (e) => {
        e.preventDefault();
        closeModuleSheet();
      });
    }

    // Initialize Subsystems
    Cursor.init();
    UI.init();
    Shelf.init();

    // Hook status change
    API.onStatusChange((statusData, isHealthy, isMock) => {
      updateStatusChip(statusData, isHealthy, isMock);
    });

    await API.init();
  }

  function updateStatusChip(statusData, isHealthy, isMock) {
    const chip = document.querySelector('#top-status-chip');
    if (!chip) return;

    if (!isHealthy) {
      chip.className = 'status-chip offline';
      chip.innerHTML = `<span class="chip-dot"></span> <span>BACKEND OFFLINE</span>`;
      return;
    }

    const cpu = statusData?.cpu_model || 'C++17 Engine';
    const threads = statusData?.hardware_concurrency || 8;
    const mockBadge = isMock ? ' (MOCK)' : '';

    chip.className = 'status-chip online';
    chip.innerHTML = `<span class="chip-dot"></span> <span>C++17 ONLINE - ${cpu} - ${threads} threads${mockBadge}</span>`;
  }

  // Reference Spec: Sheet Grow Animation out of active cover face
  function openModuleSheet(moduleId) {
    if (isSheetOpen) return;
    isSheetOpen = true;
    Shelf.setSheetOpen(true);
    Cursor.setState('default');

    const viewFactory = viewRegistry[moduleId];
    if (!viewFactory) {
      isSheetOpen = false;
      Shelf.setSheetOpen(false);
      return;
    }
    const view = viewFactory();
    currentViewModule = view;

    // Read active cover face projected 3D bounding rect
    const coverRect = Shelf.getActiveCoverRect();
    const W = window.innerWidth;
    const H = window.innerHeight;

    // Calculate clamped insets relative to viewport
    const topInset = Math.min(H - 30, Math.max(0, coverRect.top));
    const leftInset = Math.min(W - 30, Math.max(0, coverRect.left));
    const bottomInset = Math.min(H - 30, Math.max(0, H - coverRect.bottom));
    const rightInset = Math.min(W - 30, Math.max(0, W - coverRect.right));

    // Open modelessly so custom cursor at z-index 999999 remains visible on top
    if (!detailDialog.open) {
      try {
        detailDialog.show();
      } catch (e) {
        detailDialog.setAttribute('open', '');
      }
    }
    detailDialog.style.visibility = 'visible';

    // Mount view content into dialog safely
    try {
      if (view && view.mount) {
        view.mount(detailContent);
      }
    } catch (err) {
      console.error('Failed to mount module view:', err);
    }

    // Mount Paper Ink Reveal Overlay on the right column
    try {
      const vizContainer = detailContent.querySelector('.detail-right-col');
      if (vizContainer) {
        UI.createInkReveal(vizContainer);
      }
    } catch (err) {
      console.error('Failed to mount ink reveal:', err);
    }

    const prefersReduced = window.matchMedia('(prefers-reduced-motion: reduce)').matches;
    if (prefersReduced) {
      detailDialog.style.clipPath = 'none';
      detailDialog.style.transform = 'none';
      if (view && view.resize) view.resize();
      return;
    }

    // Cancel any previous animations
    detailDialog.getAnimations().forEach(a => a.cancel());

    // Animate sheet grow: from cover rect to full screen
    const growAnimation = detailDialog.animate([
      {
        clipPath: `inset(${topInset}px ${rightInset}px ${bottomInset}px ${leftInset}px round 3px)`,
        transform: 'rotate(-4.5deg)',
        transformOrigin: `${coverRect.left + coverRect.width / 2}px ${coverRect.top + coverRect.height / 2}px`
      },
      {
        clipPath: 'inset(0px 0px 0px 0px round 0px)',
        transform: 'rotate(0deg)',
        transformOrigin: `${coverRect.left + coverRect.width / 2}px ${coverRect.top + coverRect.height / 2}px`
      }
    ], {
      duration: CONFIG.openDurationMs || 1050,
      easing: 'cubic-bezier(0.76, 0, 0.18, 1)',
      fill: 'both'
    });

    growAnimation.onfinish = () => {
      if (view && view.resize) {
        try { view.resize(); } catch (e) {}
      }
    };

    // Animate text lines sliding up through masks
    const masks = detailContent.querySelectorAll('.detail-mask > *');
    masks.forEach((el, index) => {
      el.animate([
        { transform: 'translateY(110%)' },
        { transform: 'translateY(0%)' }
      ], {
        duration: CONFIG.textSlideDurationMs || 900,
        delay: (CONFIG.openDurationMs ? CONFIG.openDurationMs * 0.6 : 620) + index * 75,
        easing: 'cubic-bezier(0.2, 0.8, 0.2, 1)',
        fill: 'both'
      });
    });
  }

  // Reference Spec: Sheet Close Animation shrinking back to cover rect
  function closeModuleSheet() {
    if (!isSheetOpen) return;

    const coverRect = Shelf.getActiveCoverRect();
    const W = window.innerWidth;
    const H = window.innerHeight;

    const topInset = Math.min(H - 30, Math.max(0, coverRect.top));
    const leftInset = Math.min(W - 30, Math.max(0, coverRect.left));
    const bottomInset = Math.min(H - 30, Math.max(0, H - coverRect.bottom));
    const rightInset = Math.min(W - 30, Math.max(0, W - coverRect.right));

    const prefersReduced = window.matchMedia('(prefers-reduced-motion: reduce)').matches;
    if (prefersReduced) {
      finishClose();
      return;
    }

    detailDialog.getAnimations().forEach(a => a.cancel());

    const shrinkAnimation = detailDialog.animate([
      {
        clipPath: 'inset(0px 0px 0px 0px round 0px)',
        transform: 'rotate(0deg)'
      },
      {
        clipPath: `inset(${topInset}px ${rightInset}px ${bottomInset}px ${leftInset}px round 3px)`,
        transform: 'rotate(-4.5deg)',
        transformOrigin: `${coverRect.left + coverRect.width / 2}px ${coverRect.top + coverRect.height / 2}px`
      }
    ], {
      duration: CONFIG.closeDurationMs || 850,
      easing: 'cubic-bezier(0.76, 0, 0.18, 1)',
      fill: 'both'
    });

    shrinkAnimation.onfinish = finishClose;
  }

  function finishClose() {
    try {
      detailDialog.getAnimations().forEach(a => a.cancel());
      if (detailDialog.open) {
        detailDialog.close();
      }
      detailDialog.style.visibility = 'hidden';

      // Unmount view and release WebGL & Chart instances
      if (currentViewModule && currentViewModule.unmount) {
        try {
          currentViewModule.unmount();
        } catch (e) {
          console.error('Error unmounting view:', e);
        }
      }
      currentViewModule = null;
      detailContent.innerHTML = '';
    } finally {
      isSheetOpen = false;
      Shelf.setSheetOpen(false);
      Cursor.setState('default');
    }
  }

  return {
    init: init,
    openModuleSheet: openModuleSheet,
    closeModuleSheet: closeModuleSheet
  };
})();

// Bootstrap on DOM ready
document.addEventListener('DOMContentLoaded', () => {
  App.init();
});
