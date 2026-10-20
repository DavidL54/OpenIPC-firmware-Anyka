const el = id => document.getElementById(id);
let state = {positions: [], startup: 0}, busy = false;
const say = text => { el('ptz-presets-status').textContent = text; };
function selected() { return state.positions.find(p => p.id === Number(el('ptz-slot').value)); }
function selection() {
    const p = selected(); el('ptz-name').value = p?.name || '';
    el('ptz-recall').disabled = busy || !p || !state.homed;
    el('ptz-delete').disabled = busy || !p;
    el('ptz-save').disabled = busy || !state.homed;
}
function render() {
    const old = el('ptz-slot').value || '1';
    el('ptz-slot').replaceChildren();
    for (let id = 1; id <= 8; id++) {
        const p = state.positions.find(p => p.id === id);
        el('ptz-slot').add(new Option(`${id} — ${p?.name || 'Libre'}`, id));
    }
    el('ptz-slot').value = old;
    el('ptz-startup').replaceChildren(new Option('Centre après calibrage', '0'));
    for (const p of [...state.positions].sort((a,b) => a.id-b.id)) el('ptz-startup').add(new Option(`${p.id} — ${p.name}`, p.id));
    el('ptz-startup').value = String(state.startup); selection();
}
async function request(action, id, name) {
    const options = {cache: 'no-store', credentials: 'same-origin', signal: AbortSignal.timeout(action === 'recall' ? 70000 : 10000)};
    if (action) {
        options.method = 'POST'; options.headers = {'Content-Type': 'application/x-www-form-urlencoded'};
        options.body = new URLSearchParams({action, id, ...(name === undefined ? {} : {name})});
    }
    const response = await fetch('/api/ptz/presets', options), data = await response.json();
    if (!response.ok) throw new Error(data.error || 'Commande refusée');
    return data;
}
async function load() { state = await request(); render(); }
async function act(action) {
    if (busy) return;
    const id = action === 'startup' ? el('ptz-startup').value : el('ptz-slot').value;
    const name = el('ptz-name').value.trim();
    if (action === 'save' && (!name || new TextEncoder().encode(name).length > 48)) { say('Choisis un nom court (48 octets maximum).'); return; }
    if (action === 'delete' && !confirm('Supprimer cette position ?')) return;
    if (action === 'save' && selected() && !confirm('Remplacer cette position par la position actuelle ?')) return;
    busy = true; selection(); el('ptz-startup-save').disabled = true;
    say(action === 'recall' ? 'Déplacement en cours… Arrêt reste disponible.' : 'Enregistrement…');
    try {
        await request(action, id, action === 'save' ? name : undefined);
        await load(); say(action === 'recall' ? 'Position atteinte.' : 'Choix enregistré dans la caméra.');
    } catch (error) { say(error.message); }
    finally { busy = false; selection(); el('ptz-startup-save').disabled = false; }
}
el('ptz-slot').addEventListener('change', selection);
for (const action of ['save','recall','delete']) el(`ptz-${action}`).addEventListener('click', () => act(action));
el('ptz-startup-save').addEventListener('click', () => act('startup'));
el('ptz-stop').addEventListener('click', async () => {
    try { const r = await fetch('/cgi-bin/ptz.cgi?action=stop', {method:'POST', signal:AbortSignal.timeout(5000)}); if (!r.ok) throw new Error('Arrêt indisponible'); say('Arrêt demandé.'); }
    catch (e) { say(e.message); }
});
load().catch(e => say(e.message));
