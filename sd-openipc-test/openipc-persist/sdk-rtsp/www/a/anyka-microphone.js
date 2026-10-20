class AnykaMicrophoneProcessor extends AudioWorkletProcessor {
    constructor(options) {
        super();
        this.streamPCM = options.processorOptions?.streamPCM;
        this.samplesPerFrame = Math.round(sampleRate/25);
        this.frameBytes = this.samplesPerFrame*2;
        this.pcm = new ArrayBuffer(this.frameBytes);
        this.view = new DataView(this.pcm);
        this.position = 0;
    }

    process(inputs) {
        const samples = inputs[0]?.[0];
        if (samples?.length) {
            if (this.streamPCM) {
                for (const sample of samples) {
                    const bounded = Math.max(-1, Math.min(1, sample));
                    this.view.setInt16(this.position * 2, Math.round(bounded * (bounded < 0 ? 32768 : 32767)), true);
                    this.position++;
                    if (this.position === this.samplesPerFrame) {
                        this.port.postMessage(this.pcm, [this.pcm]);
                        this.pcm = new ArrayBuffer(this.frameBytes);
                        this.view = new DataView(this.pcm);
                        this.position = 0;
                    }
                }
            } else {
                const chunk = new Float32Array(samples);
                this.port.postMessage(chunk, [chunk.buffer]);
            }
        }
        return true;
    }
}

registerProcessor('anyka-microphone', AnykaMicrophoneProcessor);
