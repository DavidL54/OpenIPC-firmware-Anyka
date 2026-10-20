class AnykaControlsCard extends HTMLElement {
    constructor() {
        super();
        this.attachShadow({mode: 'open'});
        this.talkSession = null;
        this.presets = null;
        this.presetBusy = false;
        this.onPageHide = () => this.stopTalk();
    }

    setConfig(config) {
        this.config = config;
        if (!this.shadowRoot.querySelector('ha-card')) this.render();
    }

    set hass(hass) {
        this._hass = hass;
        const privacy = hass.states['switch.camera_anyka_mode_prive'];
        if (privacy && this.element('privacy_mode')) {
            this.element('privacy_mode').checked = privacy.state === 'on';
            this.updateAudioControls();
        }
        this.updateMotionStatus();
        this.updateNativeStatus();
        if (!this.loaded && this.isConnected && this.config) {
            this.loaded = true;
            this.readStatus();
        }
    }

    connectedCallback() {
        window.addEventListener('pagehide', this.onPageHide);
        if (this._hass && !this.loaded) {
            this.loaded = true;
            this.readStatus();
        }
    }

    disconnectedCallback() {
        window.removeEventListener('pagehide', this.onPageHide);
        this.stopTalk();
        this.loaded = false;
    }

    getCardSize() { return 7; }

    render() {
        this.shadowRoot.innerHTML = `
            <style>
                :host { display:block; }
                ha-card { padding:16px; }
                h2 { font-size:18px; margin:0 0 16px; font-weight:500; }
                h3 { font-size:14px; margin:0 0 10px; font-weight:500; }
                .layout { display:grid; grid-template-columns:136px minmax(0,1fr); gap:24px; }
                .pad { display:grid; grid-template-columns:repeat(3,40px); gap:6px; }
                button { font:inherit; cursor:pointer; color:var(--primary-text-color); background:var(--secondary-background-color); border:1px solid var(--divider-color); border-radius:4px; min-height:40px; }
                .pad button { width:40px; height:40px; padding:0; display:grid; place-items:center; }
                button:hover { background:var(--divider-color); }
                button:disabled { opacity:.5; cursor:default; }
                .up { grid-column:2; }
                .left { grid-column:1; }
                .down { grid-column:2; }
                .settings { display:flex; flex-direction:column; gap:12px; min-width:0; }
                label { display:flex; align-items:center; gap:8px; font-size:14px; }
                select { flex:1; min-width:0; padding:8px; font:inherit; color:var(--primary-text-color); background:var(--card-background-color); border:1px solid var(--divider-color); border-radius:4px; }
                input[type=checkbox] { width:18px; height:18px; accent-color:var(--primary-color); }
                .presets { border-top:1px solid var(--divider-color); margin-top:16px; padding-top:16px; display:grid; gap:10px; }
                .presets input[type=text] { flex:1; min-width:0; padding:8px; color:var(--primary-text-color); background:var(--card-background-color); border:1px solid var(--divider-color); border-radius:4px; font:inherit; }
                #preset-stop { color:var(--error-color); }
                #preset-status { min-height:18px; margin:0; overflow-wrap:anywhere; }
                .talk { border-top:1px solid var(--divider-color); margin-top:20px; padding-top:16px; }
                .mp3 { margin-top:16px; display:grid; gap:10px; }
                input[type=file] { box-sizing:border-box; width:100%; min-width:0; color:var(--primary-text-color); font:inherit; }
                .hint { color:var(--secondary-text-color); font-size:12px; }
                .volume { display:flex; align-items:center; gap:12px; margin-bottom:12px; }
                input[type=range] { flex:1; min-width:0; accent-color:var(--primary-color); }
                output { min-width:52px; font-size:14px; text-align:right; }
                .actions { display:flex; flex-wrap:wrap; gap:8px; }
                .actions button { display:flex; align-items:center; justify-content:center; gap:8px; padding:8px 12px; }
                #talk { background:var(--primary-color); color:var(--text-primary-color,#fff); }
                #talk-stop { color:var(--error-color); }
                #status { font-size:14px; min-height:20px; margin:12px 0 0; overflow-wrap:anywhere; }
                #refresh { margin-left:auto; width:40px; padding:0; }
                details { margin-top:16px; border-top:1px solid var(--divider-color); padding-top:12px; }
                summary { cursor:pointer; padding:6px 0; }
                .advanced { display:grid; gap:12px; margin-top:12px; }
                .rect { display:grid; grid-template-columns:repeat(4,minmax(0,1fr)); gap:8px; }
                .rect label { display:block; font-size:12px; }
                .rect input { box-sizing:border-box; width:100%; padding:8px 4px; color:var(--primary-text-color); background:var(--card-background-color); border:1px solid var(--divider-color); border-radius:4px; }
                @media(max-width:400px) { .layout { grid-template-columns:1fr; gap:20px; } }
            </style>
            <ha-card>
                <h2>Commandes Anyka</h2>
                <div class="layout">
                    <div><h3>Orientation</h3><div class="pad">
                        <button class="up" data-action="up" title="Monter" aria-label="Monter"><ha-icon icon="mdi:arrow-up"></ha-icon></button>
                        <button class="left" data-action="left" title="Tourner a gauche" aria-label="Tourner a gauche"><ha-icon icon="mdi:arrow-left"></ha-icon></button>
                        <button data-action="stop" title="Arret moteur" aria-label="Arret moteur"><ha-icon icon="mdi:stop"></ha-icon></button>
                        <button data-action="right" title="Tourner a droite" aria-label="Tourner a droite"><ha-icon icon="mdi:arrow-right"></ha-icon></button>
                        <button class="down" data-action="down" title="Descendre" aria-label="Descendre"><ha-icon icon="mdi:arrow-down"></ha-icon></button>
                    </div></div>
                    <div class="settings">
                        <label><input type="checkbox" id="privacy_mode" disabled>Mode privé — afficher un écran fixe, couper micro et détection</label>
                        <span class="hint">Le mode normal revient au redémarrage de la caméra.</span>
                        <label>Mode <select id="mode"><option value="manual">Manuel</option><option value="day">Jour</option><option value="night">Nuit</option><option value="auto">Auto</option></select></label>
                        <label><input type="checkbox" id="ir">LED infrarouges</label>
                        <label><input type="checkbox" id="blue">LED d'etat</label>
                        <label><input type="checkbox" id="flip_vertical" disabled>Inverser verticalement</label>
                        <label><input type="checkbox" id="flip_horizontal" disabled>Inverser horizontalement</label>
                        <label><input type="checkbox" id="osd_clock" disabled>Afficher la date et l’heure</label>
                        <label><input type="checkbox" id="motion_enabled" disabled>Détection des mouvements</label>
                        <label>Sensibilité <input id="motion_sensitivity" type="range" min="1" max="100" step="1" value="50" disabled><output id="motion-level">50</output></label>
                        <span id="motion-status" role="status" aria-live="polite">Détection indisponible</span>
                    </div>
                </div>
                <section class="presets" aria-label="Positions mémorisées">
                    <h3>Positions mémorisées</h3>
                    <label>Emplacement <select id="preset-slot" disabled></select></label>
                    <label>Nom <input id="preset-name" type="text" maxlength="32" placeholder="Entrée, jardin…" disabled></label>
                    <div class="actions">
                        <button id="preset-save" disabled>Enregistrer ici</button>
                        <button id="preset-recall" disabled>Rejoindre</button>
                        <button id="preset-stop">Arrêt</button>
                        <button id="preset-delete" disabled>Supprimer</button>
                    </div>
                    <label>Au démarrage <select id="preset-startup" disabled></select></label>
                    <div class="actions"><button id="preset-startup-save" disabled>Enregistrer le choix</button></div>
                    <span class="hint">Positions conservées dans la caméra. Le choix de démarrage est appliqué après son calibrage.</span>
                    <p id="preset-status" class="hint" role="status" aria-live="polite"></p>
                </section>
                <details><summary>Détection et suivi</summary><div class="advanced">
                    <label><input type="checkbox" id="sound_enabled" disabled>Détection sonore</label>
                    <label>Sensibilité son <input id="sound_sensitivity" type="range" min="1" max="100" value="50"><output></output></label>
                    <label><input type="checkbox" id="person_enabled" disabled>Détection native de personnes</label>
                    <label><input type="checkbox" id="tracking_enabled" disabled>Suivi automatique</label>
                    <span class="hint">Le suivi nécessite la détection de mouvement. Une commande manuelle le suspend pendant 30 secondes.</span>
                    <span id="native-status" role="status"></span>
                    <h3>Zone de mouvement</h3>
                    <span class="hint">Position depuis le coin supérieur gauche, en pourcentage de l’image.</span>
                    <div class="rect">${['X','Y','Largeur','Hauteur'].map((name, i) => `<label>${name} %<input id="zone-${i}" type="number" min="0" max="100" value="${i < 2 ? 0 : 100}"></label>`).join('')}</div>
                    <div class="actions"><button id="zone-save">Appliquer la zone</button><button id="zone-reset">Toute l’image</button></div>
                </div></details>
                <details><summary>Image et confidentialité</summary><div class="advanced">
                    ${[['brightness','Luminosité'],['contrast','Contraste'],['saturation','Saturation'],['sharpness','Netteté'],['wdr','WDR'],['exposure','Compensation exposition']].map(([id,name]) => `<label>${name}<input id="${id}" type="range" min="-50" max="50" value="0"><output>0</output></label>`).join('')}
                    <label>Éclairage<select id="power_hz"><option value="50">50 Hz</option><option value="60">60 Hz</option></select></label>
                    <label>Antiscintillement<input id="anti_flicker" type="range" min="0" max="100" value="0"><output>0</output></label>
                    <label><input type="checkbox" id="white_balance_auto" disabled>Balance des blancs automatique</label>
                    ${[['white_balance_red','Rouge'],['white_balance_green','Vert'],['white_balance_blue','Bleu']].map(([id,name]) => `<label>Balance ${name}<input id="${id}" type="range" min="64" max="1024" value="256"><output>256</output></label>`).join('')}
                    <h3>Masques de confidentialité</h3>
                    <label>Masque<select id="mask-index">${[1,2,3,4].map(i => `<option value="${i}">${i}</option>`).join('')}</select></label>
                    <label>Aspect<select id="privacy_style"><option value="0">Opaque</option><option value="1">Mosaïque</option></select></label>
                    <div class="rect">${['X','Y','Largeur','Hauteur'].map((name, i) => `<label>${name} %<input id="mask-${i}" type="number" min="0" max="100" value="0"></label>`).join('')}</div>
                    <div class="actions"><button id="mask-save">Appliquer le masque</button><button id="mask-clear">Supprimer ce masque</button></div>
                    <span class="hint">Les masques s’appliquent aux flux de la caméra. Après une rotation, vérifiez la zone masquée.</span>
                </div></details>
                <div class="talk">
                    <label>Qualité audio<select id="audio-rate"><option value="8000">8 kHz avec annulation d’écho</option></select></label>
                    <label><input id="audio-nr" type="checkbox">Réduire le bruit du son diffusé</label>
                    <label class="volume">Volume <input id="volume" type="range" min="-30" max="0" step="1" value="0"><output id="volume-value">0 dB</output></label>
                    <div class="actions">
                        <button id="talk"><ha-icon icon="mdi:microphone"></ha-icon>Parler</button>
                        <button id="talk-stop" disabled><ha-icon icon="mdi:stop"></ha-icon>Arrêter</button>
                        <button id="refresh" title="Actualiser l'etat" aria-label="Actualiser l'etat"><ha-icon icon="mdi:refresh"></ha-icon></button>
                    </div>
                    <div class="mp3">
                        <label for="mp3">Fichier MP3</label>
                        <input id="mp3" type="file" accept=".mp3,audio/mpeg" aria-label="Choisir un fichier MP3">
                        <span class="hint">Décodage sur le serveur Home Assistant. 64 Mio et 4 min 50 s maximum.</span>
                        <div class="actions"><button id="file-play" disabled><ha-icon icon="mdi:play"></ha-icon>Diffuser le MP3</button></div>
                    </div>
                </div>
                <p id="status" role="status" aria-live="polite"></p>
            </ha-card>`;
        this.shadowRoot.querySelectorAll('[data-action]').forEach(button => {
            button.addEventListener('click', () => this.command(button.dataset.action));
        });
        this.element('mode').addEventListener('change', event => this.command(`mode-${event.target.value}`));
        for (const [id, prefix] of [['ir', 'ir'], ['blue', 'blue']]) {
            this.element(id).addEventListener('change', event => this.command(`${prefix}-${event.target.checked ? 'on' : 'off'}`));
        }
        for (const action of ['privacy_mode', 'flip_vertical', 'flip_horizontal', 'osd_clock', 'motion_enabled', 'sound_enabled', 'person_enabled', 'tracking_enabled', 'white_balance_auto']) {
            this.element(action).addEventListener('change', event => this.command(action, event.target.checked));
        }
        this.element('motion_sensitivity').addEventListener('input', event => {
            this.element('motion-level').value = event.target.value;
        });
        this.element('motion_sensitivity').addEventListener('change', event => {
            this.command('motion_sensitivity', Number(event.target.value));
        });
        for (const id of ['sound_sensitivity','brightness','contrast','saturation','sharpness','wdr','exposure','anti_flicker','white_balance_red','white_balance_green','white_balance_blue']) {
            this.element(id).addEventListener('input', event => { event.target.nextElementSibling.value = event.target.value; });
            this.element(id).addEventListener('change', event => this.command(id, Number(event.target.value)));
        }
        for (const id of ['power_hz','privacy_style']) this.element(id).addEventListener('change', event => this.command(id, Number(event.target.value)));
        this.element('zone-save').addEventListener('click', () => this.command('motion_zone', this.rectangle('zone')));
        this.element('zone-reset').addEventListener('click', () => this.command('motion_zone', [0,0,100,100]));
        this.element('mask-save').addEventListener('click', () => this.command(`privacy_mask${this.element('mask-index').value}`, this.rectangle('mask')));
        this.element('mask-clear').addEventListener('click', () => this.command(`privacy_mask${this.element('mask-index').value}`, [0,0,0,0]));
        this.element('mask-index').addEventListener('change', () => this.updateMaskEditor());
        this.element('volume').addEventListener('input', event => {
            this.element('volume-value').value = `${event.target.value} dB`;
        });
        this.element('talk').addEventListener('click', () => this.startTalk());
        this.element('talk-stop').addEventListener('click', () => this.stopTalk());
        this.element('mp3').addEventListener('change', () => {
            this.updateAudioControls();
            if (!this.talkSession) this.status(this.element('mp3').files[0]?.name || 'Choisir un fichier MP3');
        });
        this.element('file-play').addEventListener('click', () => this.startFile());
        this.element('preset-slot').addEventListener('change', () => this.presetSelection());
        for (const action of ['save','recall','delete','startup']) {
            this.element(action === 'startup' ? 'preset-startup-save' : `preset-${action}`).addEventListener('click', () => this.presetAction(action));
        }
        this.element('preset-stop').addEventListener('click', () => this.command('stop'));
        this.element('refresh').addEventListener('click', () => this.readStatus());
    }

    element(id) { return this.shadowRoot.getElementById(id); }
    rectangle(prefix) { return [0,1,2,3].map(i => Number(this.element(`${prefix}-${i}`).value)); }
    updateMaskEditor() {
        const mask = this.image?.privacy_masks?.[Number(this.element('mask-index').value)-1] || [0,0,0,0];
        mask.forEach((v,i) => { this.element(`mask-${i}`).value = v; });
    }
    status(message) { this.element('status').textContent = message; }

    updateAudioControls() {
        const active = Boolean(this.talkSession);
        const privateMode = this.element('privacy_mode')?.checked === true;
        this.element('talk').disabled = active || privateMode;
        this.element('talk-stop').disabled = !active;
        this.element('volume').disabled = active;
        this.element('audio-rate').disabled = active;
        this.element('audio-nr').disabled = active;
        this.element('mp3').disabled = active;
        this.element('file-play').disabled = active || privateMode || !this.element('mp3').files[0];
    }

    updateMotionStatus() {
        const output = this.element('motion-status');
        if (!output) return;
        const state = this._hass?.states[this.config?.motion_entity || 'binary_sensor.camera_anyka_mouvement'];
        output.textContent = this.element('privacy_mode')?.checked ? 'Mode privé : détection suspendue' :
            !state || ['unavailable','unknown'].includes(state.state) ? 'Détection indisponible' :
            state.attributes.enabled === false ? 'Détection désactivée' :
            state.attributes.suspended ? 'Détection suspendue pendant la stabilisation' :
            state.state === 'on' ? 'Mouvement détecté' : 'Aucun mouvement';
    }

    updateNativeStatus(image) {
        const output = this.element('native-status');
        if (!output) return;
        const state = kind => this._hass?.states[this.config?.[kind+'_entity'] || 'binary_sensor.camera_anyka_'+(kind === 'sound' ? 'son' : 'personne')];
        const describe = (kind, idle, detected) => {
            const entity = image ? null : state(kind);
            const data = image || entity?.attributes;
            const enabled = image ? data?.[kind+'_enabled'] : data?.enabled;
            const ready = image ? data?.[kind+'_ready'] : data?.ready;
            const error = image ? data?.[kind+'_error'] : data?.error;
            const active = image ? data?.[kind+'_detected'] : entity?.state === 'on';
            return enabled === false ? idle+' désactivée' : error || (enabled === true ? (ready ? (active ? detected : 'Aucune détection '+idle.toLowerCase()) : 'Initialisation '+idle.toLowerCase()+'…') : idle+' indisponible');
        };
        const tracking = image?.tracking_enabled ?? this._hass?.states['switch.camera_anyka_suivi_automatique']?.state === 'on';
        output.textContent = [describe('sound','Sonore','Son détecté'), describe('person','Personne','Personne détectée'), image?.tracking_error || (tracking ? 'Suivi activé' : 'Suivi désactivé')].join(' · ');
    }

    async readStatus() {
        try {
            const data = await this._hass.callApi('GET', 'anyka_controls/status');
            this.presets = data.presets || null;
            this.renderPresets();
            if (data.gpio) {
                this.element('ir').checked = Boolean(data.gpio.infrared);
                this.element('blue').checked = Boolean(data.gpio.blue);
            }
            if (data.daynight?.mode) this.element('mode').value = data.daynight.mode;
            const image = data.image?.image || data.image;
            this.image = image;
            for (const action of ['privacy_mode', 'flip_vertical', 'flip_horizontal', 'osd_clock', 'motion_enabled', 'sound_enabled', 'person_enabled', 'tracking_enabled', 'white_balance_auto']) {
                const value = image?.[action];
                this.element(action).disabled = ![true, false, 0, 1].includes(value);
                this.element(action).checked = value === true || value === 1;
            }
            for (const id of ['sound_sensitivity','brightness','contrast','saturation','sharpness','wdr','exposure','anti_flicker','white_balance_red','white_balance_green','white_balance_blue','power_hz','privacy_style']) {
                this.element(id).disabled = !Number.isInteger(image?.[id]);
                if (!this.element(id).disabled) {
                    this.element(id).value = image[id];
                    if (this.element(id).nextElementSibling?.tagName === 'OUTPUT') this.element(id).nextElementSibling.value = image[id];
                }
            }
            (image?.motion_zone || [0,0,100,100]).forEach((v,i) => { this.element(`zone-${i}`).value = v; });
            this.updateMaskEditor();
            this.updateNativeStatus(image);
            const sensitivity = image?.motion_sensitivity;
            this.element('motion_sensitivity').disabled = !Number.isInteger(sensitivity) || sensitivity < 1 || sensitivity > 100;
            if (!this.element('motion_sensitivity').disabled) {
                this.element('motion_sensitivity').value = sensitivity;
                this.element('motion-level').value = sensitivity;
            }
            this.updateMotionStatus();
            this.updateAudioControls();
            if (!this.talkSession) this.status(image?.privacy_mode ? 'Mode privé activé' : data.gpio ? 'Camera connectee' : 'Camera indisponible');
        } catch (error) {
            this.status(error.message || 'Etat indisponible');
        }
    }

    presetSelection() {
        const selected = this.presets?.positions?.find(p => p.id === Number(this.element('preset-slot').value));
        this.element('preset-name').value = selected?.name || '';
        this.element('preset-slot').disabled = this.presetBusy || !this.presets;
        this.element('preset-name').disabled = this.presetBusy || !this.presets;
        this.element('preset-save').disabled = this.presetBusy || !this.presets?.homed;
        this.element('preset-recall').disabled = this.presetBusy || !this.presets?.homed || !selected;
        this.element('preset-delete').disabled = this.presetBusy || !selected;
        this.element('preset-startup').disabled = this.presetBusy || !this.presets;
        this.element('preset-startup-save').disabled = this.presetBusy || !this.presets;
    }

    renderPresets() {
        const old = this.element('preset-slot').value || '1';
        this.element('preset-slot').replaceChildren();
        for (let id=1; id<=8; id++) {
            const preset = this.presets?.positions?.find(p => p.id === id);
            this.element('preset-slot').add(new Option(`${id} — ${preset?.name || 'Libre'}`, id));
        }
        this.element('preset-slot').value = old;
        this.element('preset-startup').replaceChildren(new Option('Centre après calibrage', '0'));
        for (const p of [...(this.presets?.positions || [])].sort((a,b) => a.id-b.id)) this.element('preset-startup').add(new Option(`${p.id} — ${p.name}`, p.id));
        this.element('preset-startup').value = String(this.presets?.startup || 0);
        this.presetSelection();
        if (!this.presets) this.element('preset-status').textContent = 'Positions indisponibles — actualise la carte.';
    }

    async presetAction(action) {
        if (this.presetBusy || !this.presets) return;
        const id = Number(this.element(action === 'startup' ? 'preset-startup' : 'preset-slot').value);
        const name = this.element('preset-name').value.trim();
        const say = text => { this.element('preset-status').textContent = text; };
        if (action === 'save' && (!name || new TextEncoder().encode(name).length > 48)) { say('Choisis un nom court (48 octets maximum).'); return; }
        if (action === 'delete' && !confirm('Supprimer cette position ?')) return;
        if (action === 'save' && this.presets.positions.some(p => p.id === id) && !confirm('Remplacer cette position par la position actuelle ?')) return;
        this.presetBusy = true; this.presetSelection();
        say(action === 'recall' ? 'Déplacement en cours… Arrêt reste disponible.' : 'Enregistrement…');
        try {
            const result = await this._hass.callApi('POST', 'anyka_controls/presets', {action, id, ...(action === 'save' ? {name} : {})});
            if (result.ok === false) throw new Error(result.error || 'Commande refusée');
            this.presets = await this._hass.callApi('GET', 'anyka_controls/presets');
            this.renderPresets(); say(action === 'recall' ? 'Position atteinte.' : 'Choix enregistré dans la caméra.');
        } catch (error) {
            say(error.body?.error || error.message || 'Commande indisponible — actualise l’état avant de réessayer.');
        } finally {
            this.presetBusy = false; this.presetSelection();
        }
    }

    async command(action, value) {
        try {
            const result = await this._hass.callApi('POST', 'anyka_controls/command', {action, value});
            if (result.ok === false) throw new Error(result.error || 'Commande refusee');
            await this.readStatus();
        } catch (error) {
            this.status(error.message || 'Commande refusee');
        }
    }

    async startTalk() {
        if (this.talkSession) return;
        if (!window.isSecureContext || !navigator.mediaDevices?.getUserMedia) {
            this.status('Ouvrir Home Assistant en HTTPS pour utiliser le micro');
            return;
        }
        const session = {cancelled: false, kind: 'microphone', rate: Number(this.element('audio-rate').value)};
        this.talkSession = session;
        this.updateAudioControls();
        this.status('Connexion du micro...');
        try {
            session.context = new AudioContext({sampleRate: session.rate, latencyHint: 'interactive'});
            if (session.context.sampleRate !== session.rate) throw new Error('Fréquence audio indisponible');
            await session.context.resume();
            session.microphone = await navigator.mediaDevices.getUserMedia({audio: {
                channelCount: 1, echoCancellation: true, noiseSuppression: true, autoGainControl: true,
            }});
            if (session.cancelled) {
                session.microphone.getTracks().forEach(track => track.stop());
                return;
            }
            await session.context.audioWorklet.addModule('/local/anyka-microphone.js');
            if (session.cancelled) return;
            await this.openSpeaker(session, () => {
                session.source = session.context.createMediaStreamSource(session.microphone);
                session.recorder = new AudioWorkletNode(session.context, 'anyka-microphone', {
                    channelCount: 1, channelCountMode: 'explicit', outputChannelCount: [1],
                    processorOptions: {streamPCM: true},
                });
                session.recorder.port.onmessage = ({data}) => {
                    if (session.cancelled || session.socket.readyState !== WebSocket.OPEN) return;
                    if (session.socket.bufferedAmount > 4096) {
                        this.stopTalk('Connexion audio trop lente');
                        return;
                    }
                    session.socket.send(data);
                };
                session.recorder.onprocessorerror = () => this.stopTalk('Erreur de capture micro');
                session.source.connect(session.recorder);
                session.recorder.connect(session.context.destination);
                session.timer = setTimeout(() => this.stopTalk(), 290000);
                this.status('Micro actif');
            });
        } catch (error) {
            if (!session.cancelled) this.stopTalk(error.message || 'Micro indisponible');
        }
    }

    async openSpeaker(session, onReady) {
        const signed = await this._hass.callWS({type: 'anyka_controls/audio_url', volume: Number(this.element('volume').value), rate: session.rate, nr: this.element('audio-nr').checked});
        if (session.cancelled) return;
        const target = new URL(signed.url, location.origin);
        target.protocol = location.protocol === 'https:' ? 'wss:' : 'ws:';
        session.socket = new WebSocket(target.href);
        session.timer = setTimeout(() => {
            if (!session.cancelled) this.stopTalk('Connexion haut-parleur expirée');
        }, 12000);
        session.socket.addEventListener('message', event => {
            if (session.cancelled) return;
            try {
                const message = JSON.parse(event.data);
                if (message.type === 'error') throw new Error(message.value);
                if (message.type !== 'ready' || session.ready) throw new Error('Réponse audio invalide');
                session.ready = true;
                clearTimeout(session.timer);
                onReady();
            } catch (error) {
                this.stopTalk(error.message);
            }
        });
        session.socket.addEventListener('close', () => {
            if (!session.cancelled) this.stopTalk('Haut-parleur déconnecté');
        });
        session.socket.addEventListener('error', () => {
            if (!session.cancelled) this.stopTalk('Haut-parleur indisponible');
        });
    }

    async startFile() {
        const file = this.element('mp3').files[0];
        if (this.talkSession || !file) return;
        const session = {cancelled: false, kind: 'file', file, offset: 0};
        this.talkSession = session;
        this.updateAudioControls();
        this.status('Connexion au serveur audio…');
        try {
            if (!/\.mp3$/i.test(file.name)) throw new Error('Choisir un fichier MP3');
            if (!file.size) throw new Error('Fichier MP3 vide');
            if (file.size > 64 * 1024 * 1024) throw new Error('Le MP3 dépasse 64 Mio');
            const signed = await this._hass.callWS({type: 'anyka_controls/mp3_url', volume: Number(this.element('volume').value), nr: this.element('audio-nr').checked});
            if (session.cancelled) return;
            const target = new URL(signed.url, location.origin);
            target.protocol = location.protocol === 'https:' ? 'wss:' : 'ws:';
            session.socket = new WebSocket(target.href);
            session.timer = setTimeout(() => this.stopTalk('Connexion au serveur expirée'), 12000);
            session.socket.addEventListener('message', async event => {
                if (session.cancelled) return;
                try {
                    const message = JSON.parse(event.data);
                    if (message.type === 'error') throw new Error(message.value);
                    if (message.type === 'upload_ready' || message.type === 'upload_credit') {
                        clearTimeout(session.timer);
                        await this.sendMp3Chunk(session);
                    } else if (message.type === 'ready') {
                        session.duration = message.duration;
                        session.timer = setTimeout(() => this.stopTalk('Durée maximale de lecture atteinte'), 295000);
                        this.status(`Lecture MP3 sur la caméra : 0 / ${Math.ceil(message.duration)} s`);
                    } else if (message.type === 'progress') {
                        this.status(`Lecture MP3 sur la caméra : ${message.seconds} / ${Math.ceil(session.duration)} s`);
                    } else if (message.type === 'ended') {
                        this.stopTalk('Lecture MP3 terminée');
                    } else {
                        throw new Error('Réponse audio invalide');
                    }
                } catch (error) {
                    this.stopTalk(error.message || 'Lecture MP3 indisponible');
                }
            });
            session.socket.addEventListener('close', () => {
                if (!session.cancelled) this.stopTalk('Connexion au serveur audio interrompue');
            });
            session.socket.addEventListener('error', () => {
                if (!session.cancelled) this.stopTalk('Serveur audio indisponible');
            });
        } catch (error) {
            if (!session.cancelled) this.stopTalk(error.message || 'Lecture MP3 indisponible');
        }
    }

    async sendMp3Chunk(session) {
        if (session.cancelled || session.socket.readyState !== WebSocket.OPEN) return;
        if (session.offset >= session.file.size) {
            if (!session.uploadEnded) {
                session.uploadEnded = true;
                session.socket.send('upload_end');
                this.status('Conversion du MP3 sur le serveur…');
            }
            return;
        }
        const end = Math.min(session.offset + 32768, session.file.size);
        const chunk = await session.file.slice(session.offset, end).arrayBuffer();
        if (session.cancelled || session.socket.readyState !== WebSocket.OPEN) return;
        session.socket.send(chunk);
        session.offset = end;
        this.status(`Envoi MP3 au serveur : ${Math.round(100 * end / session.file.size)} %`);
    }

    stopTalk(message) {
        const session = this.talkSession;
        message ??= session?.kind === 'file' ? 'Lecture MP3 arrêtée' : 'Micro arrêté';
        if (session) {
            session.cancelled = true;
            clearTimeout(session.timer);
            session.recorder?.port.close();
            session.recorder?.disconnect();
            session.source?.disconnect();
            session.socket?.close();
            session.microphone?.getTracks().forEach(track => track.stop());
            session.context?.close().catch(() => {});
            session.file = null;
            this.talkSession = null;
        }
        if (this.element('talk')) {
            this.updateAudioControls();
            this.status(message);
        }
    }
}

if (!customElements.get('anyka-controls-card')) customElements.define('anyka-controls-card', AnykaControlsCard);
window.customCards = window.customCards || [];
window.customCards.push({type: 'anyka-controls-card', name: 'Commandes Anyka', preview: false});

// Le mode panneau étire la grille native à la hauteur de l'écran. Ce conteneur
// conserve sa largeur et ses cartes natives, avec une hauteur liée au contenu.
class AnykaCameraGridCard extends HTMLElement {
    constructor() {
        super();
        this.attachShadow({mode: 'open'});
        this.shadowRoot.innerHTML = '<style>:host{display:block}hui-grid-card{height:auto}</style>';
    }

    setConfig(config) {
        if (!Array.isArray(config.cards) || !config.cards.length) throw new Error('Ajouter des cartes à la grille');
        this.config = config;
        this.loadGrid(config).catch(() => {
            if (this.config === config) this.shadowRoot.append(document.createTextNode('Grille indisponible'));
        });
    }

    async loadGrid(config) {
        const helpers = await window.loadCardHelpers();
        if (this.config !== config) return;
        this.grid?.remove();
        this.grid = helpers.createCardElement({type: 'grid', columns: 2, square: false, cards: config.cards});
        this.grid.hass = this._hass;
        this.shadowRoot.append(this.grid);
    }

    set hass(hass) {
        this._hass = hass;
        if (this.grid) this.grid.hass = hass;
    }

    getCardSize() { return 6; }
}

if (!customElements.get('anyka-camera-grid')) customElements.define('anyka-camera-grid', AnykaCameraGridCard);
window.customCards.push({type: 'anyka-camera-grid', name: 'Grille de caméras', preview: false});
