/**
 * app.js — LRU Cache Visualizer frontend
 *
 * Calls the C HTTP server's API endpoints and renders the cache state,
 * latency comparisons, and operation log in real time. No frameworks —
 * plain DOM manipulation with fetch().
 */

const API_BASE = `http://${location.hostname || 'localhost'}:8080`;

/* ── Latency tracking for the comparison chart ─────────────────────── */
const latencyData = { hits: [], misses: [] };
const MAX_LATENCY_BARS = 12;   /* keep the chart readable */

/* ══════════════════════════════════════════════════════════════════════
 * API CALLS
 * ══════════════════════════════════════════════════════════════════════ */

async function apiGet(key) {
    const res = await fetch(`${API_BASE}/api/get/${encodeURIComponent(key)}`);
    return res.json();
}

async function apiPut(key, value) {
    const res = await fetch(`${API_BASE}/api/put`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ key, value })
    });
    return res.json();
}

async function apiState() {
    const res = await fetch(`${API_BASE}/api/cache/state`);
    return res.json();
}

async function apiStats() {
    const res = await fetch(`${API_BASE}/api/stats`);
    return res.json();
}

async function apiDelete(key) {
    const res = await fetch(`${API_BASE}/api/delete/${encodeURIComponent(key)}`, {
        method: 'DELETE'
    });
    return res.json();
}

async function apiResize(capacity) {
    const res = await fetch(`${API_BASE}/api/capacity`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ capacity })
    });
    return res.json();
}

/* ══════════════════════════════════════════════════════════════════════
 * USER ACTIONS (wired to buttons)
 * ══════════════════════════════════════════════════════════════════════ */

async function doGet() {
    const keyVal = document.getElementById('get-key').value.trim();
    if (!keyVal) return;
    const key = parseInt(keyVal, 10);

    try {
        const data = await apiGet(key);

        if (data.hit) {
            logEntry('HIT', `GET ${key} → ${data.value}  (${data.latency_ms.toFixed(2)}ms)`, 'hit');
            latencyData.hits.push(data.latency_ms);
        } else {
            logEntry('MISS', `GET ${key} → ${data.value}  (${data.latency_ms.toFixed(2)}ms, fetched from DB)`, 'miss');
            latencyData.misses.push(data.latency_ms);
        }

        await refreshView();
        updateLatencyChart();
    } catch (e) {
        logEntry('ERR', `GET failed: ${e.message}`, 'info');
    }

    document.getElementById('get-key').value = '';
    document.getElementById('get-key').focus();
}

async function doPut() {
    const keyVal = document.getElementById('put-key').value.trim();
    const valVal = document.getElementById('put-value').value.trim();
    if (!keyVal || !valVal) return;
    const key = parseInt(keyVal, 10);
    const value = parseInt(valVal, 10);

    try {
        const data = await apiPut(key, value);
        logEntry('PUT', `PUT ${key} = ${value}`, 'put');

        if (data.evicted) {
            logEntry('EVICT', `Evicted LRU key ${data.evicted}`, 'evict');
        }

        await refreshView();
    } catch (e) {
        logEntry('ERR', `PUT failed: ${e.message}`, 'info');
    }

    document.getElementById('put-key').value = '';
    document.getElementById('put-value').value = '';
    document.getElementById('put-key').focus();
}

async function doResize() {
    const cap = parseInt(document.getElementById('capacity-input').value, 10);
    if (!cap || cap < 1) return;

    try {
        const data = await apiResize(cap);
        logEntry('INFO', `Resized capacity to ${cap}`, 'info');

        if (data.evicted_count > 0) {
            logEntry('EVICT', `Evicted ${data.evicted_count} LRU item(s) during resize`, 'evict');
        }

        await refreshView();
    } catch (e) {
        logEntry('ERR', `Resize failed: ${e.message}`, 'info');
    }
}

/* ══════════════════════════════════════════════════════════════════════
 * SCRIPTED DEMO
 *
 * Fills the cache, triggers evictions, and shows hit/miss latency
 * difference — all animated step by step.
 * ══════════════════════════════════════════════════════════════════════ */

async function runDemo() {
    const btn = document.getElementById('btn-demo');
    btn.disabled = true;
    btn.textContent = '⏳ Running...';

    logEntry('INFO', '═══ Starting scripted demo ═══', 'info');

    /* Helper: run a step with a visible pause */
    const delay = (ms) => new Promise(r => setTimeout(r, ms));

    try {
        /* Step 1: Reset to capacity 5 */
        await apiResize(5);
        logEntry('INFO', 'Reset capacity to 5', 'info');
        await refreshView();
        await delay(500);

        /* Step 2: Fill the cache with 5 items */
        const items = [
            [1, 100],
            [2, 200],
            [3, 300],
            [4, 400],
            [5, 500]
        ];

        for (const [k, v] of items) {
            const data = await apiPut(k, v);
            logEntry('PUT', `PUT ${k} = ${v}`, 'put');
            await refreshView();
            await delay(400);
        }

        logEntry('INFO', 'Cache is now full (5/5). Next PUT will trigger eviction.', 'info');
        await delay(800);

        /* Step 3: Access 1 so it moves to MRU (not the next eviction victim) */
        const getResult = await apiGet(1);
        logEntry(getResult.hit ? 'HIT' : 'MISS',
            `GET 1 → ${getResult.value} — moved to MRU`, getResult.hit ? 'hit' : 'miss');
        if (getResult.hit) latencyData.hits.push(getResult.latency_ms);
        else latencyData.misses.push(getResult.latency_ms);
        await refreshView();
        updateLatencyChart();
        await delay(600);

        /* Step 4: Insert a new key — triggers eviction of 2 (current LRU) */
        logEntry('INFO', 'Inserting 6 — should evict the LRU item 2', 'info');
        await delay(300);
        const putResult = await apiPut(6, 600);
        logEntry('PUT', `PUT 6 = 600`, 'put');
        if (putResult.evicted) {
            logEntry('EVICT', `Evicted LRU key ${putResult.evicted} ✓`, 'evict');
        }
        await refreshView();
        await delay(800);

        /* Step 5: Show hit vs. miss latency */
        logEntry('INFO', 'Now comparing hit vs miss latency...', 'info');
        await delay(300);

        /* GET a key that's in cache (hit — fast) */
        const hitResult = await apiGet(6);
        latencyData.hits.push(hitResult.latency_ms);
        logEntry('HIT', `GET 6 (cached) → ${hitResult.latency_ms.toFixed(2)}ms`, 'hit');
        await refreshView();
        await delay(300);

        /* GET a key that's NOT in cache (miss — slow DB lookup) */
        const missResult = await apiGet(99);
        latencyData.misses.push(missResult.latency_ms);
        logEntry('MISS', `GET 99 (DB fetch) → ${missResult.latency_ms.toFixed(2)}ms`, 'miss');
        await refreshView();
        updateLatencyChart();
        await delay(600);

        logEntry('INFO', '═══ Demo complete. Try your own operations! ═══', 'info');
    } catch (e) {
        logEntry('ERR', `Demo failed: ${e.message}`, 'info');
    }

    btn.disabled = false;
    btn.textContent = '▶ Run Scripted Demo';
}

/* ══════════════════════════════════════════════════════════════════════
 * VIEW RENDERING
 * ══════════════════════════════════════════════════════════════════════ */

async function refreshView() {
    try {
        const [state, stats] = await Promise.all([apiState(), apiStats()]);
        renderList(state.nodes || []);
        renderHashTable(state.nodes || [], state.num_buckets || 0);
        renderStats(stats);
    } catch (e) {
        /* Server might not be running yet — silently ignore */
    }
}

/**
 * renderList — Draw the doubly linked list MRU → LRU.
 */
function renderList(nodes) {
    const container = document.getElementById('list-nodes');

    if (nodes.length === 0) {
        container.innerHTML = '<div class="empty-message">Cache is empty</div>';
        return;
    }

    let html = '';
    nodes.forEach((node, i) => {
        const isMRU = i === 0;
        const isLRU = i === nodes.length - 1;
        let cls = 'list-node';
        let badge = '';

        if (isMRU) { cls += ' mru'; badge = '<span class="node-badge">MRU</span>'; }
        else if (isLRU && nodes.length > 1) { cls += ' lru'; badge = '<span class="node-badge">LRU</span>'; }

        if (i > 0) {
            html += '<span class="list-arrow">⇄</span>';
        }

        html += `<div class="${cls}" data-key="${node.key}" style="animation: slideIn 0.3s ease">
            ${badge}
            <span class="node-key">${node.key}</span>
            <span class="node-value">${node.value}</span>
        </div>`;
    });

    container.innerHTML = html;
}

/**
 * renderHashTable — Draw all buckets and their chains.
 */
function renderHashTable(nodes, numBuckets) {
    const container = document.getElementById('hash-table');

    if (numBuckets === 0) {
        container.innerHTML = '<div class="empty-message">No buckets to display</div>';
        return;
    }

    /* Build a map: bucket_index → list of keys in that bucket */
    const buckets = {};
    nodes.forEach(n => {
        const idx = n.bucket;
        if (!buckets[idx]) buckets[idx] = [];
        buckets[idx].push(n.key);
    });

    /* Only show buckets 0..numBuckets-1, but collapse long stretches of empty ones */
    let html = '';
    let emptyStreak = 0;

    for (let i = 0; i < numBuckets; i++) {
        const chain = buckets[i];
        if (!chain) {
            emptyStreak++;
            /* Show first 2 empty, then collapse */
            if (emptyStreak <= 2 || i === numBuckets - 1) {
                html += `<div class="hash-bucket">
                    <span class="bucket-index">${i}</span>
                    <div class="bucket-chain"><span class="bucket-empty">∅</span></div>
                </div>`;
            } else if (emptyStreak === 3) {
                html += `<div class="hash-bucket">
                    <span class="bucket-index">⋮</span>
                    <div class="bucket-chain"><span class="bucket-empty">empty buckets</span></div>
                </div>`;
            }
            continue;
        }

        emptyStreak = 0;
        let chainHtml = chain.map(key =>
            `<span class="chain-node" data-key="${key}">${key}</span>`
        ).join('<span class="chain-arrow">→</span>');

        html += `<div class="hash-bucket">
            <span class="bucket-index">${i}</span>
            <div class="bucket-chain">${chainHtml}</div>
        </div>`;
    }

    container.innerHTML = html;
}

/**
 * renderStats — Update the stats bar numbers and occupancy indicator.
 */
function renderStats(stats) {
    document.getElementById('stat-hits').textContent = stats.hits;
    document.getElementById('stat-misses').textContent = stats.misses;
    document.getElementById('stat-size').textContent = stats.size;
    document.getElementById('stat-capacity').textContent = stats.capacity;

    const total = stats.hits + stats.misses;
    const hitRate = total > 0 ? ((stats.hits / total) * 100).toFixed(1) + '%' : '—';
    document.getElementById('stat-hitrate').textContent = hitRate;

    const pct = stats.capacity > 0 ? (stats.size / stats.capacity) * 100 : 0;
    const fill = document.getElementById('occupancy-fill');
    fill.style.width = pct + '%';

    /* Color: green under 70%, orange 70-90%, red 90%+ */
    if (pct >= 90) fill.style.background = 'var(--red)';
    else if (pct >= 70) fill.style.background = 'var(--orange)';
    else fill.style.background = 'var(--accent)';
}

/* ══════════════════════════════════════════════════════════════════════
 * LATENCY CHART
 * ══════════════════════════════════════════════════════════════════════ */

function updateLatencyChart() {
    const chart = document.getElementById('latency-chart');

    /* Keep only the most recent bars */
    const recentHits = latencyData.hits.slice(-MAX_LATENCY_BARS);
    const recentMisses = latencyData.misses.slice(-MAX_LATENCY_BARS);
    const all = [...recentHits, ...recentMisses];

    if (all.length === 0) return;

    const maxMs = Math.max(300, ...all);  /* at least 300ms scale */

    /* Build interleaved bars: most recent operations on top */
    const entries = [
        ...recentHits.map(ms => ({ ms, type: 'hit' })),
        ...recentMisses.map(ms => ({ ms, type: 'miss' }))
    ].sort((a, b) => a.ms - b.ms);  /* sort so hits (short) are above misses (long) */

    let barsHtml = '';
    entries.forEach(e => {
        const pct = (e.ms / maxMs) * 100;
        const label = e.ms.toFixed(1) + 'ms';
        barsHtml += `<div class="latency-bar-row">
            <span class="latency-bar-label">${e.type === 'hit' ? 'Hit' : 'Miss'}</span>
            <div class="latency-bar-track">
                <div class="latency-bar-fill ${e.type}" style="width: ${pct}%">${label}</div>
            </div>
        </div>`;
    });

    /* Keep the axis at the bottom */
    const axisHtml = `<div class="latency-axis">
        <span>0ms</span>
        <span>${(maxMs/3).toFixed(0)}ms</span>
        <span>${(maxMs*2/3).toFixed(0)}ms</span>
        <span>${maxMs.toFixed(0)}ms</span>
    </div>`;

    chart.innerHTML = barsHtml + axisHtml;

    /* Update averages */
    const avgHit = recentHits.length > 0
        ? (recentHits.reduce((a,b) => a+b, 0) / recentHits.length).toFixed(2) + 'ms'
        : '—';
    const avgMiss = recentMisses.length > 0
        ? (recentMisses.reduce((a,b) => a+b, 0) / recentMisses.length).toFixed(2) + 'ms'
        : '—';

    document.getElementById('avg-hit').textContent = avgHit;
    document.getElementById('avg-miss').textContent = avgMiss;

    if (recentHits.length > 0 && recentMisses.length > 0) {
        const avgH = recentHits.reduce((a,b) => a+b, 0) / recentHits.length;
        const avgM = recentMisses.reduce((a,b) => a+b, 0) / recentMisses.length;
        document.getElementById('speedup').textContent = (avgM / avgH).toFixed(0) + '×';
    } else {
        document.getElementById('speedup').textContent = '—';
    }
}

/* ══════════════════════════════════════════════════════════════════════
 * OPERATION LOG
 * ══════════════════════════════════════════════════════════════════════ */

function logEntry(op, message, type) {
    const container = document.getElementById('log-container');
    const now = new Date();
    const time = now.toLocaleTimeString('en-US', { hour12: false });

    const entry = document.createElement('div');
    entry.className = `log-entry log-${type}`;
    entry.innerHTML = `<span class="log-time">${time}</span>` +
                      `<span class="log-op">${escapeHtml(op)}</span>` +
                      `<span class="log-msg">${escapeHtml(message)}</span>`;

    container.prepend(entry);   /* newest on top */

    /* Cap the log at 100 entries */
    while (container.children.length > 100) {
        container.removeChild(container.lastChild);
    }
}

/* ══════════════════════════════════════════════════════════════════════
 * UTILITIES
 * ══════════════════════════════════════════════════════════════════════ */

function escapeHtml(str) {
    if (!str) return '';
    return str.replace(/&/g, '&amp;')
              .replace(/</g, '&lt;')
              .replace(/>/g, '&gt;')
              .replace(/"/g, '&quot;');
}

/* Enter key on input fields triggers the associated button */
async function doDelete() {
    const keyVal = document.getElementById('del-key').value.trim();
    if (!keyVal) return;
    const key = parseInt(keyVal, 10);
    try {
        const data = await apiDelete(key);
        if (data.deleted) {
            logEntry('DEL', `Deleted key ${key}`, 'del');
        } else {
            logEntry('MISS', `Could not delete ${key} (not found)`, 'miss');
        }
        await refreshView();
    } catch (e) {
        logEntry('ERR', `DEL failed: ${e.message}`, 'info');
    }
    document.getElementById('del-key').value = '';
    document.getElementById('del-key').focus();
}

document.getElementById('get-key').addEventListener('keydown', e => {
    if (e.key === 'Enter') doGet();
});
document.getElementById('put-key').addEventListener('keydown', e => {
    if (e.key === 'Enter') document.getElementById('put-value').focus();
});
document.getElementById('put-value').addEventListener('keydown', e => {
    if (e.key === 'Enter') doPut();
});
document.getElementById('capacity-input').addEventListener('keydown', e => {
    if (e.key === 'Enter') doResize();
});

document.getElementById('del-key').addEventListener('keydown', e => {
    if (e.key === 'Enter') doDelete();
});

/* Initial load */
refreshView();
