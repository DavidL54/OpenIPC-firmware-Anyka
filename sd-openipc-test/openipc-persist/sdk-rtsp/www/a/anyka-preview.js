import {VideoRTC} from './video-rtc.js';
import wavEncoder from './wav-encoder.js';

class AnykaVideo extends VideoRTC {
    onconnect() {
        return this.talking ? false : super.onconnect();
    }

    oninit() {
        this.video = document.getElementById('live-video');
        this.video.addEventListener('loadeddata', () => {
            document.getElementById('mj-badge').textContent = 'H.264 / MSE';
        });
    }
}

customElements.define('anyka-video', AnykaVideo);
const player = document.createElement('anyka-video');
player.mode = 'mse';
player.media = 'video,audio';
player.mseTargetLatency = 0.4;
player.mseMaxLatency = 1.5;
player.visibilityCheck = true;
player.style.display = 'none';
document.getElementById('mj-player').appendChild(player);
player.src = `${location.protocol === 'https:' ? 'wss' : 'ws'}://${location.host}/api/ws?src=preview`;

const stage = document.getElementById('mj-stage');
stage.addEventListener('pointerdown', (event) => {
    if (event.pointerType === 'touch' && event.target === player.video) stage.classList.toggle('mj-show');
});
const pad = document.getElementById('mj-ptz-pad');
const slot = document.getElementById('mj-ptz');
slot.appendChild(pad);
slot.hidden = false;
pad.hidden = false;
const status = document.createElement('p');
status.className = 'mj-adapt-toast small';
status.setAttribute('role', 'status');
status.hidden = true;
document.getElementById('mj-toasts').appendChild(status);
let moving = false;
pad.addEventListener('click', async (event) => {
    const button = event.target.closest('button[data-act]');
    if (!button || (moving && button.dataset.act !== 'stop')) return;
    moving = button.dataset.act !== 'stop';
    status.hidden = true;
    try {
        const response = await fetch(`ptz.cgi?action=${button.dataset.act}`, {method: 'POST'});
        const result = await response.json();
        if (!response.ok || !result.ok) throw new Error(result.error || 'PTZ unavailable');
    } catch (error) {
        status.textContent = error.message;
        status.hidden = false;
    } finally {
        moving = false;
    }
});
document.querySelector('label[for="mj-stream-0"]').textContent = 'Sub';
document.getElementById('mj-stream-0').disabled = true;
const fullscreen = document.getElementById('mj-fs');
fullscreen.hidden = false;
fullscreen.addEventListener('click', () => {
    const operation = document.fullscreenElement ? document.exitFullscreen() : stage.requestFullscreen();
    operation.catch(error => {
        status.textContent = error.message;
        status.hidden = false;
    });
});

const listen = document.getElementById('mj-mute');
const listenVolume = document.getElementById('mj-vol');
document.getElementById('mj-audio-ctl').hidden = false;
listenVolume.disabled = false;
listen.addEventListener('change', () => {
    player.video.muted = !listen.checked;
    document.getElementById('mj-mute-t').textContent = listen.checked ? 'Audio' : 'Muted';
    if (listen.checked) {
        player.video.play().catch(error => {
            listen.checked = false;
            player.video.muted = true;
            document.getElementById('mj-mute-t').textContent = 'Muted';
            status.textContent = error.message;
            status.hidden = false;
        });
    }
});
listenVolume.addEventListener('input', event => {
    event.stopImmediatePropagation();
    player.video.volume = Number(listenVolume.value) / 100;
}, true);

const vocalForm = document.getElementById('anyka-vocal-form');
const vocalFile = document.getElementById('anyka-vocal-file');
const vocalVolume = document.getElementById('anyka-vocal-volume');
const vocalPlay = document.getElementById('anyka-vocal-play');
const vocalStop = document.getElementById('anyka-vocal-stop');
const vocalStatus = document.getElementById('anyka-vocal-status');
const record = document.getElementById('anyka-vocal-record');
const finish = document.getElementById('anyka-vocal-finish');
const recordingPreview = document.getElementById('anyka-recording-preview');
const recordingAudio = document.getElementById('anyka-recording-audio');
const talk = document.getElementById('anyka-talk');
const talkStop = document.getElementById('anyka-talk-stop');
let vocalOperation = null;
let recordedAudio = null;
let recordingURL = null;
let recorder = null;
let recordingContext = null;
let recordingSource = null;
let recordingSamples = [];
let recordingFrames = 0;
let microphone = null;
let recordingTimer = null;
let recordingPending = false;
let pageClosing = false;
let talkSession = null;

function stopTalk(message = 'Talk stopped') {
    const session = talkSession;
    if (!session) return;
    talkSession = null;
    session.cancelled = true;
    clearTimeout(session.timer);
    session.recorder?.port.close();
    session.recorder?.disconnect();
    session.source?.disconnect();
    session.microphone?.getTracks().forEach(track => track.stop());
    session.context?.close().catch(() => {});
    session.socket?.close();
    talk.disabled = false;
    talkStop.disabled = true;
    record.disabled = false;
    vocalFile.disabled = false;
    vocalVolume.disabled = false;
    vocalPlay.disabled = !(recordedAudio || vocalFile.files.length);
    listen.disabled = false;
    listen.checked = session.listening;
    player.video.muted = !listen.checked;
    document.getElementById('mj-mute-t').textContent = listen.checked ? 'Audio' : 'Muted';
    vocalStatus.textContent = message;
    player.talking = false;
    if (!pageClosing) player.onconnect();
}

talk.addEventListener('click', async () => {
    if (talkSession || vocalOperation || recorder || recordingPending) return;
    vocalStatus.hidden = false;
    if (!window.isSecureContext || !navigator.mediaDevices?.getUserMedia || !window.AudioWorkletNode) {
        vocalStatus.textContent = 'Microphone unavailable: use a secure browser context.';
        return;
    }
    const session = {cancelled: false, listening: listen.checked};
    talkSession = session;
    talk.disabled = true;
    talkStop.disabled = false;
    record.disabled = true;
    vocalFile.disabled = true;
    vocalVolume.disabled = true;
    vocalPlay.disabled = true;
    recordingAudio.pause();
    listen.disabled = true;
    listen.checked = false;
    player.video.muted = true;
    document.getElementById('mj-mute-t').textContent = 'Muted';
    player.talking = true;
    clearTimeout(player.reconnectTID);
    player.reconnectTID = 0;
    player.ondisconnect();
    vocalStatus.textContent = 'Waiting for microphone permission...';
    try {
        session.context = new AudioContext({sampleRate: 8000, latencyHint: 'interactive'});
        if (session.context.sampleRate !== 8000) throw new Error('Live PCM sample rate is unavailable');
        await session.context.resume();
        if (session.cancelled) return;
        const microphone = await navigator.mediaDevices.getUserMedia({audio: {
            channelCount: 1, echoCancellation: true, noiseSuppression: true, autoGainControl: true,
        }});
        if (session.cancelled) {
            microphone.getTracks().forEach(track => track.stop());
            return;
        }
        session.microphone = microphone;
        await session.context.audioWorklet.addModule('/a/anyka-microphone.js');
        if (session.cancelled) return;
        const socket = new WebSocket(`${location.protocol === 'https:' ? 'wss' : 'ws'}://${location.host}/api/audio/live?volume=${vocalVolume.value}`);
        session.socket = socket;
        session.timer = setTimeout(() => stopTalk('Speaker connection timed out'), 12000);
        socket.addEventListener('message', event => {
            if (session.cancelled) return;
            try {
                const message = JSON.parse(event.data);
                if (message.type === 'error') {
                    stopTalk(message.value);
                    return;
                }
                if (message.type !== 'ready' || session.recorder) throw new Error('Invalid speaker response');
                clearTimeout(session.timer);
                session.source = session.context.createMediaStreamSource(microphone);
                session.recorder = new AudioWorkletNode(session.context, 'anyka-microphone', {
                    channelCount: 1, channelCountMode: 'explicit', outputChannelCount: [1],
                    processorOptions: {streamPCM: true},
                });
                session.recorder.port.onmessage = ({data}) => {
                    if (session.cancelled || socket.readyState !== WebSocket.OPEN) return;
                    if (socket.bufferedAmount > 4096) {
                        stopTalk('Audio connection is too slow');
                        return;
                    }
                    socket.send(data);
                };
                session.recorder.onprocessorerror = () => stopTalk('Microphone capture failed');
                session.source.connect(session.recorder);
                session.recorder.connect(session.context.destination);
                session.timer = setTimeout(() => stopTalk('Talk session finished'), 290000);
                vocalStatus.textContent = 'Talking...';
            } catch (error) {
                stopTalk(error.message);
            }
        });
        socket.addEventListener('close', () => {
            if (!session.cancelled) stopTalk('Live speaker disconnected');
        });
        socket.addEventListener('error', () => {
            if (!session.cancelled) stopTalk('Live speaker unavailable');
        });
    } catch (error) {
        if (!session.cancelled) stopTalk(error.message);
    }
});

talkStop.addEventListener('click', () => stopTalk());

function releaseMicrophone() {
    clearTimeout(recordingTimer);
    recordingTimer = null;
    recorder?.port.close();
    recorder?.disconnect();
    recorder = null;
    recordingSource?.disconnect();
    recordingSource = null;
    microphone?.getTracks().forEach(track => track.stop());
    microphone = null;
    recordingContext?.close().catch(() => {});
    recordingContext = null;
    recordingSamples = [];
    recordingFrames = 0;
}

function clearRecording() {
    recordingAudio.pause();
    recordingAudio.removeAttribute('src');
    recordingAudio.load();
    if (recordingURL) URL.revokeObjectURL(recordingURL);
    recordingURL = null;
    recordedAudio = null;
    recordingPreview.hidden = true;
    vocalFile.required = true;
}

function finishRecording(error = null) {
    if (!recorder) return;
    const sampleRate = recordingContext.sampleRate;
    const samples = new Float32Array(recordingFrames);
    let offset = 0;
    for (const chunk of recordingSamples) {
        samples.set(chunk, offset);
        offset += chunk.length;
    }
    releaseMicrophone();
    record.disabled = false;
    finish.disabled = true;
    vocalFile.disabled = false;
    if (pageClosing) return;
    if (error || !samples.length) {
        vocalStatus.textContent = error || 'Recording is empty';
        vocalPlay.disabled = !vocalFile.files.length;
        return;
    }
    try {
        const wav = wavEncoder.encode.sync({sampleRate, channelData: [samples]}, {bitDepth: 16});
        recordedAudio = new Blob([wav], {type: 'audio/wav'});
        recordingURL = URL.createObjectURL(recordedAudio);
        recordingAudio.src = recordingURL;
        recordingPreview.hidden = false;
        vocalFile.required = false;
        vocalPlay.disabled = false;
        vocalStatus.textContent = 'Recording ready';
    } catch (failure) {
        vocalStatus.textContent = failure.message;
        vocalPlay.disabled = !vocalFile.files.length;
    }
}

vocalFile.addEventListener('change', () => {
    clearRecording();
    vocalPlay.disabled = !vocalFile.files.length;
});

record.addEventListener('click', async () => {
    if (vocalOperation || recorder || recordingPending || talkSession) return;
    vocalStatus.hidden = false;
    if (!window.isSecureContext || !navigator.mediaDevices?.getUserMedia) {
        vocalStatus.textContent = 'Microphone unavailable: a secure browser context is required.';
        return;
    }
    if (!window.AudioWorkletNode) {
        vocalStatus.textContent = 'Microphone recording is not supported by this browser.';
        return;
    }
    recordingPending = true;
    record.disabled = true;
    vocalPlay.disabled = true;
    vocalFile.disabled = true;
    clearRecording();
    vocalStatus.textContent = 'Waiting for microphone permission...';
    try {
        microphone = await navigator.mediaDevices.getUserMedia({audio: {
            channelCount: 1, echoCancellation: true, noiseSuppression: true, autoGainControl: true,
        }});
        if (pageClosing) {
            releaseMicrophone();
            return;
        }
        recordingContext = new AudioContext();
        await recordingContext.audioWorklet.addModule('/a/anyka-microphone.js');
        if (pageClosing) {
            releaseMicrophone();
            return;
        }
        await recordingContext.resume();
        recordingSource = recordingContext.createMediaStreamSource(microphone);
        recorder = new AudioWorkletNode(recordingContext, 'anyka-microphone', {
            channelCount: 1, channelCountMode: 'explicit', outputChannelCount: [1],
        });
        const maximumFrames = Math.floor(recordingContext.sampleRate * 30);
        recorder.port.onmessage = ({data}) => {
            if (!recorder || !(data instanceof Float32Array)) return;
            const chunk = data.slice(0, maximumFrames - recordingFrames);
            recordingSamples.push(chunk);
            recordingFrames += chunk.length;
            if (recordingFrames >= maximumFrames) finishRecording();
        };
        recorder.onprocessorerror = () => finishRecording('Microphone capture failed');
        recordingSource.connect(recorder);
        recorder.connect(recordingContext.destination);
        vocalFile.value = '';
        listen.checked = false;
        player.video.muted = true;
        document.getElementById('mj-mute-t').textContent = 'Muted';
        finish.disabled = false;
        vocalStatus.textContent = 'Recording...';
        recordingTimer = setTimeout(finishRecording, 29000);
    } catch (error) {
        releaseMicrophone();
        record.disabled = false;
        vocalFile.disabled = false;
        vocalPlay.disabled = !vocalFile.files.length;
        vocalStatus.textContent = error.message;
    } finally {
        recordingPending = false;
    }
});

finish.addEventListener('click', () => {
    finish.disabled = true;
    finishRecording();
});

vocalVolume.addEventListener('input', event => {
    event.stopImmediatePropagation();
    document.getElementById('anyka-vocal-level').value = `${vocalVolume.value} dB`;
}, true);
vocalForm.addEventListener('submit', async event => {
    event.preventDefault();
    const file = recordedAudio || vocalFile.files[0];
    if (!file || vocalOperation || recorder || recordingPending || talkSession) return;
    const operation = new AbortController();
    vocalOperation = operation;
    vocalPlay.disabled = true;
    vocalStop.disabled = false;
    record.disabled = true;
    vocalFile.disabled = true;
    recordingAudio.pause();
    vocalStatus.hidden = false;
    vocalStatus.textContent = 'Preparing audio...';
    let decoder;
    try {
        if (file.size > 10 * 1024 * 1024) throw new Error('Audio file exceeds 10 MiB');
        decoder = new AudioContext();
        const decoded = await decoder.decodeAudioData(await file.arrayBuffer());
        await decoder.close();
        decoder = null;
        if (!Number.isFinite(decoded.duration) || decoded.duration <= 0 || decoded.duration > 30) {
            throw new Error('Audio must be between zero and thirty seconds');
        }
        const renderer = new OfflineAudioContext(1, Math.ceil(decoded.duration * 8000), 8000);
        const source = renderer.createBufferSource();
        source.buffer = decoded;
        source.connect(renderer.destination);
        source.start();
        const rendered = await renderer.startRendering();
        operation.signal.throwIfAborted();
        const samples = rendered.getChannelData(0);
        const pcm = new ArrayBuffer(samples.length * 2);
        const view = new DataView(pcm);
        samples.forEach((sample, index) => {
            const bounded = Math.max(-1, Math.min(1, sample));
            view.setInt16(index * 2, Math.round(bounded * (bounded < 0 ? 32768 : 32767)), true);
        });
        vocalStatus.textContent = 'Playing on camera...';
        const response = await fetch(`/api/audio/play?volume=${vocalVolume.value}`, {
            method: 'POST', headers: {'Content-Type': 'application/octet-stream'},
            body: pcm, signal: operation.signal,
        });
        if (!response.ok) throw new Error((await response.text()).trim() || 'Speaker unavailable');
        vocalStatus.textContent = 'Playback finished';
    } catch (error) {
        vocalStatus.textContent = operation.signal.aborted ? 'Stopped' : error.message;
    } finally {
        if (decoder) await decoder.close().catch(() => {});
        vocalOperation = null;
        vocalPlay.disabled = false;
        vocalStop.disabled = true;
        record.disabled = false;
        vocalFile.disabled = false;
    }
});
vocalStop.addEventListener('click', async () => {
    vocalOperation?.abort();
    try {
        const response = await fetch('/api/audio/stop', {method: 'POST'});
        if (!response.ok) throw new Error('Could not stop the camera speaker');
    } catch (error) {
        vocalStatus.textContent = error.message;
    }
});
window.addEventListener('pagehide', () => {
    pageClosing = true;
    stopTalk();
    vocalOperation?.abort();
    releaseMicrophone();
    clearRecording();
});