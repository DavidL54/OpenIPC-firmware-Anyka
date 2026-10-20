(() => {
  const log = document.getElementById('log');
  const source = document.getElementById('log-source');
  const filter = document.getElementById('log-filter');
  const pause = document.getElementById('log-pause');
  const status = document.getElementById('log-status');
  let snapshot = '';
  let paused = false;
  let cleared = new Set();
  let stopped = false;
  function render() {
    const query = filter.value.toLowerCase();
    log.replaceChildren();
    for (const line of snapshot.split('\n')) {
      if (!line || cleared.has(line) || !line.toLowerCase().includes(query)) continue;
      const row = document.createElement('div');
      row.className = 'log-line';
      row.textContent = line;
      log.append(row);
    }
    log.scrollTop = log.scrollHeight;
  }
  async function refresh() {
    if (stopped) return;
    if (!paused && !document.hidden) {
      const selected = source.value;
      try {
        const response = await fetch(`logs-data.cgi?source=${selected}`, {cache: 'no-store', signal: AbortSignal.timeout(5000)});
        if (!response.ok) throw new Error(`HTTP ${response.status}`);
        const text = await response.text();
        if (selected === source.value && !paused) {
          snapshot = text;
          render();
          status.textContent = new Date().toLocaleTimeString();
        }
      } catch (error) { status.textContent = error.message; }
    }
    if (!stopped) setTimeout(refresh, 3000);
  }
  source.addEventListener('change', () => { snapshot = ''; cleared = new Set(); render(); });
  filter.addEventListener('input', render);
  pause.addEventListener('click', () => {
    paused = !paused;
    pause.textContent = paused ? 'Resume' : 'Pause';
    pause.setAttribute('aria-pressed', String(paused));
  });
  document.getElementById('log-clear').addEventListener('click', () => { cleared = new Set(snapshot.split('\n')); log.replaceChildren(); });
  document.getElementById('log-download').addEventListener('click', () => {
    const url = URL.createObjectURL(new Blob([snapshot], {type: 'text/plain'}));
    const anchor = document.createElement('a');
    anchor.href = url;
    anchor.download = `anyka-${source.value}.log`;
    anchor.click();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
  });
  window.addEventListener('pagehide', () => { stopped = true; });
  refresh();
})();