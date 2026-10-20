(() => {
    const imageNames = {brightness:'Brightness', contrast:'Contrast', saturation:'Saturation', sharpness:'Sharpness', hue:'Hue', flip_vertical:'Vertical flip', flip_horizontal:'Horizontal flip', osd_clock:'Afficher la date et l’heure (Europe/Paris)', motion_enabled:'Détection des mouvements', motion_sensitivity:'Sensibilité des mouvements'};
    const isFlip = name => ['flip_vertical','flip_horizontal','osd_clock','motion_enabled'].includes(name);
    const validImageValue = (name, value) => Object.hasOwn(imageNames, name) &&
        (isFlip(name) ? typeof value === 'boolean' : Number.isInteger(value) &&
            (name === 'motion_sensitivity' ? value >= 1 && value <= 100 : value >= -50 && value <= 50));
    async function imageConfig() {
        try {
            const response = await fetch('isp.cgi', {cache:'no-store', signal:AbortSignal.timeout(4000)});
            if (!response.ok) return {};
            const result = await response.json();
            return Object.fromEntries(Object.keys(imageNames).filter(name => validImageValue(name, result[name])).map(name => [name, result[name]]));
        } catch (error) { return {}; }
    }
    const schema = {
        type:'object',
        'x-groups':[{id:'camera', label:'Camera', sections:['image','night','lights']}],
        properties:{night:{type:'object', title:'Day / Night', properties:{
            mode:{type:'string', title:'Mode', enum:['auto','day','night','manual'],
                'x-enum-labels':['Auto','Day','Night','Manual'], 'x-reload':'none'}
        }}, lights:{type:'object', title:'Lights', properties:{
            infrared:{type:'boolean', title:'Infrared LED', 'x-reload':'none'},
            blue:{type:'boolean', title:'Blue status LED', 'x-reload':'none'}
        }}}
    };
    const jsonResponse = (value, status = 200) => new Response(JSON.stringify(value), {
        status, headers:{'Content-Type':'application/json'}
    });
    async function config() {
        const responses = await Promise.all([
            fetch('gpio.cgi?action=status', {cache:'no-store'}),
            fetch('gpio.cgi?action=mode-status', {cache:'no-store'})
        ]);
        if (responses.some(response => !response.ok)) throw new Error('Camera controls unavailable');
        const [result, mode] = await Promise.all(responses.map(response => response.json()));
        return {image:await imageConfig(), night:{mode:mode.mode}, lights:{infrared:result.infrared, blue:result.blue}};
    }
    window.mjConfig = config;
    window.apiFetch = async (path, options = {}) => {
        if (path === '/api/v1/config.schema.json') {
            const values = await imageConfig();
            const properties = Object.fromEntries(Object.keys(values).map(name => [name, isFlip(name) ? {
                type:'boolean', title:imageNames[name], 'x-reload':'none'
            } : {
                type:'integer', title:imageNames[name], minimum:name === 'motion_sensitivity' ? 1 : -50,
                maximum:name === 'motion_sensitivity' ? 100 : 50, default:name === 'motion_sensitivity' ? 50 : 0
            }]));
            return jsonResponse({...schema, properties:{...schema.properties,
                ...(Object.keys(properties).length ? {image:{type:'object', title:'Image', properties}} : {})
            }});
        }
        if (path === '/api/v1/config.json') return jsonResponse(await config());
        if (path !== '/api/v1/config' || options.method !== 'POST') {
            return jsonResponse({error:'Unsupported Anyka operation'}, 404);
        }
        let changes;
        try { changes = JSON.parse(options.body); }
        catch (error) { return jsonResponse({error:'Invalid settings'}, 400); }
        if (!changes || typeof changes !== 'object' || Array.isArray(changes) ||
            Object.keys(changes).some(key => !['image','lights','night'].includes(key)) ||
            Object.values(changes).some(value => !value || typeof value !== 'object' || Array.isArray(value))) {
            return jsonResponse({error:'Invalid settings'}, 400);
        }
        if (changes.night && (Object.keys(changes.night).some(key => key !== 'mode') ||
            !['auto','day','night','manual'].includes(changes.night.mode))) {
            return jsonResponse({error:'Invalid day/night mode'}, 400);
        }
        const imageChanges = Object.fromEntries(Object.entries(changes.image || {}).map(([name, value]) => {
            if (isFlip(name) && [0, 1, '0', '1', 'true', 'false'].includes(value)) {
                value = value === 1 || value === '1' || value === 'true';
            } else if (!isFlip(name) && typeof value === 'string' && /^-?(0|[1-9][0-9]{0,2})$/.test(value)) {
                value = Number(value);
            }
            return [name, value];
        }));
        if (Object.entries(imageChanges).some(([name, value]) => !validImageValue(name, value))) {
            return jsonResponse({error:'Invalid image setting'}, 400);
        }
        if (changes.night?.mode && changes.night.mode !== 'manual' &&
            Object.hasOwn(changes.lights || {}, 'infrared')) {
            return jsonResponse({error:'Select manual mode to change the infrared LED'}, 400);
        }
        const actions = {infrared:'ir', blue:'blue'};
        for (const [name, value] of Object.entries(changes.lights || {})) {
            if (!Object.hasOwn(actions, name) || ![true, false, 0, 1, '0', '1', 'true', 'false'].includes(value)) {
                return jsonResponse({error:'Invalid light value'}, 400);
            }
        }
        if (changes.night) {
            const response = await fetch(`gpio.cgi?action=mode-${changes.night.mode}`, {method:'POST'});
            if (!response.ok) return response;
        }
        for (const [name, value] of Object.entries(imageChanges)) {
            const encoded = isFlip(name) ? Number(value) : value;
            const response = await fetch(`isp.cgi?${name}=${encoded}`, {method:'POST'});
            if (!response.ok) return response;
        }
        for (const [name, value] of Object.entries(changes.lights || {})) {
            const enabled = [true, 1, '1', 'true'].includes(value);
            const action = `${actions[name]}-${enabled ? 'on' : 'off'}`;
            const response = await fetch(`gpio.cgi?action=${action}`, {method:'POST'});
            if (!response.ok) return response;
        }
        return jsonResponse(await config());
    };
})();
