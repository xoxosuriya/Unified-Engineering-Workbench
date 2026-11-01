/**
 * Unified Engineering Workbench - Procedural Tape Artwork
 * Generates screen-print duotone SVG art for Spines (142x840) and Covers (474x900)
 * Palette: Ink #141414, Cobalt #1F3FB0, Vermilion #E0452B, Cream #E8E1D0
 * Candidate: RA2311047010144 - PASS Science Works R&D Assessment
 */

window.Art = (function() {
  'use strict';

  // Screen-print motifs generator
  function renderMotif(motifType, isCover) {
    const scale = isCover ? 1.0 : 0.45;
    
    switch (motifType) {
      case 'geometry':
        // Isometric L-bracket with triangular reinforcing rib
        return `
          <g transform="${isCover ? 'translate(237, 490) scale(1.6)' : 'translate(71, 105) scale(0.65)'}">
            <!-- Base footprint -->
            <polygon points="-80,30 0,70 80,30 0,-10" fill="#E8E1D0" stroke="#141414" stroke-width="2.5" />
            <!-- Base front faces -->
            <polygon points="-80,30 0,70 0,85 -80,45" fill="#1F3FB0" stroke="#141414" stroke-width="2.5" />
            <polygon points="0,70 80,30 80,45 0,85" fill="#141414" stroke="#141414" stroke-width="2.5" />
            <!-- Vertical upright -->
            <polygon points="-80,30 -80,-50 -50,-65 -50,15" fill="#1F3FB0" stroke="#141414" stroke-width="2.5" />
            <polygon points="-50,-65 0,-40 0,40 -50,15" fill="#E8E1D0" stroke="#141414" stroke-width="2.5" />
            <polygon points="-80,-50 -50,-65 0,-40 -30,-25" fill="#E0452B" stroke="#141414" stroke-width="2" />
            <!-- Triangular Gusset / Rib -->
            <polygon points="-50,15 0,40 -50,-25" fill="#E0452B" stroke="#141414" stroke-width="2" stroke-linejoin="round" />
            <line x1="-50" y1="-25" x2="-40" y2="-20" stroke="#141414" stroke-width="2" />
            <!-- Dimension lines / CAD hatch -->
            <line x1="-90" y1="30" x2="-90" y2="-50" stroke="#141414" stroke-width="1.2" stroke-dasharray="3,3" />
            <line x1="-95" y1="30" x2="-85" y2="30" stroke="#141414" stroke-width="1.5" />
            <line x1="-95" y1="-50" x2="-85" y2="-50" stroke="#141414" stroke-width="1.5" />
          </g>
        `;

      case 'ansys':
        // Grid of dots graded cobalt to vermilion
        let dots = '';
        const rows = isCover ? 9 : 5;
        const cols = isCover ? 11 : 5;
        const spacingX = isCover ? 28 : 16;
        const spacingY = isCover ? 24 : 14;
        const originX = isCover ? 237 - (cols * spacingX) / 2 + 10 : 71 - (cols * spacingX) / 2 + 8;
        const originY = isCover ? 480 - (rows * spacingY) / 2 : 105 - (rows * spacingY) / 2;

        for (let r = 0; r < rows; r++) {
          for (let c = 0; c < cols; c++) {
            const tNorm = (c + r * 0.4) / (cols + rows * 0.4);
            const color = tNorm > 0.65 ? '#E0452B' : (tNorm > 0.35 ? '#141414' : '#1F3FB0');
            const rad = isCover ? (3.5 + 2 * tNorm) : (1.8 + 1.2 * tNorm);
            const cx = originX + c * spacingX;
            const cy = originY + r * spacingY;
            dots += `<circle cx="${cx}" cy="${cy}" r="${rad}" fill="${color}" stroke="#141414" stroke-width="0.8" />`;
            if (c > 0 && r % 2 === 0) {
              dots += `<line x1="${cx - spacingX}" y1="${cy}" x2="${cx}" y2="${cy}" stroke="#141414" stroke-width="0.5" stroke-dasharray="2,2" opacity="0.4" />`;
            }
          }
        }
        return `<g>${dots}</g>`;

      case 'thermal':
        // A rod above a parabola with 4 mesh ticks
        if (isCover) {
          return `
            <g transform="translate(237, 480)">
              <!-- Background temperature isotherm field -->
              <rect x="-160" y="-120" width="320" height="240" fill="#E8E1D0" stroke="#141414" stroke-width="1.5" />
              <!-- Rod Element -->
              <rect x="-140" y="-80" width="280" height="28" fill="#1F3FB0" stroke="#141414" stroke-width="2.5" />
              <!-- Mesh Ticks (4 elements = 5 nodes) -->
              <line x1="-140" y1="-90" x2="-140" y2="-40" stroke="#141414" stroke-width="2.5" />
              <line x1="-70" y1="-90" x2="-70" y2="-40" stroke="#141414" stroke-width="2" />
              <line x1="0" y1="-90" x2="0" y2="-40" stroke="#141414" stroke-width="2" />
              <line x1="70" y1="-90" x2="70" y2="-40" stroke="#141414" stroke-width="2" />
              <line x1="140" y1="-90" x2="140" y2="-40" stroke="#141414" stroke-width="2.5" />
              <!-- Heat generation flux vectors q -->
              <path d="M-105,-110 v14 M-35,-110 v14 M35,-110 v14 M105,-110 v14" stroke="#E0452B" stroke-width="2" />
              <polygon points="-108,-96 -105,-91 -102,-96" fill="#E0452B" />
              <polygon points="-38,-96 -35,-91 -32,-96" fill="#E0452B" />
              <polygon points="32,-96 35,-91 38,-96" fill="#E0452B" />
              <polygon points="102,-96 105,-91 108,-96" fill="#E0452B" />
              <!-- Analytical Parabola T(x) Curve -->
              <path d="M-140,50 Q0,-45 140,75" fill="none" stroke="#E0452B" stroke-width="3.5" />
              <!-- Piecewise linear FEA approximation -->
              <polyline points="-140,50 -70,-8 0,-25 70,18 140,75" fill="none" stroke="#141414" stroke-width="2" stroke-dasharray="4,3" />
              <!-- Midpoint error annotation line -->
              <line x1="0" y1="-25" x2="0" y2="-45" stroke="#1F3FB0" stroke-width="2" />
              <circle cx="0" cy="-35" r="3" fill="#1F3FB0" />
            </g>
          `;
        } else {
          return `
            <g transform="translate(71, 105)">
              <rect x="-50" y="-35" width="100" height="14" fill="#1F3FB0" stroke="#141414" stroke-width="1.5" />
              <line x1="-50" y1="-40" x2="-50" y2="-15" stroke="#141414" stroke-width="1.5" />
              <line x1="-16" y1="-40" x2="-16" y2="-15" stroke="#141414" stroke-width="1.2" />
              <line x1="16" y1="-40" x2="16" y2="-15" stroke="#141414" stroke-width="1.2" />
              <line x1="50" y1="-40" x2="50" y2="-15" stroke="#141414" stroke-width="1.5" />
              <path d="M-50,20 Q0,-15 50,30" fill="none" stroke="#E0452B" stroke-width="2.5" />
              <polyline points="-50,20 -16,-2 16,5 50,30" fill="none" stroke="#141414" stroke-width="1.2" stroke-dasharray="2,2" />
            </g>
          `;
        }

      case 'backprop':
        // 4 nodes in a graph with an arrow backward
        if (isCover) {
          return `
            <g transform="translate(237, 480)">
              <!-- Graph links forward -->
              <line x1="-120" y1="0" x2="-40" y2="-50" stroke="#141414" stroke-width="2" />
              <line x1="-120" y1="0" x2="-40" y2="50" stroke="#141414" stroke-width="2" />
              <line x1="-40" y1="-50" x2="60" y2="0" stroke="#141414" stroke-width="2" />
              <line x1="-40" y1="50" x2="60" y2="0" stroke="#141414" stroke-width="2" />
              <line x1="60" y1="0" x2="130" y2="0" stroke="#141414" stroke-width="2" />
              <!-- Backward gradient flow arrow (curved vermilion) -->
              <path d="M 120,40 C 40,90 -20,90 -110,35" fill="none" stroke="#E0452B" stroke-width="3" stroke-dasharray="6,4" />
              <polygon points="-118,30 -105,42 -100,30" fill="#E0452B" />
              <!-- Nodes -->
              <!-- Input node x -->
              <circle cx="-120" cy="0" r="18" fill="#E8E1D0" stroke="#141414" stroke-width="2.5" />
              <text x="-120" y="5" font-family="'Inter Tight', sans-serif" font-weight="700" font-size="12" text-anchor="middle" fill="#141414">x</text>
              <!-- Hidden node h1 -->
              <circle cx="-40" cy="-50" r="18" fill="#1F3FB0" stroke="#141414" stroke-width="2.5" />
              <text x="-40" y="-45" font-family="'Inter Tight', sans-serif" font-weight="700" font-size="12" text-anchor="middle" fill="#FFFFFF">h₁</text>
              <!-- Hidden node h2 -->
              <circle cx="-40" cy="50" r="18" fill="#1F3FB0" stroke="#141414" stroke-width="2.5" />
              <text x="-40" y="55" font-family="'Inter Tight', sans-serif" font-weight="700" font-size="12" text-anchor="middle" fill="#FFFFFF">h₂</text>
              <!-- Output node y_hat -->
              <circle cx="60" cy="0" r="18" fill="#E0452B" stroke="#141414" stroke-width="2.5" />
              <text x="60" y="4" font-family="'Inter Tight', sans-serif" font-weight="700" font-size="11" text-anchor="middle" fill="#FFFFFF">ŷ</text>
              <!-- Loss box -->
              <rect x="110" y="-16" width="34" height="32" fill="#141414" />
              <text x="127" y="5" font-family="'Inter Tight', sans-serif" font-weight="700" font-size="12" text-anchor="middle" fill="#FFFFFF">J</text>
            </g>
          `;
        } else {
          return `
            <g transform="translate(71, 105)">
              <line x1="-40" y1="0" x2="-10" y2="-22" stroke="#141414" stroke-width="1.2" />
              <line x1="-40" y1="0" x2="-10" y2="22" stroke="#141414" stroke-width="1.2" />
              <line x1="-10" y1="-22" x2="25" y2="0" stroke="#141414" stroke-width="1.2" />
              <line x1="-10" y1="22" x2="25" y2="0" stroke="#141414" stroke-width="1.2" />
              <path d="M 30,16 C 5,35 -20,35 -38,12" fill="none" stroke="#E0452B" stroke-width="2" stroke-dasharray="3,2" />
              <circle cx="-40" cy="0" r="10" fill="#E8E1D0" stroke="#141414" stroke-width="1.5" />
              <circle cx="-10" cy="-22" r="10" fill="#1F3FB0" stroke="#141414" stroke-width="1.5" />
              <circle cx="-10" cy="22" r="10" fill="#1F3FB0" stroke="#141414" stroke-width="1.5" />
              <circle cx="25" cy="0" r="10" fill="#E0452B" stroke="#141414" stroke-width="1.5" />
            </g>
          `;
        }

      case 'performance':
        // Parallel horizontal lanes of unequal length
        if (isCover) {
          return `
            <g transform="translate(237, 480)">
              <!-- Frame container -->
              <rect x="-160" y="-100" width="320" height="200" fill="#E8E1D0" stroke="#141414" stroke-width="2" />
              <!-- Lane markings -->
              <line x1="-160" y1="-50" x2="160" y2="-50" stroke="#141414" stroke-width="1" stroke-dasharray="3,3" />
              <line x1="-160" y1="0" x2="160" y2="0" stroke="#141414" stroke-width="1" stroke-dasharray="3,3" />
              <line x1="-160" y1="50" x2="160" y2="50" stroke="#141414" stroke-width="1" stroke-dasharray="3,3" />
              <!-- Horizontal execution blocks -->
              <!-- Lane 1 (Serial) -->
              <rect x="-140" y="-85" width="260" height="24" fill="#141414" />
              <text x="-130" y="-69" font-family="'Inter Tight', sans-serif" font-weight="600" font-size="9" fill="#FFFFFF">WORKER #0 (SERIAL T₁)</text>
              <!-- Lane 2 -->
              <rect x="-140" y="-35" width="135" height="24" fill="#1F3FB0" />
              <rect x="0" y="-35" width="115" height="24" fill="#1F3FB0" />
              <!-- Lane 3 -->
              <rect x="-140" y="15" width="60" height="24" fill="#E0452B" />
              <rect x="-75" y="15" width="70" height="24" fill="#E0452B" />
              <rect x="0" y="15" width="55" height="24" fill="#E0452B" />
              <rect x="60" y="15" width="75" height="24" fill="#E0452B" />
              <!-- Lane 4 (16 Workers saturated) -->
              <rect x="-140" y="65" width="28" height="22" fill="#1F3FB0" />
              <rect x="-108" y="65" width="32" height="22" fill="#141414" />
              <rect x="-72" y="65" width="26" height="22" fill="#E0452B" />
              <rect x="-42" y="65" width="30" height="22" fill="#1F3FB0" />
              <rect x="-8" y="65" width="34" height="22" fill="#141414" />
              <rect x="30" y="65" width="28" height="22" fill="#E0452B" />
              <rect x="62" y="65" width="30" height="22" fill="#1F3FB0" />
              <rect x="96" y="65" width="32" height="22" fill="#141414" />
              <!-- Timeline vertical tick -->
              <line x1="140" y1="-100" x2="140" y2="100" stroke="#E0452B" stroke-width="2" stroke-dasharray="4,2" />
            </g>
          `;
        } else {
          return `
            <g transform="translate(71, 105)">
              <rect x="-45" y="-30" width="90" height="12" fill="#141414" />
              <rect x="-45" y="-14" width="42" height="12" fill="#1F3FB0" />
              <rect x="2" y="-14" width="40" height="12" fill="#1F3FB0" />
              <rect x="-45" y="2" width="20" height="12" fill="#E0452B" />
              <rect x="-20" y="2" width="22" height="12" fill="#E0452B" />
              <rect x="7" y="2" width="24" height="12" fill="#E0452B" />
              <rect x="-45" y="18" width="8" height="12" fill="#1F3FB0" />
              <rect x="-33" y="18" width="10" height="12" fill="#141414" />
              <rect x="-19" y="18" width="8" height="12" fill="#E0452B" />
              <rect x="-7" y="18" width="12" height="12" fill="#1F3FB0" />
              <rect x="9" y="18" width="8" height="12" fill="#141414" />
              <rect x="21" y="18" width="10" height="12" fill="#E0452B" />
            </g>
          `;
        }

      case 'hardware':
        // Chip outline with pin ticks plus 4-pin JTAG header
        if (isCover) {
          return `
            <g transform="translate(237, 480)">
              <!-- Main IC Package (QFP) -->
              <rect x="-80" y="-80" width="160" height="160" fill="#141414" rx="6" />
              <!-- Corner pin 1 indicator -->
              <circle cx="-60" cy="-60" r="6" fill="#E0452B" />
              <!-- Die substrate center plate -->
              <rect x="-45" y="-45" width="90" height="90" fill="#E8E1D0" stroke="#1F3FB0" stroke-width="2" />
              <text x="0" y="-15" font-family="'Inter Tight', sans-serif" font-weight="700" font-size="11" text-anchor="middle" fill="#1F3FB0">STM32 / ARTIX</text>
              <text x="0" y="5" font-family="'Inter Tight', sans-serif" font-weight="600" font-size="9" text-anchor="middle" fill="#141414">IEEE 1149.1</text>
              <text x="0" y="25" font-family="'Inter Tight', sans-serif" font-weight="600" font-size="9" text-anchor="middle" fill="#E0452B">TAP ENGINE</text>
              <!-- QFP Pins around edges -->
              <!-- Top pins -->
              <path d="M-60,-80 v-15 M-40,-80 v-15 M-20,-80 v-15 M0,-80 v-15 M20,-80 v-15 M40,-80 v-15 M60,-80 v-15" stroke="#1F3FB0" stroke-width="3" />
              <!-- Bottom pins -->
              <path d="M-60,80 v15 M-40,80 v15 M-20,80 v15 M0,80 v15 M20,80 v15 M40,80 v15 M60,80 v15" stroke="#1F3FB0" stroke-width="3" />
              <!-- Left pins -->
              <path d="M-80,-60 h-15 M-80,-40 h-15 M-80,-20 h-15 M-80,0 h-15 M-80,20 h-15 M-80,40 h-15 M-80,60 h-15" stroke="#1F3FB0" stroke-width="3" />
              <!-- Right pins -->
              <path d="M80,-60 h15 M80,-40 h15 M80,-20 h15 M80,0 h15 M80,20 h15 M80,40 h15 M80,60 h15" stroke="#1F3FB0" stroke-width="3" />
              <!-- 4-Pin JTAG Header at Bottom Right -->
              <g transform="translate(85, 45)">
                <rect x="0" y="0" width="55" height="55" fill="#E8E1D0" stroke="#141414" stroke-width="2" />
                <circle cx="16" cy="16" r="4.5" fill="#E0452B" />
                <circle cx="39" cy="16" r="4.5" fill="#141414" />
                <circle cx="16" cy="39" r="4.5" fill="#141414" />
                <circle cx="39" cy="39" r="4.5" fill="#1F3FB0" />
                <text x="28" y="72" font-family="'Inter Tight', sans-serif" font-weight="700" font-size="9" text-anchor="middle" fill="#141414">JTAG</text>
              </g>
            </g>
          `;
        } else {
          return `
            <g transform="translate(71, 105)">
              <rect x="-30" y="-30" width="60" height="60" fill="#141414" rx="3" />
              <circle cx="-20" cy="-20" r="3" fill="#E0452B" />
              <rect x="-16" y="-16" width="32" height="32" fill="#E8E1D0" stroke="#1F3FB0" stroke-width="1.2" />
              <path d="M-20,-30 v-8 M0,-30 v-8 M20,-30 v-8" stroke="#1F3FB0" stroke-width="2" />
              <path d="M-20,30 v8 M0,30 v8 M20,30 v8" stroke="#1F3FB0" stroke-width="2" />
              <path d="M-30,-20 h-8 M-30,0 h-8 M-30,20 h-8" stroke="#1F3FB0" stroke-width="2" />
              <path d="M30,-20 h8 M30,0 h8 M30,20 h8" stroke="#1F3FB0" stroke-width="2" />
            </g>
          `;
        }

      default:
        return '';
    }
  }

  return {
    // Generate Spine SVG (viewBox 0 0 142 840)
    renderSpineSvg: function(module) {
      const motifSvg = renderMotif(module.motif, false);
      const title = module.title.toUpperCase();
      
      return `
        <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 142 840" width="100%" height="100%">
          <!-- Cardboard cream paper background -->
          <rect width="142" height="840" fill="#EDEBE6" />
          
          <!-- Outer screen-print boundary rule -->
          <rect x="6" y="6" width="130" height="828" fill="none" stroke="#141414" stroke-width="1.5" />
          
          <!-- Top Motif Zone -->
          <rect x="14" y="14" width="114" height="175" fill="#E8E1D0" stroke="#141414" stroke-width="1" />
          <line x1="14" y1="189" x2="128" y2="189" stroke="#141414" stroke-width="2" />
          ${motifSvg}

          <!-- Japanese gloss under motif if enabled -->
          ${CONFIG.showKanji && module.romaji ? `
            <text x="71" y="178" font-family="'Inter Tight', 'Yu Gothic UI', sans-serif" font-weight="700" font-size="12" letter-spacing="0.1em" text-anchor="middle" fill="#1F3FB0">
              ${module.romaji}
            </text>
          ` : ''}

          <!-- Vertical Condensed Serif Title -->
          <!-- We write vertical text centered down the tape spine -->
          <g transform="translate(71, 230)">
            <text x="0" y="0" transform="rotate(90)" font-family="'Newsreader', Georgia, serif" font-weight="600" font-size="28" letter-spacing="0.08em" fill="#141414" dominant-baseline="central">
              ${title}
            </text>
          </g>

          <!-- Middle separator band -->
          <line x1="20" y1="710" x2="122" y2="710" stroke="#141414" stroke-width="1" stroke-dasharray="3,3" />

          <!-- Bottom Branding: UEW Emblem -->
          <text x="71" y="742" font-family="'Newsreader', Georgia, serif" font-weight="700" font-size="20" letter-spacing="0.05em" text-anchor="middle" fill="#1F3FB0">
            UEW
          </text>
          <text x="71" y="756" font-family="'Inter Tight', sans-serif" font-weight="600" font-size="7.5" letter-spacing="0.22em" text-anchor="middle" fill="#6D6A63">
            C++17 ENGINE
          </text>

          <!-- Boxed Task Badge at bottom (e.g. TASK C) -->
          <g transform="translate(26, 772)">
            <rect width="90" height="32" fill="#141414" rx="2" />
            <text x="45" y="21" font-family="'Inter Tight', sans-serif" font-weight="700" font-size="13" letter-spacing="0.14em" text-anchor="middle" fill="#FFFFFF">
              ${module.label}
            </text>
          </g>
        </svg>
      `;
    },

    // Generate Cover SVG (viewBox 0 0 474 900)
    renderCoverSvg: function(module) {
      const motifSvg = renderMotif(module.motif, true);
      const title = module.title;
      
      return `
        <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 474 900" width="100%" height="100%">
          <!-- Paper background -->
          <rect width="474" height="900" fill="#EDEBE6" />
          
          <!-- Outer double frame rule -->
          <rect x="14" y="14" width="446" height="872" fill="none" stroke="#141414" stroke-width="2" />
          <rect x="20" y="20" width="434" height="860" fill="none" stroke="#141414" stroke-width="1" />
          
          <!-- Top Masthead Header -->
          <text x="237" y="52" font-family="'Inter Tight', sans-serif" font-weight="700" font-size="10.5" letter-spacing="0.32em" text-anchor="middle" fill="#141414">
            UNIFIED ENGINEERING WORKBENCH
          </text>
          
          <line x1="36" y1="68" x2="438" y2="68" stroke="#141414" stroke-width="1.5" />
          
          <!-- Subtitle / Group & Japanese Tag -->
          <text x="42" y="98" font-family="'Inter Tight', sans-serif" font-weight="600" font-size="12" letter-spacing="0.16em" fill="#1F3FB0">
            ${module.group} / ${module.label}
          </text>
          
          ${CONFIG.showKanji && module.romaji ? `
            <text x="432" y="98" font-family="'Inter Tight', 'Yu Gothic UI', sans-serif" font-weight="600" font-size="14" letter-spacing="0.1em" text-anchor="end" fill="#E0452B">
              ${module.romaji}
            </text>
          ` : ''}

          <!-- Big Serif Title -->
          <g transform="translate(42, 136)">
            <text x="0" y="0" font-family="'Newsreader', Georgia, serif" font-weight="400" font-size="38" letter-spacing="-0.02em" fill="#141414">
              ${title}
            </text>
          </g>

          <line x1="42" y1="168" x2="432" y2="168" stroke="#141414" stroke-width="2" />

          <!-- Central Technical Screen-Print Illustration Canvas -->
          <rect x="42" y="190" width="390" height="570" fill="#E8E1D0" stroke="#141414" stroke-width="1.5" />
          
          <!-- CAD Technical Grid inside illustration -->
          <g stroke="#141414" stroke-width="0.5" stroke-dasharray="4,6" opacity="0.3">
            <line x1="42" y1="330" x2="432" y2="330" />
            <line x1="42" y1="480" x2="432" y2="480" />
            <line x1="42" y1="620" x2="432" y2="620" />
            <line x1="172" y1="190" x2="172" y2="760" />
            <line x1="302" y1="190" x2="302" y2="760" />
          </g>

          <!-- Module Central Motif -->
          ${motifSvg}

          <!-- Bottom Footer Band -->
          <line x1="42" y1="785" x2="432" y2="785" stroke="#141414" stroke-width="1" />
          
          <text x="42" y="815" font-family="'Newsreader', Georgia, serif" font-style="italic" font-size="13" fill="#6D6A63">
            PASS Science Works Assessment
          </text>
          <text x="42" y="834" font-family="'Inter Tight', sans-serif" font-weight="600" font-size="9.5" letter-spacing="0.1em" fill="#141414">
            CANDIDATE ID: ${CONFIG.candidateId}
          </text>

          <!-- Boxed UEW Badge Bottom Right -->
          <g transform="translate(344, 798)">
            <rect width="88" height="44" fill="#1F3FB0" stroke="#141414" stroke-width="2" />
            <text x="44" y="24" font-family="'Newsreader', Georgia, serif" font-weight="700" font-size="20" letter-spacing="0.08em" text-anchor="middle" fill="#FFFFFF">
              UEW
            </text>
            <text x="44" y="37" font-family="'Inter Tight', sans-serif" font-weight="700" font-size="7" letter-spacing="0.2em" text-anchor="middle" fill="#E8E1D0">
              STANDALONE
            </text>
          </g>
        </svg>
      `;
    }
  };
})();
