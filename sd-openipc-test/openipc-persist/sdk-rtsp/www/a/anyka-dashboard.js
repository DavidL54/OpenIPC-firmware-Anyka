const charts = window.MjCharts;
const theme = getComputedStyle(document.documentElement);
const primary = theme.getPropertyValue('--st-c1').trim() || '#4c60d8';
const secondary = theme.getPropertyValue('--st-c2').trim() || '#0d9488';
const cpuSpark = charts.makeSpark('#spark-cpu', primary, 0, 100);
const ramSpark = charts.makeSpark('#spark-ram', primary, 0, 100);
const netChart = charts.makeChart('#ch-net', {h:110, lo:0, hi:null, colors:[primary, secondary]});
const lumaChart = charts.makeChart('#ch-luma', {h:110, lo:0, hi:null, colors:[secondary]});
let previous = null;
const setText = (selector, value) => {document.querySelector(selector).textContent = value;};
setText('#st-temp', 'n/a');
document.getElementById('st-temp-u').hidden = true;
setText('#st-temp-note', 'Not reported');
setText('#st-daynight', 'Not reported');
setText('#st-prev-note', 'Preview');
const streams = document.getElementById('streams');
streams.replaceChildren();
for (const [name, path] of [['Main', 'vs0'], ['Sub', 'vs1']]) {
    const link = document.createElement('a');
    link.href = `rtsp://${location.hostname}:554/${path}`;
    link.textContent = `${name} / H.264`;
    link.className = 'small';
    streams.appendChild(link);
}

async function refresh() {
    try {
        const response = await fetch('metrics.cgi', {cache:'no-store', signal:AbortSignal.timeout(5000)});
        if (!response.ok) throw new Error('Metrics unavailable');
        const sample = await response.json();
        const hasTemperature = Number.isInteger(sample.temperatureMilliC);
        setText('#st-temp', hasTemperature ? (sample.temperatureMilliC / 1000).toFixed(1) : 'n/a');
        document.getElementById('st-temp-u').hidden = !hasTemperature;
        setText('#st-temp-note', hasTemperature ? 'SoC' : 'Not exposed by kernel');
        const now = performance.now();
        const memUsed = sample.memTotal - sample.memAvailable;
        const memPercent = 100 * memUsed / sample.memTotal;
        setText('#st-ram', memPercent.toFixed(0));
        setText('#st-ram-mb', `${(memUsed / 1048576).toFixed(1)} / ${(sample.memTotal / 1048576).toFixed(1)} MB`);
        setText('#st-load', sample.load.toFixed(2));
        const minutes = Math.floor(sample.uptime / 60);
        setText('#st-uptime', `${Math.floor(minutes / 1440)}d ${Math.floor(minutes / 60) % 24}h ${minutes % 60}m`);
        charts.pushSpark(ramSpark, memPercent);
        if (previous) {
            const total = sample.cpuTotal - previous.sample.cpuTotal;
            const idle = sample.cpuIdle - previous.sample.cpuIdle;
            if (total > 0) {
                const usage = Math.max(0, Math.min(100, 100 * (total - idle) / total));
                setText('#st-cpu', usage.toFixed(0));
                charts.pushSpark(cpuSpark, usage);
            }
            const elapsed = (now - previous.now) / 1000;
            charts.pushChart(netChart, [Math.max(0, sample.tx - previous.sample.tx) * 8 / elapsed / 1e6,
                Math.max(0, sample.rx - previous.sample.rx) * 8 / elapsed / 1e6]);
        }
        if (sample.wifiRssi !== null) {
            document.getElementById('st-wifi-tile').hidden = false;
            setText('#st-wifi-val', sample.wifiRssi);
            setText('#st-wifi-unit', ' dBm');
        }
        try {
            const statusResponse = await fetch('gpio.cgi?action=daynight-status', {
                cache:'no-store', signal:AbortSignal.timeout(3000)
            });
            if (!statusResponse.ok) throw new Error('ISP unavailable');
            const status = await statusResponse.json();
            const measured = Number.isInteger(status.lumi) && status.lumi >= 0;
            const entries = [];
            if (measured) entries.push(['Luminance', status.lumi]);
            if (typeof status.stable === 'boolean') entries.push(['Exposure stable', status.stable ? 'Yes' : 'No']);
            const list = document.getElementById('st-isp');
            list.replaceChildren();
            for (const [name, value] of entries) {
                const label = document.createElement('dt');
                const detail = document.createElement('dd');
                label.textContent = name;
                detail.textContent = value;
                list.append(label, detail);
            }
            document.getElementById('st-isp-empty').hidden = entries.length > 0;
            document.getElementById('st-luma-panel').hidden = !measured;
            if (measured) {
                setText('#st-luma-now', status.lumi);
                setText('#st-luma-note', 'SDK units');
                charts.pushChart(lumaChart, [status.lumi]);
            }
            const modes = {auto:'Auto', manual:'Manual', day:'Day', night:'Night'};
            const states = {day:'Day', night:'Night', unknown:'Unknown'};
            setText('#st-daynight', status.error || `${modes[status.mode] || 'Unknown'} / ${states[status.state] || 'Unknown'}`);
        } catch (error) {
            document.getElementById('st-isp').replaceChildren();
            document.getElementById('st-isp-empty').hidden = false;
            document.getElementById('st-luma-panel').hidden = true;
            setText('#st-daynight', 'ISP unavailable');
        }
        document.getElementById('st-alert-stale').hidden = true;
        document.getElementById('st-alerts').hidden = true;
        previous = {sample, now};
        charts.renderAll();
    } catch (error) {
        document.getElementById('st-alert-stale').hidden = false;
        document.getElementById('st-alerts').hidden = false;
    } finally {
        setTimeout(refresh, 5000);
    }
}
refresh();