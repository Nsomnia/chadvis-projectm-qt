# ChadVis Overdrive Overhaul - Mission Log

## ✅ COMPLETED (TURNING IT TO 11)
- **Controller Architecture**: Logic decoupled from `MainWindow`.
- **Async PBO Recording**: ping-pong buffers for stall-free capture.
- **Preset Ranking**: Integrated 1-5 star ratings with TOML persistence.
- **Playlist Persistence**: Auto-save/load sessions.
- **Rich Overlays**: Dynamic ID3 templates and DVD-logo kinetics.
- **Viewport Precision**: Resolved projectM resolution cutoff.

## Recording still broken:

 ./build/chadvis-projectm-qt
```
[13:29:54.875] [info] Logger initialized. Debug mode: false
[13:29:54.876] [info] chadvis-projectm-qt starting up. I use Arch btw.
[13:29:54.877] [info] Config: visualizer 640x512 @ 30fps
[13:29:54.877] [info] Config loaded from: /home/nsomnia/.config/chadvis-projectm-qt/config.toml
qt.multimedia.ffmpeg: Using Qt multimedia with FFmpeg version n8.0.1 GPL version 3 or later
Failed to open VDPAU backend libvdpau_nvidia.so: cannot open shared object file: No such file or directory
[13:29:55.430] [info] Audio engine initialized with QAudioBufferOutput
[13:29:55.430] [info] Overlay engine initialized with 3 elements
[13:29:55.431] [info] Audio engine initialized with QAudioBufferOutput
[13:29:55.431] [info] Overlay engine initialized with 3 elements
[13:29:55.435] [info] VisualizerPanel: Connecting to presetChanged signal
[13:29:55.435] [info] VisualizerPanel: Connected to preset signals
[13:29:55.762] [info] Loading last session playlist...
[13:29:55.762] [info] Initialization complete. Let's get this bread.
[13:29:57.944] [info] Scanned 14582 presets from /usr/share/projectM/presets
[13:30:21.821] [info] Loading track: Once Upon A Time In Mexico (Nuclear Fusion Mother__ckers) - DJ STEEL.mp3
[13:30:21.823] [info] AudioEngine::play() CALLED
[13:30:21.823] [info] Calling player_->play(), source=file:///home/nsomnia/Downloads/Once Upon A Time In Mexico (Nuclear Fusion Mother__ckers) - DJ STEEL.mp3
[13:30:21.823] [info] player_->play() returned, state=0
Input #0, mp3, from '/home/nsomnia/Downloads/Once Upon A Time In Mexico (Nuclear Fusion Mother__ckers) - DJ STEEL.mp3':
  Metadata:
    title           : Once Upon A Time In Mexico (Nuclear Fusion Mother**ckers) - DJ STEEL
    artist          : [redacted-handle]e
    comment         : 2000 credits and still either can't get the story right, or the vocal model breaks down (a bit of it here) (or the entire v5 model breaks down and does wild as hell generations I want to almksti Release!). enjoy son, GET SOME! soo... flamenco PHLUCK this 
  Duration: 00:07:59.96, start: 0.023021, bitrate: 178 kb/s
  Stream #0:0: Audio: mp3 (mp3float), 48000 Hz, stereo, fltp, 176 kb/s, start 0.023021
    Metadata:
      encoder         : Lavc60.31
  Stream #0:1: Video: mjpeg (Baseline), yuvj420p(pc, bt470bg/unknown/unknown), 572x1024 [SAR 1:1 DAR 143:256], 90k tbr, 90k tbn, start 0.023022 (attached pic)
    Metadata:
      title           : Cover
      comment         : Cover (front)
[13:30:21.909] [info] AudioEngine: Player state changed from 0 to 1
[mp3float @ 0x563c2d6bab40] Could not update timestamps for skipped samples.
[libx264 @ 0x563c2d912ac0] using cpu capabilities: MMX2 SSE2Fast SSSE3 SSE4.2
[libx264 @ 0x563c2d912ac0] profile Main, level 4.0, 4:2:0, 8-bit
[libx264 @ 0x563c2d912ac0] 264 - core 165 r3222 b35605a - H.264/MPEG-4 AVC codec - Copyleft 2003-2025 - http://www.videolan.org/x264.html - options: cabac=0 ref=1 deblock=0:0:0 analyse=0:0 me=dia subme=0 psy=1 psy_rd=1.00:0.00 mixed_ref=0 me_range=16 chroma_me=1 trellis=0 8x8dct=0 cqm=0 deadzone=21,11 fast_pskip=1 chroma_qp_offset=0 threads=2 lookahead_threads=2 sliced_threads=1 slices=2 nr=0 decimate=1 interlaced=0 bluray_compat=0 constrained_intra=0 bframes=3 b_pyramid=2 b_adapt=0 b_bias=0 direct=1 weightb=0 open_gop=0 weightp=0 keyint=60 keyint_min=6 scenecut=0 intra_refresh=0 rc=crf mbtree=0 crf=23.0 qcomp=0.60 qpmin=0 qpmax=69 qpstep=4 ip_ratio=1.40 pb_ratio=1.30 aq=0
[13:30:38.887] [info] Recording started: /home/nsomnia/Videos/ChadVis/chadvis-projectm-qt_2026-01-01_13-30-38.mp4
[13:30:48.651] [info] Preset changed to $$$ Royal - Mashup (390), restarting recording...
[libx264 @ 0x563c2d912ac0] frame I:5     Avg QP:15.00  size: 16913
[libx264 @ 0x563c2d912ac0] frame P:63    Avg QP:14.95  size: 11604
[libx264 @ 0x563c2d912ac0] frame B:183   Avg QP:16.87  size:  5852
[libx264 @ 0x563c2d912ac0] consecutive B-frames:  2.0%  0.8%  4.8% 92.4%
[libx264 @ 0x563c2d912ac0] mb I  I16..4: 100.0%  0.0%  0.0%
[libx264 @ 0x563c2d912ac0] mb P  I16..4:  3.2%  0.0%  0.0%  P16..4:  1.5%  0.0%  0.0%  0.0%  0.0%    skip:95.3%
[libx264 @ 0x563c2d912ac0] mb B  I16..4:  0.4%  0.0%  0.0%  B16..8:  1.8%  0.0%  0.0%  direct: 0.6%  skip:97.2%  L0:28.1% L1:27.8% BI:44.2%
[libx264 @ 0x563c2d912ac0] coded y,uvDC,uvAC intra: 17.9% 19.3% 16.2% inter: 1.1% 1.3% 0.8%
[libx264 @ 0x563c2d912ac0] i16 v,h,dc,p: 82%  7%  7%  4%
[libx264 @ 0x563c2d912ac0] i8c dc,h,v,p: 84%  7%  6%  3%
[libx264 @ 0x563c2d912ac0] kb/s:1803.91
[aac @ 0x563c2d393180] Qavg: 51464.977
[13:30:48.685] [info] Recording stopped. Frames: 251, Dropped: 0
[libx264 @ 0x7f7a5c69b280] using cpu capabilities: MMX2 SSE2Fast SSSE3 SSE4.2
[libx264 @ 0x7f7a5c69b280] profile Main, level 4.0, 4:2:0, 8-bit
[libx264 @ 0x7f7a5c69b280] 264 - core 165 r3222 b35605a - H.264/MPEG-4 AVC codec - Copyleft 2003-2025 - http://www.videolan.org/x264.html - options: cabac=0 ref=1 deblock=0:0:0 analyse=0:0 me=dia subme=0 psy=1 psy_rd=1.00:0.00 mixed_ref=0 me_range=16 chroma_me=1 trellis=0 8x8dct=0 cqm=0 deadzone=21,11 fast_pskip=1 chroma_qp_offset=0 threads=2 lookahead_threads=2 sliced_threads=1 slices=2 nr=0 decimate=1 interlaced=0 bluray_compat=0 constrained_intra=0 bframes=3 b_pyramid=2 b_adapt=0 b_bias=0 direct=1 weightb=0 open_gop=0 weightp=0 keyint=60 keyint_min=6 scenecut=0 intra_refresh=0 rc=crf mbtree=0 crf=23.0 qcomp=0.60 qpmin=0 qpmax=69 qpstep=4 ip_ratio=1.40 pb_ratio=1.30 aq=0
[13:30:48.701] [info] Recording started: /home/nsomnia/Videos/ChadVis/chadvis-projectm-qt_20260101_133048.mp4
[13:30:58.648] [info] Preset changed to fed - DSC-011 LSD Edit 2 --- Isosceles edit2, restarting recording...
[libx264 @ 0x7f7a5c69b280] frame I:4     Avg QP:18.00  size: 26784
[libx264 @ 0x7f7a5c69b280] frame P:53    Avg QP:18.53  size: 23551
[libx264 @ 0x7f7a5c69b280] frame B:155   Avg QP:20.35  size: 11613
[libx264 @ 0x7f7a5c69b280] consecutive B-frames:  1.9%  0.0%  5.7% 92.5%
[libx264 @ 0x7f7a5c69b280] mb I  I16..4: 100.0%  0.0%  0.0%
[libx264 @ 0x7f7a5c69b280] mb P  I16..4: 27.3%  0.0%  0.0%  P16..4:  9.2%  0.0%  0.0%  0.0%  0.0%    skip:63.5%
[libx264 @ 0x7f7a5c69b280] mb B  I16..4:  3.8%  0.0%  0.0%  B16..8:  9.3%  0.0%  0.0%  direct: 2.2%  skip:84.8%  L0:38.8% L1:38.2% BI:23.0%
[libx264 @ 0x7f7a5c69b280] coded y,uvDC,uvAC intra: 19.8% 28.4% 14.1% inter: 5.1% 6.0% 1.1%
[libx264 @ 0x7f7a5c69b280] i16 v,h,dc,p: 63% 16%  8% 13%
[libx264 @ 0x7f7a5c69b280] i8c dc,h,v,p: 73% 12%  9%  6%
[libx264 @ 0x7f7a5c69b280] kb/s:3572.04
[aac @ 0x563c2d58b800] Qavg: 33976.703
[13:30:58.695] [info] Recording stopped. Frames: 212, Dropped: 0
[libx264 @ 0x563c2d45c440] using cpu capabilities: MMX2 SSE2Fast SSSE3 SSE4.2
[libx264 @ 0x563c2d45c440] profile Main, level 4.0, 4:2:0, 8-bit
[libx264 @ 0x563c2d45c440] 264 - core 165 r3222 b35605a - H.264/MPEG-4 AVC codec - Copyleft 2003-2025 - http://www.videolan.org/x264.html - options: cabac=0 ref=1 deblock=0:0:0 analyse=0:0 me=dia subme=0 psy=1 psy_rd=1.00:0.00 mixed_ref=0 me_range=16 chroma_me=1 trellis=0 8x8dct=0 cqm=0 deadzone=21,11 fast_pskip=1 chroma_qp_offset=0 threads=2 lookahead_threads=2 sliced_threads=1 slices=2 nr=0 decimate=1 interlaced=0 bluray_compat=0 constrained_intra=0 bframes=3 b_pyramid=2 b_adapt=0 b_bias=0 direct=1 weightb=0 open_gop=0 weightp=0 keyint=60 keyint_min=6 scenecut=0 intra_refresh=0 rc=crf mbtree=0 crf=23.0 qcomp=0.60 qpmin=0 qpmax=69 qpstep=4 ip_ratio=1.40 pb_ratio=1.30 aq=0
[13:30:58.709] [info] Recording started: /home/nsomnia/Videos/ChadVis/chadvis-projectm-qt_20260101_133058.mp4
[libx264 @ 0x563c2d45c440] frame I:1     Avg QP:20.00  size: 21278
[libx264 @ 0x563c2d45c440] frame P:10    Avg QP:20.50  size: 61949
[libx264 @ 0x563c2d45c440] frame B:30    Avg QP:22.37  size: 17653
[libx264 @ 0x563c2d45c440] consecutive B-frames:  2.4%  0.0%  0.0% 97.6%
[libx264 @ 0x563c2d45c440] mb I  I16..4: 100.0%  0.0%  0.0%
[libx264 @ 0x563c2d45c440] mb P  I16..4: 51.5%  0.0%  0.0%  P16..4: 10.3%  0.0%  0.0%  0.0%  0.0%    skip:38.2%
[libx264 @ 0x563c2d45c440] mb B  I16..4:  2.4%  0.0%  0.0%  B16..8:  8.7%  0.0%  0.0%  direct: 4.8%  skip:84.2%  L0:24.8% L1:23.8% BI:51.4%
[libx264 @ 0x563c2d45c440] coded y,uvDC,uvAC intra: 12.2% 24.1% 19.5% inter: 6.2% 13.0% 8.3%
[libx264 @ 0x563c2d45c440] i16 v,h,dc,p: 80% 10%  5%  5%
[libx264 @ 0x563c2d45c440] i8c dc,h,v,p: 80% 10%  5%  4%
[libx264 @ 0x563c2d45c440] kb/s:6850.85
[aac @ 0x563c305a9a00] Qavg: 30749.023
[13:31:00.727] [info] Recording stopped. Frames: 41, Dropped: 0
^C
Received signal 2, shutting down gracefully...
[13:31:04.624] [info] Shutting down...
[13:31:04.625] [info] Config saved atomically to: /home/nsomnia/.config/chadvis-projectm-qt/config.toml
[13:31:04.625] [info] Config saved atomically to: /home/nsomnia/.config/chadvis-projectm-qt/config.toml
^C
Received signal 2, shutting down gracefully...
[13:31:04.965] [info] Shutting down...
[13:31:04.966] [info] Config saved atomically to: /home/nsomnia/.config/chadvis-projectm-qt/config.toml
^C
Received signal 2, shutting down gracefully...
[13:31:08.605] [info] Shutting down...
[13:31:08.606] [info] Config saved atomically to: /home/nsomnia/.config/chadvis-projectm-qt/config.toml
^C
Received signal 2, shutting down gracefully...
[13:31:09.105] [info] Shutting down...
[13:31:09.106] [info] Config saved atomically to: /home/nsomnia/.config/chadvis-projectm-qt/config.toml
^C
Received signal 2, shutting down gracefully...
[13:31:09.572] [info] Shutting down...
[13:31:09.573] [info] Config saved atomically to: /home/nsomnia/.config/chadvis-projectm-qt/config.toml
fish: Job 1, './build/chadvis-projectm-qt' terminated by signal SIGKILL (Forced quit)
󰪢 1m20s 󰜥 󰉋  ••/chadvis-projectm-qt 󰜥 󰘬 fix/video-re 
    ls $HOME/Videos
 ChadVis
󰪢 0s 󰜥 󰉋  ••/chadvis-projectm-qt 󰜥 󰘬 fix/video-re 
    ls $HOME/Videos/ChadVis
 chadvis-projectm-qt_2025-12-31_17-24-21.mp4   chadvis-projectm-qt_2026-01-01_09-43-58.mp4   chadvis-projectm-qt_20260101_092629.mp4   chadvis-projectm-qt_20260101_094421.mp4
 chadvis-projectm-qt_2026-01-01_09-01-09.mp4   chadvis-projectm-qt_2026-01-01_13-30-38.mp4   chadvis-projectm-qt_20260101_094401.mp4   chadvis-projectm-qt_20260101_133048.mp4
 chadvis-projectm-qt_2026-01-01_09-26-16.mp4   chadvis-projectm-qt_20260101_092619.mp4       chadvis-projectm-qt_20260101_094411.mp4   chadvis-projectm-qt_20260101_133058.mp4
󰪢 0s 󰜥 󰉋  ••/chadvis-projectm-qt 󰜥 󰘬 fix/video-re 
    ll $HOME/Videos/ChadVis
Permissions Size User    Date Modified Name
.rw-r--r--   261 nsomnia 31 Dec  2025   chadvis-projectm-qt_2025-12-31_17-24-21.mp4
.rw-r--r--   261 nsomnia  1 Jan 09:02   chadvis-projectm-qt_2026-01-01_09-01-09.mp4
.rw-r--r--  1.8M nsomnia  1 Jan 09:26   chadvis-projectm-qt_2026-01-01_09-26-16.mp4
.rw-r--r--  717k nsomnia  1 Jan 09:44   chadvis-projectm-qt_2026-01-01_09-43-58.mp4
.rw-r--r--  2.2M nsomnia  1 Jan 13:30   chadvis-projectm-qt_2026-01-01_13-30-38.mp4
.rw-r--r--  1.4M nsomnia  1 Jan 09:26   chadvis-projectm-qt_20260101_092619.mp4
.rw-r--r--  524k nsomnia  1 Jan 09:26   chadvis-projectm-qt_20260101_092629.mp4
.rw-r--r--  6.2M nsomnia  1 Jan 09:44   chadvis-projectm-qt_20260101_094401.mp4
.rw-r--r--  6.9M nsomnia  1 Jan 09:44   chadvis-projectm-qt_20260101_094411.mp4
.rw-r--r--  432k nsomnia  1 Jan 09:44   chadvis-projectm-qt_20260101_094421.mp4
.rw-r--r--  3.5M nsomnia  1 Jan 13:30   chadvis-projectm-qt_20260101_133048.mp4
.rw-r--r--  1.2M nsomnia  1 Jan 13:31   chadvis-projectm-qt_20260101_133058.mp4
󰪢 0s 󰜥 󰉋  ••/chadvis-projectm-qt 󰜥 󰘬 fix/video-re 
    echo "no video file. Still starts a new recording when the projectM visaulizer shader preset changes automatically every n seconds."
no video file. Still starts a new recording when the projectM visaulizer shader preset changes automatically every n seconds.
```

## 🟡 NEXT STEPS
- [ ] **Advanced Settings**: Expose raw projectM mesh/shader knobs.
- [ ] **Unit Tests**: professional-grade logic coverage.
- [ ] **Telemetry Stats**: FPS/PBO performance overlay.

---
*I use Arch btw.*
