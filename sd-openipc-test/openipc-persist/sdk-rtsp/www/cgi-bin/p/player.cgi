<%

mj_src_labels=$(sed -n \
	's/^mj_\(source_[A-Za-z0-9_]*\)=\(.*\)/\1\t\2/p' \
	j/locale.cgi 2>/dev/null |
	while IFS="$(printf '\t')" read -r k v; do
		v=$(printf '%s' "$v" |
			sed -e 's/\\/\\\\/g' -e 's/"/\\"/g' -e 's/[<>]//g' \
				-e 's/[[:cntrl:]]//g')
		printf '"%s":"%s"\n' "$k" "$v"
	done | paste -sd,)
printf '%s' \
	"<script type=\"application/json\" id=\"mj-preview-boot\">{\"labels\":{${mj_src_labels}}}</script>"

%>
<div class="mj-player" id="mj-player">
	<div class="mj-stage" id="mj-stage" tabindex="0" aria-label="Live video">
		<video id="live-video" class="mj-stage-media" autoplay muted playsinline></video>
		<video id="live-video-b" class="mj-stage-media" autoplay muted playsinline style="display:none"></video>
		<canvas id="live-canvas" class="mj-stage-media" style="display:none"></canvas>
		<canvas id="live-canvas-b" class="mj-stage-media" style="display:none"></canvas>
		<img id="live-mjpeg" class="mj-stage-media" alt="" style="display:none">
		<img id="live-mjpeg-b" class="mj-stage-media" alt="" style="display:none">
		<img id="mj-snapshot" class="mj-stage-media" alt="" hidden>
		<img id="mj-still" class="mj-still-media" alt="Uncompressed still" hidden>
		<button type="button" id="mj-tap-play" class="mj-tap-play" hidden
			aria-label="Tap to play the live video">
			<svg viewBox="0 0 64 64" width="34" height="34" fill="currentColor" aria-hidden="true">
				<path d="M25 20.5v23a1.5 1.5 0 0 0 2.28 1.28l18.7-11.5a1.5 1.5 0 0 0 0-2.56l-18.7-11.5A1.5 1.5 0 0 0 25 20.5z"></path>
			</svg>
		</button>
		<p id="mj-note" class="alert alert-warning mj-stage-alert" style="display:none">
			<span id="mj-note-why">Your browser can't play the live video stream.</span>
			<a id="mj-note-act" href="camera.cgi?tab=jpeg">Enable JPEG</a> for an MJPEG fallback.
		</p>
		<p id="mj-blind" class="alert alert-warning mj-stage-alert" style="display:none">
			<span id="mj-blind-why"></span>
			<a id="mj-blind-act" href="camera.cgi?tab=isp"></a>
			<a id="mj-blind-help" href="logs.cgi" hidden></a>
		</p>
		<span id="mj-badge" class="mj-chip">connecting…</span>
		<div id="mj-stats" class="mj-stats-overlay small" hidden></div>
		<div class="mj-toasts" id="mj-toasts">
		<p id="mj-still-note" class="mj-adapt-toast small" role="status" hidden></p>
		<p id="mj-peer-note" class="mj-adapt-toast small" role="status" hidden></p>
		<div id="mj-ear-card" class="mj-adapt-toast mj-ear-card small" hidden>
			<b class="mj-ear-num" id="mj-ear-now">–</b>
			<span class="mj-ear-sub">best <b id="mj-ear-best">–</b></span>
			<span class="mj-ear-word" id="mj-ear-word" role="status"></span>
			<button type="button" class="mj-hud-btn" id="mj-ear-area" aria-pressed="false" hidden
				title="Listen to part of the picture: press, then drag a rectangle over it. A click instead of a drag keeps the whole frame; pressing again goes back to the whole frame.">
				<svg viewBox="0 0 20 20" width="16" height="16" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
					<rect x="3" y="4.4" width="14" height="11.2" rx="1.2" stroke-dasharray="3 2.2"></rect>
					<path d="M10 7.6v4.8M7.6 10h4.8"></path>
				</svg>
				<span>Area</span>
			</button>
			<button type="button" class="mj-hud-btn" id="mj-ear-reset">Start over</button>
		</div>
		<p id="mj-ear-note" class="mj-adapt-toast small" role="status" hidden></p>
		<p id="mj-adapt" class="mj-adapt-toast small" role="status" hidden>
			<span id="mj-adapt-rates" class="mj-adapt-rates"></span>
			<span id="mj-adapt-why" class="mj-adapt-why"></span>
			<button type="button" class="mj-adapt-close" aria-label="Dismiss">×</button>
		</p>
		<p id="mj-served" class="mj-adapt-toast mj-served-toast small" role="status" hidden>
			<span id="mj-served-why"></span>
			<button type="button" class="mj-adapt-close" aria-label="Dismiss">×</button>
		</p>
		</div>
		<div id="mj-marquee" class="mj-marquee" hidden></div>
		<div id="mj-ptz" class="mj-ptz" hidden></div>
	<div class="mj-bar" id="mj-bar">
			<span class="mj-hud mj-seg" role="group" aria-label="View" id="mj-view-ctl" hidden>
				<input type="radio" class="mj-seg-in" name="mj-view" id="mj-view-fit" autocomplete="off">
				<label class="mj-seg-lbl" for="mj-view-fit"
					title="The whole frame. Letterboxed where its shape does not match the window's.">Fit</label>
				<input type="radio" class="mj-seg-in" name="mj-view" id="mj-view-fill" autocomplete="off" checked>
				<label class="mj-seg-lbl" for="mj-view-fill"
					title="Cover the window: no part of the screen is spent on black. Whatever does not fit is one drag away.">Fill</label>
				<input type="radio" class="mj-seg-in" name="mj-view" id="mj-view-one" autocomplete="off">
				<label class="mj-seg-lbl" for="mj-view-one"
					title="One stream pixel per screen pixel — what to judge focus on.">1:1</label>
			</span>

			<span class="mj-hud mj-tog-wrap" id="mj-detail-ctl" hidden>
				<input type="checkbox" class="mj-tog-in" id="mj-detail" autocomplete="off">
				<label class="mj-tog" for="mj-detail"
					title="Freeze the part of the scene you are looking at and show it uncompressed, at the main stream's full resolution — no quantiser, no encoder, no enlargement. Trades motion for detail: nothing moves while it is up. Esc or a click returns to live, and so does any zoom or pan.">
					<span class="mj-led"></span>
					<svg viewBox="0 0 20 20" width="16" height="16" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
						<circle cx="8.8" cy="8.8" r="5.2"></circle>
						<path d="M12.6 12.6 17 17"></path>
						<path d="M6.9 8.8h3.8M8.8 6.9v3.8"></path>
					</svg>
					<span class="mj-tog-t">Detail</span>
				</label>
			</span>

			<span class="mj-hud mj-tog-wrap" id="mj-area-ctl" hidden>
				<input type="checkbox" class="mj-tog-in" id="mj-area" autocomplete="off">
				<label class="mj-tog" for="mj-area"
					title="Draw a rectangle on the picture to enlarge that part of it. Where nothing is hidden — Fit, mostly — you can just drag; this is for when the picture is already zoomed in and a drag would move it instead. Esc cancels; Fit or Fill comes back out.">
					<span class="mj-led"></span>
					<svg viewBox="0 0 20 20" width="16" height="16" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
						<rect x="3" y="4.4" width="14" height="11.2" rx="1.2" stroke-dasharray="3 2.2"></rect>
						<path d="M10 7.6v4.8M7.6 10h4.8"></path>
					</svg>
					<span class="mj-tog-t">Area</span>
				</label>
			</span>

			<span class="mj-hud mj-tog-wrap" id="mj-peer-ctl" hidden>
				<input type="checkbox" class="mj-tog-in" id="mj-peer" autocomplete="off">
				<label class="mj-tog" for="mj-peer"
					title="See another camera's view of this scene. Its outline appears on the picture; click inside it and that camera's live picture is laid over the outline, aligned with this one. Zoom as usual to look closer. Click again or press Esc to take it away.">
					<span class="mj-led"></span>
					<svg viewBox="0 0 20 20" width="16" height="16" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
						<rect x="2.5" y="5" width="8" height="10" rx="1.2"></rect>
						<rect x="12.5" y="7" width="5" height="6" rx="1"></rect>
						<path d="M10.5 10h2"></path>
					</svg>
					<span class="mj-tog-t">Peer</span>
				</label>
				<select id="mj-peer-pick" class="mj-peer-pick" aria-label="Which camera" hidden></select>
			</span>

			<span class="mj-hud mj-seg" role="group" aria-label="Source"
				id="mj-source" hidden></span>

			<span class="mj-hud mj-seg" role="group" aria-label="Stream">
				<input type="radio" class="mj-seg-in" name="mj-stream" id="mj-stream-0" autocomplete="off" checked>
				<label class="mj-seg-lbl" for="mj-stream-0">Main</label>
				<input type="radio" class="mj-seg-in" name="mj-stream" id="mj-stream-1" autocomplete="off" disabled>
				<label class="mj-seg-lbl" for="mj-stream-1" id="mj-sub" hidden>Sub</label>
				<input type="radio" class="mj-seg-in" name="mj-stream" id="mj-stream-auto" autocomplete="off" disabled>
				<label class="mj-seg-lbl" for="mj-stream-auto" id="mj-auto" hidden
					title="Picks whichever stream is closest to the size this player is being shown at, and follows the window as it changes. The chip names the one in use.">Auto</label>
			</span>

			<span class="mj-hud mj-tog-wrap" id="mj-audio-ctl" hidden>
				<input type="checkbox" class="mj-tog-in" id="mj-mute" autocomplete="off">
				<label class="mj-tog" for="mj-mute" id="mj-mute-lbl">
					<span class="mj-led"></span>
					<svg viewBox="0 0 20 20" width="16" height="16" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
						<path d="M4 7.6h2.8L10.6 4.4v11.2L6.8 12.4H4z"></path>
						<path class="mj-ic-off" d="M13.6 7.8l3.8 4.4M17.4 7.8l-3.8 4.4"></path>
						<path class="mj-ic-on" d="M13.4 7.6a3.4 3.4 0 0 1 0 4.8M15.8 5.6a6.6 6.6 0 0 1 0 8.8"></path>
					</svg>
					<span class="mj-tog-t" id="mj-mute-t">Muted</span>
				</label>
				<input type="range" id="mj-vol" min="0" max="100" value="100" class="mj-vol" disabled aria-label="Volume">
			</span>

			<span class="mj-hud mj-tog-wrap" id="mj-talk-ctl" hidden>
				<input type="checkbox" class="mj-tog-in" id="mj-talk" autocomplete="off">
				<label class="mj-tog mj-tog-amber" for="mj-talk" id="mj-talk-lbl"
					title="Send this browser's microphone to the camera's speaker. The camera will not take audio in one direction only, so talking also opens its audio to you.">
					<span class="mj-led"></span>
					<svg viewBox="0 0 20 20" width="16" height="16" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
						<rect x="7.4" y="2.6" width="5.2" height="9" rx="2.6"></rect>
						<path d="M4.6 9.4a5.4 5.4 0 0 0 10.8 0M10 14.8v2.6"></path>
					</svg>
					<span class="mj-tog-t" id="mj-talk-t">Talk</span>
				</label>
			</span>

			<span class="mj-hud mj-seg mj-bar-end" role="group" aria-label="Transport" id="mj-transport-ctl" hidden>
				<input type="radio" class="mj-seg-in" name="mj-transport" id="mj-transport-w" autocomplete="off">
				<label class="mj-seg-lbl" for="mj-transport-w" id="mj-transport-lbl"
					title="Sub-second video and two-way audio, and the camera fits the stream to your connection — which changes it for everyone else watching that stream too.">WebRTC</label>
				<input type="radio" class="mj-seg-in" name="mj-transport" id="mj-transport-m" autocomplete="off">
				<label class="mj-seg-lbl" for="mj-transport-m" id="mj-transport-m-lbl"
					title="Plain buffered playback. A couple of seconds behind, but nothing adapts and nothing negotiates.">MSE</label>
			</span>

			<span class="mj-hud mj-tog-wrap" id="mj-ear-ctl" hidden>
				<input type="checkbox" class="mj-tog-in" id="mj-ear" autocomplete="off">
				<label class="mj-tog" for="mj-ear"
					title="Hear the focus while you turn the lens. Beeps come faster and higher as the picture nears the best it has been; a low note means you have gone past it; a held tone means you are back on it. Press Start over after changing zoom.">
					<span class="mj-led"></span>
					<svg viewBox="0 0 20 20" width="16" height="16" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
						<path d="M6.5 8.4a3.5 3.5 0 0 1 7 0c0 2.2-2.2 2.6-2.2 4.6a1.6 1.6 0 0 1-3.2 0"></path>
						<path d="M4 6.4a6.6 6.6 0 0 1 12 0"></path>
					</svg>
					<span class="mj-tog-t">By ear</span>
				</label>
			</span>

			<span class="mj-hud mj-tog-wrap" id="mj-stats-ctl" hidden>
				<input type="checkbox" class="mj-tog-in" id="mj-stats-btn" autocomplete="off">
				<label class="mj-tog" for="mj-stats-btn"
					title="Per-second measurements from both ends of the session.">
					<span class="mj-led"></span>
					<svg viewBox="0 0 20 20" width="16" height="16" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" aria-hidden="true">
						<path d="M3.4 16.6V11M8.4 16.6V4.6M13.4 16.6V8.2M17.2 16.6v-3.4"></path>
					</svg>
					<span class="mj-tog-t">Stats</span>
				</label>
			</span>

			<span class="mj-hud mj-ico-wrap">
				<button type="button" class="mj-hud-ico" id="mj-snap" hidden
					title="Download a full-resolution snapshot" aria-label="Snapshot">
					<svg viewBox="0 0 20 20" width="17" height="17" fill="none" stroke="currentColor" stroke-width="1.5" aria-hidden="true">
						<path d="M2.6 6.6h3.2l1.5-2.1h5.4l1.5 2.1h3.2v9H2.6z" stroke-linejoin="round"></path>
						<circle cx="10" cy="10.6" r="3.1"></circle>
					</svg>
				</button>
				<button type="button" class="mj-hud-ico" id="mj-fs" hidden
					title="Fullscreen" aria-label="Fullscreen">
					<svg viewBox="0 0 20 20" width="17" height="17" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" aria-hidden="true">
						<path d="M3 7.4V3h4.4M16.9 7.4V3h-4.4M3 12.6V17h4.4M16.9 12.6V17h-4.4"></path>
					</svg>
				</button>
			</span>
		</div>
	</div>
</div>
