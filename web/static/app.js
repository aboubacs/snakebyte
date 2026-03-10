// ===== State =====
let versions = [];
let currentMatch = null;
let currentFrame = 0;
let playing = false;
let playInterval = null;
let currentPool = null;
let currentPoolName = '';
let expandedVersion = null;
let autoRefreshInterval = null;

// ===== Helpers =====
function winRateColor(rate) {
    // Gradient: red (0%) -> orange (25%) -> gray (50%) -> teal (75%) -> green (100%)
    const r = rate * 100;
    if (r <= 50) {
        // red to gray: hsl(0, 70%, 45%) -> hsl(0, 0%, 55%)
        const t = r / 50;
        const h = 0;
        const s = 70 * (1 - t);
        const l = 45 + 10 * t;
        return `hsl(${h}, ${s}%, ${l}%)`;
    } else {
        // gray to green: hsl(0, 0%, 55%) -> hsl(145, 65%, 38%)
        const t = (r - 50) / 50;
        const h = 145 * t;
        const s = 65 * t;
        const l = 55 - 17 * t;
        return `hsl(${h}, ${s}%, ${l}%)`;
    }
}

// ===== API =====
async function api(url, opts = {}) {
    if (opts.body && typeof opts.body === 'object') {
        opts.headers = { 'Content-Type': 'application/json', ...opts.headers };
        opts.body = JSON.stringify(opts.body);
    }
    const res = await fetch(url, opts);
    return res.json();
}

// ===== Versions =====
async function loadVersions() {
    versions = await api('/api/versions');
    renderVersions();
    populateSelects();
}

function renderVersions() {
    const el = document.getElementById('version-list');
    if (!versions.length) {
        el.innerHTML = '<div class="empty">No builds yet.<br>Run <code>make version</code> to create one.</div>';
        return;
    }
    el.innerHTML = versions.map(v =>
        `<div class="version-item">
            <span class="name">${v.name}</span>
            <span class="meta">${v.modified.split('T')[0]}</span>
        </div>`
    ).join('');
}

function populateSelects() {
    const opts = versions.map(v => `<option value="${v.name}">${v.name}</option>`).join('');
    document.getElementById('player1').innerHTML = opts;
    document.getElementById('player2').innerHTML = opts;
    document.getElementById('add-version-select').innerHTML =
        '<option value="">Add version...</option>' + opts;
}

// ===== Match =====
async function runMatch() {
    const p1 = document.getElementById('player1').value;
    const p2 = document.getElementById('player2').value;
    if (!p1 || !p2) return;

    const btn = document.getElementById('btn-run-match');
    const statusEl = document.getElementById('match-status');
    statusEl.innerHTML = '<div class="status info">Running...</div>';
    btn.disabled = true;

    try {
        const result = await api('/api/match', { method: 'POST', body: { players: [p1, p2] } });
        if (result.error) {
            statusEl.innerHTML = `<div class="status error">${result.error}</div>`;
        } else {
            currentMatch = result.log;
            currentFrame = 0;
            const winner = result.log.winner >= 0 ? result.players[result.log.winner] : 'draw';
            const endReason = result.log.end_reason && result.log.end_reason !== 'normal'
                ? ` [${result.log.end_reason}]` : '';
            statusEl.innerHTML = `<div class="status success">#${result.id} — ${winner}${endReason}</div>`;
            setupViewer();
        }
    } catch (e) {
        statusEl.innerHTML = `<div class="status error">${e.message}</div>`;
    }
    btn.disabled = false;
}

// ===== Viewer =====
function setupViewer() {
    if (!currentMatch?.frames) return;
    const slider = document.getElementById('frame-slider');
    slider.max = currentMatch.frames.length - 1;
    slider.value = 0;
    currentFrame = 0;
    renderFrame();
}

// Player colors
const PLAYER_COLORS = [
    { head: '#2563eb', body: '#60a5fa', bodyAlt: '#93bbfd' },  // blue
    { head: '#dc2626', body: '#f87171', bodyAlt: '#fca5a5' },  // red
];

function renderFrame() {
    if (!currentMatch?.frames) return;
    const canvas = document.getElementById('viewer-canvas');
    const ctx = canvas.getContext('2d');

    const rect = canvas.parentElement.getBoundingClientRect();
    canvas.width = rect.width;
    canvas.height = rect.height;

    ctx.fillStyle = '#ffffff';
    ctx.fillRect(0, 0, canvas.width, canvas.height);

    const frame = currentMatch.frames[currentFrame];
    if (!frame) return;

    const gw = currentMatch.width || 25;
    const gh = currentMatch.height || 15;
    const grid = currentMatch.grid || [];
    const scores = frame.state.scores || [];
    const energyCells = frame.state.energy || [];
    const snakes = frame.state.snakes || [];

    // Layout: HUD on top, grid below
    const hudH = 36;
    const pad = 8;
    const availW = canvas.width - pad * 2;
    const availH = canvas.height - hudH - pad * 2;
    const cellSize = Math.min(Math.floor(availW / gw), Math.floor(availH / gh));
    const gridW = cellSize * gw;
    const gridH = cellSize * gh;
    const ox = Math.floor((canvas.width - gridW) / 2);
    const oy = hudH + Math.floor((canvas.height - hudH - gridH) / 2);

    // --- HUD ---
    ctx.font = '500 11px -apple-system, system-ui, sans-serif';
    ctx.fillStyle = '#9ca3af';
    ctx.fillText(`Turn ${frame.state.turn}`, pad, 14);

    // Player scores
    for (let i = 0; i < 2; i++) {
        const x = pad + 80 + i * 120;
        ctx.fillStyle = PLAYER_COLORS[i].head;
        ctx.fillRect(x, 5, 10, 10);
        ctx.fillStyle = '#09090b';
        ctx.font = '500 11px "SF Mono", Consolas, monospace';
        ctx.fillText(`P${i}: ${scores[i] || 0}`, x + 14, 14);
    }

    // End reason on last frame
    const isLastFrame = currentFrame === currentMatch.frames.length - 1;
    const endReason = currentMatch.end_reason;
    if (isLastFrame && endReason && endReason !== 'normal') {
        ctx.fillStyle = '#dc2626';
        ctx.font = 'bold 11px "SF Mono", Consolas, monospace';
        ctx.fillText(endReason.toUpperCase(), pad + 80 + 2 * 120, 14);
    }

    // --- Grid background ---
    ctx.fillStyle = '#f9fafb';
    ctx.fillRect(ox, oy, gridW, gridH);

    // Grid lines
    ctx.strokeStyle = '#f0f0f0';
    ctx.lineWidth = 0.5;
    for (let x = 0; x <= gw; x++) {
        ctx.beginPath();
        ctx.moveTo(ox + x * cellSize, oy);
        ctx.lineTo(ox + x * cellSize, oy + gridH);
        ctx.stroke();
    }
    for (let y = 0; y <= gh; y++) {
        ctx.beginPath();
        ctx.moveTo(ox, oy + y * cellSize);
        ctx.lineTo(ox + gridW, oy + y * cellSize);
        ctx.stroke();
    }

    // --- Platforms ---
    for (let y = 0; y < gh; y++) {
        if (!grid[y]) continue;
        for (let x = 0; x < gw; x++) {
            if (grid[y][x] === '#') {
                ctx.fillStyle = '#374151';
                ctx.fillRect(ox + x * cellSize, oy + y * cellSize, cellSize, cellSize);
            }
        }
    }

    // --- Energy ---
    const inset = Math.max(2, cellSize * 0.2);
    for (const [ex, ey] of energyCells) {
        ctx.fillStyle = '#fbbf24';
        ctx.strokeStyle = '#d97706';
        ctx.lineWidth = 1;
        const cx = ox + ex * cellSize + cellSize / 2;
        const cy = oy + ey * cellSize + cellSize / 2;
        const r = cellSize / 2 - inset;
        ctx.beginPath();
        ctx.arc(cx, cy, r, 0, Math.PI * 2);
        ctx.fill();
        ctx.stroke();
    }

    // --- Snakes ---
    for (const snake of snakes) {
        if (!snake.alive) continue;
        const colors = PLAYER_COLORS[snake.owner] || PLAYER_COLORS[0];
        const body = snake.body || [];

        // Body segments
        for (let i = body.length - 1; i >= 0; i--) {
            const [bx, by] = body[i];
            const isHead = (i === 0);
            const seg = Math.max(1, cellSize - 2);
            const sOff = (cellSize - seg) / 2;

            if (isHead) {
                ctx.fillStyle = colors.head;
            } else {
                ctx.fillStyle = (i % 2 === 0) ? colors.body : colors.bodyAlt;
            }

            ctx.fillRect(
                ox + bx * cellSize + sOff,
                oy + by * cellSize + sOff,
                seg, seg
            );

            // Draw border for head
            if (isHead) {
                ctx.strokeStyle = '#09090b';
                ctx.lineWidth = 1.5;
                ctx.strokeRect(
                    ox + bx * cellSize + sOff,
                    oy + by * cellSize + sOff,
                    seg, seg
                );

                // Eyes: two small dots based on direction
                const ecx = ox + bx * cellSize + cellSize / 2;
                const ecy = oy + by * cellSize + cellSize / 2;
                const eyeR = Math.max(1, cellSize * 0.1);
                const eyeOff = cellSize * 0.2;
                ctx.fillStyle = '#ffffff';

                let e1x, e1y, e2x, e2y;
                switch (snake.dir) {
                    case 'UP':    e1x = ecx - eyeOff; e1y = ecy - eyeOff; e2x = ecx + eyeOff; e2y = ecy - eyeOff; break;
                    case 'DOWN':  e1x = ecx - eyeOff; e1y = ecy + eyeOff; e2x = ecx + eyeOff; e2y = ecy + eyeOff; break;
                    case 'LEFT':  e1x = ecx - eyeOff; e1y = ecy - eyeOff; e2x = ecx - eyeOff; e2y = ecy + eyeOff; break;
                    case 'RIGHT': e1x = ecx + eyeOff; e1y = ecy - eyeOff; e2x = ecx + eyeOff; e2y = ecy + eyeOff; break;
                    default:      e1x = ecx - eyeOff; e1y = ecy - eyeOff; e2x = ecx + eyeOff; e2y = ecy - eyeOff;
                }
                ctx.beginPath(); ctx.arc(e1x, e1y, eyeR, 0, Math.PI * 2); ctx.fill();
                ctx.beginPath(); ctx.arc(e2x, e2y, eyeR, 0, Math.PI * 2); ctx.fill();
            }
        }

        // Debug text above head
        if (snake.debug && body.length > 0) {
            const [hx, hy] = body[0];
            ctx.fillStyle = colors.head;
            ctx.font = '10px -apple-system, system-ui, sans-serif';
            ctx.textAlign = 'center';
            ctx.fillText(snake.debug, ox + hx * cellSize + cellSize / 2, oy + hy * cellSize - 3);
            ctx.textAlign = 'start';
        }
    }

    // --- Grid border ---
    ctx.strokeStyle = '#d1d5db';
    ctx.lineWidth = 1;
    ctx.strokeRect(ox, oy, gridW, gridH);

    // --- Frame info ---
    document.getElementById('frame-slider').value = currentFrame;
    document.getElementById('frame-info').textContent =
        `${currentFrame + 1} / ${currentMatch.frames.length}`;
}

function stepFrame(delta) {
    if (!currentMatch) return;
    currentFrame = Math.max(0, Math.min(currentMatch.frames.length - 1, currentFrame + delta));
    renderFrame();
}

function togglePlay() {
    const btn = document.getElementById('btn-play');
    if (playing) {
        clearInterval(playInterval);
        playing = false;
        btn.innerHTML = '&#9654;';
    } else {
        playing = true;
        btn.innerHTML = '&#9646;&#9646;';
        playInterval = setInterval(() => {
            if (currentFrame >= currentMatch.frames.length - 1) { togglePlay(); return; }
            stepFrame(1);
        }, 200);
    }
}

// ===== League =====
async function loadPools() {
    const pools = await api('/api/leagues');
    const select = document.getElementById('pool-select');
    const prev = select.value;
    select.innerHTML = '<option value="">Select pool</option>' +
        pools.map(p => {
            const label = p.active ? p.name : `${p.name} (paused)`;
            return `<option value="${p.name}">${label}</option>`;
        }).join('');
    if (prev && pools.some(p => p.name === prev)) {
        select.value = prev;
    }
}

async function loadPool(name) {
    currentPoolName = name;
    const toggleBtn = document.getElementById('btn-toggle-active');
    if (!name) {
        currentPool = null;
        toggleBtn.style.display = 'none';
        renderRankings();
        renderMatchHistory();
        return;
    }
    currentPool = await api(`/api/leagues/${name}`);
    // Update toggle button
    toggleBtn.style.display = '';
    toggleBtn.innerHTML = currentPool.active ? '&#9646;&#9646;' : '&#9654;';
    toggleBtn.title = currentPool.active ? 'Pause pool' : 'Activate pool';
    toggleBtn.classList.toggle('pool-paused', !currentPool.active);
    renderRankings();
    renderMatchHistory();
}

async function togglePoolActive() {
    if (!currentPoolName || !currentPool) return;
    await api(`/api/leagues/${currentPoolName}/active`, {
        method: 'POST', body: { active: !currentPool.active }
    });
    await loadPool(currentPoolName);
    await loadPools();
}

function renderRankings() {
    const prev = expandedVersion;
    expandedVersion = null;
    const tbody = document.querySelector('#rankings-table tbody');
    if (!currentPool) {
        tbody.innerHTML = '<tr><td colspan="5" class="empty">Select a pool to view rankings</td></tr>';
        return;
    }
    const rankings = currentPool.rankings || [];
    if (!rankings.length) {
        tbody.innerHTML = '<tr><td colspan="5" class="empty">No versions in this pool</td></tr>';
        return;
    }
    const gp = currentPool.games_played || {};
    const wr = currentPool.win_rates || {};
    tbody.innerHTML = rankings.map((v, i) => {
        const rate = wr[v] || 0;
        const pct = Math.round(rate * 100);
        const wrColor = winRateColor(rate);
        return `<tr data-version="${v}"><td>${i + 1}</td><td>${v}</td><td>${gp[v] || 0}</td><td>${Math.round(currentPool.ratings[v] || 1500)}</td><td style="color:${wrColor};font-weight:600">${pct}%</td><td><button class="btn-remove" onclick="removeVersion(event,'${v}')" title="Remove">&times;</button></td></tr>`;
    }).join('');

    // Re-apply H2H if a version was selected
    if (prev && rankings.includes(prev)) {
        toggleH2H(prev);
    }
}

async function removeVersion(event, version) {
    event.stopPropagation();
    if (!currentPoolName) return;
    if (!confirm(`Remove ${version} from pool?`)) return;
    await api(`/api/leagues/${currentPoolName}/versions/${version}`, { method: 'DELETE' });
    await loadPool(currentPoolName);
}

function clearH2H() {
    document.querySelectorAll('.h2h-badge').forEach(el => el.remove());
    document.querySelectorAll('tr.h2h-selected').forEach(el => el.classList.remove('h2h-selected'));
    expandedVersion = null;
}

async function toggleH2H(version) {
    if (expandedVersion === version) {
        clearH2H();
        return;
    }
    clearH2H();

    expandedVersion = version;
    const data = await api(`/api/leagues/${currentPoolName}/h2h/${version}`);
    if (!data.h2h) return;

    // Highlight selected row
    const selectedRow = document.querySelector(`tr[data-version="${version}"]`);
    if (!selectedRow) { expandedVersion = null; return; }
    selectedRow.classList.add('h2h-selected');

    // Annotate each opponent's row with the H2H record
    for (const [opp, r] of Object.entries(data.h2h)) {
        const oppRow = document.querySelector(`tr[data-version="${opp}"]`);
        if (!oppRow) continue;

        const net = r.wins - r.losses;
        const cls = net > 0 ? 'h2h-positive' : net < 0 ? 'h2h-negative' : 'h2h-neutral';
        const badge = document.createElement('span');
        badge.className = `h2h-badge ${cls}`;
        badge.textContent = `${r.wins}W ${r.losses}L${r.draws ? ' ' + r.draws + 'D' : ''}`;

        // Append badge to the version name cell (2nd td)
        oppRow.cells[1].appendChild(badge);
    }
}

function renderMatchHistory() {
    const el = document.getElementById('match-history');
    if (!currentPool?.matches?.length) {
        el.innerHTML = '<div class="empty">No matches played yet</div>';
        return;
    }
    const matches = [...currentPool.matches].reverse().slice(0, 100);
    el.innerHTML = matches.map(m => {
        const isDraw = !m.winner;
        return `<div class="match-item">
            <span class="players">${m.players.join(' vs ')}</span>
            <span class="winner ${isDraw ? 'draw' : ''}">${m.winner || 'draw'}</span>
        </div>`;
    }).join('');
}

// Pool dialog
function showCreatePoolDialog() {
    document.getElementById('create-pool-dialog').style.display = 'flex';
    const input = document.getElementById('new-pool-name');
    input.value = '';
    setTimeout(() => input.focus(), 50);
}

function hideCreatePoolDialog() {
    document.getElementById('create-pool-dialog').style.display = 'none';
}

async function createPool() {
    const name = document.getElementById('new-pool-name').value.trim();
    if (!name) return;
    await api('/api/leagues', { method: 'POST', body: { name } });
    hideCreatePoolDialog();
    await loadPools();
    document.getElementById('pool-select').value = name;
    await loadPool(name);
}

async function addVersionToPool() {
    const version = document.getElementById('add-version-select').value;
    if (!currentPoolName || !version) return;
    await api(`/api/leagues/${currentPoolName}/versions`, { method: 'POST', body: { version } });
    document.getElementById('add-version-select').value = '';
    await loadPool(currentPoolName);
}

async function runLeagueMatches() {
    if (!currentPoolName) return;
    const btn = document.getElementById('btn-run-league');
    const statusEl = document.getElementById('league-status');
    statusEl.innerHTML = '<div class="status info">Running...</div>';
    btn.disabled = true;

    try {
        const result = await api(`/api/leagues/${currentPoolName}/run`, {
            method: 'POST', body: { count: 10 }
        });
        if (result.error) {
            statusEl.innerHTML = `<div class="status error">${result.error}</div>`;
        } else {
            statusEl.innerHTML = `<div class="status success">${result.matches_run} matches done</div>`;
            await loadPool(currentPoolName);
        }
    } catch (e) {
        statusEl.innerHTML = `<div class="status error">${e.message}</div>`;
    }
    btn.disabled = false;
}

// ===== Tabs =====
function setupTabs() {
    document.querySelectorAll('.tab').forEach(tab => {
        tab.addEventListener('click', () => {
            const parent = tab.closest('.panel') || tab.parentElement.parentElement;
            parent.querySelectorAll('.tab').forEach(t => t.classList.remove('active'));
            parent.querySelectorAll('.tab-content').forEach(t => t.classList.remove('active'));
            tab.classList.add('active');
            document.getElementById('tab-' + tab.dataset.tab).classList.add('active');
        });
    });
}

// ===== Init =====
document.addEventListener('DOMContentLoaded', () => {
    setupTabs();
    loadVersions();
    loadPools();

    // Match
    document.getElementById('btn-run-match').addEventListener('click', runMatch);

    // Viewer
    document.getElementById('btn-prev').addEventListener('click', () => stepFrame(-1));
    document.getElementById('btn-next').addEventListener('click', () => stepFrame(1));
    document.getElementById('btn-play').addEventListener('click', togglePlay);
    document.getElementById('frame-slider').addEventListener('input', e => {
        currentFrame = parseInt(e.target.value);
        renderFrame();
    });

    // Pool
    document.getElementById('pool-select').addEventListener('change', e => loadPool(e.target.value));
    document.getElementById('btn-toggle-active').addEventListener('click', togglePoolActive);
    document.getElementById('btn-refresh-pool').addEventListener('click', () => {
        if (currentPoolName) loadPool(currentPoolName);
    });

    // Create pool dialog
    document.getElementById('btn-new-pool').addEventListener('click', showCreatePoolDialog);
    document.getElementById('btn-cancel-pool').addEventListener('click', hideCreatePoolDialog);
    document.getElementById('btn-confirm-pool').addEventListener('click', createPool);
    document.getElementById('new-pool-name').addEventListener('keydown', e => {
        if (e.key === 'Enter') createPool();
        if (e.key === 'Escape') hideCreatePoolDialog();
    });
    document.getElementById('create-pool-dialog').addEventListener('click', e => {
        if (e.target === e.currentTarget) hideCreatePoolDialog();
    });

    // League
    document.getElementById('btn-add-version').addEventListener('click', addVersionToPool);
    document.getElementById('btn-run-league').addEventListener('click', runLeagueMatches);

    // H2H click on rankings table
    document.getElementById('rankings-table').addEventListener('click', e => {
        const row = e.target.closest('tr[data-version]');
        if (!row) return;
        toggleH2H(row.dataset.version);
    });

    // Auto-refresh pool data every 3 seconds (for background runner updates)
    autoRefreshInterval = setInterval(() => {
        if (currentPoolName) {
            loadPool(currentPoolName);
        }
    }, 3000);
});
