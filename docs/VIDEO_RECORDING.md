# Video Recording System

```
    ██╗   ██╗██╗██████╗ ███████╗ ██████╗ ██████╗ 
    ██║   ██║██║██╔══██╗██╔════╝██╔════╝██╔═══██╗
    ██║   ██║██║██████╔╝█████╗  ██║     ██║   ██║
    ╚██╗ ██╔╝██║██╔══██╗██╔══╝  ██║     ██║   ██║
     ╚████╔╝ ██║██║  ██║███████╗╚██████╗╚██████╔╝
      ╚═══╝  ╚═╝╚═╝  ╚═╝╚══════╝ ╚═════╝ ╚═════╝ 
    
    "I use Arch btw" - The ChadVis Recording System
```

## Overview

The ChadVis video recording system captures your audio visualizations and encodes them into high-quality video files. It supports both software encoding (CPU) and hardware acceleration (GPU) for maximum flexibility and performance.

## Features

- **Multiple Codecs**: H.264, H.265/HEVC, VP9, AV1, ProRes, FFV1 (lossless)
- **Hardware Acceleration**: NVIDIA NVENC, Intel/AMD VAAPI
- **Real-time Stats**: Live FPS, dropped frames, encoding health indicator
- **Audio Sync**: Perfectly synchronized audio and video
- **Quality Presets**: Pre-configured settings for YouTube, Twitter, Discord, and more

## Quick Start

### Basic Recording

1. Load a song and let it play
2. Click the **⏺ Start Recording** button in the Recording panel
3. The recording will automatically stop when the track ends (if configured)
4. Find your video in `~/.local/share/chadvis-projectm-qt/recordings/`

### Keyboard Shortcut

Press `R` to toggle recording on/off.

### CLI Recording

```bash
# Record a song immediately
chadvis-projectm-qt --record song.mp3

# Specify output file
chadvis-projectm-qt --record --output my_video.mp4 song.mp3

# Use a specific preset
chadvis-projectm-qt --preset "Aderrasi - Airhandler" --record song.mp3
```

## Configuration

### Config File Location

```
~/.config/chadvis-projectm-qt/config.toml
```

### Recording Settings

```toml
[recording]
enabled = true
auto_record = false
output_directory = "~/.local/share/chadvis-projectm-qt/recordings"
default_filename = "chadvis_{date}_{time}_{preset}"
container = "mp4"

# Auto-stop options
restart_track_on_record = true
stop_at_track_end = true
record_entire_song = true

[recording.video]
codec = "libx264"      # or "h264_nvenc" for NVIDIA GPU
width = 1920
height = 1080
fps = 60
crf = 23               # Quality: 0-51 (lower = better, 23 is default)
preset = "medium"      # Speed: ultrafast to veryslow

[recording.audio]
codec = "aac"
bitrate = 320          # kbps
```

## Quality Presets

| Preset | Codec | Resolution | Use Case |
|--------|-------|------------|----------|
| YouTube 1080p60 | H.264 | 1920x1080@60 | High quality YouTube uploads |
| YouTube 4K60 | H.264 | 3840x2160@60 | Maximum quality 4K |
| Twitter/X 720p | H.264 | 1280x720@30 | Optimized for Twitter |
| Discord 8MB | H.264 | 1280x720@30 | Compressed for Discord free tier |
| Hardware 1080p60 | NVENC | 1920x1080@60 | GPU encoding, minimal CPU |
| Hardware 4K60 | NVENC HEVC | 3840x2160@60 | GPU 4K recording |
| Lossless | FFV1 | 1920x1080@60 | No quality loss, huge files |
| Editing (ProRes) | ProRes | 1920x1080@60 | For video editing software |

## Hardware Acceleration

### NVIDIA NVENC (Recommended)

Requirements:
- NVIDIA GTX 600 series or newer
- GTX 900 series or newer for HEVC
- Proprietary NVIDIA drivers installed

Enable in config:
```toml
[recording.video]
codec = "h264_nvenc"   # or "hevc_nvenc" for H.265
```

Or select the "Hardware 1080p60 (NVENC)" preset in the GUI.

### Intel/AMD VAAPI

Requirements:
- Intel: 6th Gen Core (Skylake) or newer
- AMD: RX 400 series or newer
- Mesa drivers with VAAPI support

Enable in config:
```toml
[recording.video]
codec = "h264_vaapi"   # or "hevc_vaapi" for H.265
```

### Checking Hardware Support

```bash
# List available encoders
ffmpeg -encoders | grep -E "(nvenc|vaapi|amf)"

# Test NVENC
ffmpeg -f lavfi -i testsrc=duration=1:size=1280x720:rate=30 -c:v h264_nvenc -f null -

# Test VAAPI
ffmpeg -f lavfi -i testsrc=duration=1:size=1280x720:rate=30 -c:v h264_vaapi -f null -
```

## Understanding the Stats Panel

```
┌─────────────────────────────────────┐
│  Recording                          │
│  Quality: [YouTube 1080p60    ▼]   │
│  Output:  [chadvis_2024-01-29...]  │
│  [⏹ Stop Recording]                 │
└─────────────────────────────────────┘
┌─────────────────────────────────────┐
│  Statistics                         │
│  Status: Recording                  │
│  Time:   02:34:12                   │
│  Frames: 5520 (0 dropped) @ 60.0 FPS│
│  Size:   234.5 MB                   │
│  [████████░░░░░░░░░░] 90%           │
└─────────────────────────────────────┘
```

### Stats Explained

- **Time**: Recording duration (HH:MM:SS)
- **Frames**: Total frames written (dropped frames) @ current FPS
- **Size**: Current file size
- **Health Bar**: Encoding performance indicator
  - 🟢 Green (90-100%): Excellent, keeping up with target FPS
  - 🟡 Yellow (50-89%): Warning, some frames may drop
  - 🔴 Red (<50%): Critical, significant frame drops expected

### What Affects Performance?

1. **Resolution**: 4K requires 4x the processing of 1080p
2. **FPS**: 60fps is 2x the work of 30fps
3. **Codec**: H.265 is slower than H.264
4. **Preset**: "slow" = better compression, more CPU usage
5. **CRF**: Lower values = higher quality = larger files

## Troubleshooting

### Recording Stutters or Drops Frames

**Solutions:**
1. Use hardware acceleration (NVENC/VAAPI)
2. Lower resolution (try 720p)
3. Reduce FPS (try 30fps)
4. Use a faster preset ("fast" or "veryfast")
5. Increase CRF (try 28 for lower quality but smaller files)

### Audio/Video Out of Sync

**Check:**
- Ensure your audio device is working properly
- Try a different audio backend in config
- Check system audio latency settings

### "Failed to initialize encoder" Error

**Solutions:**
1. Check codec is installed: `ffmpeg -encoders | grep libx264`
2. For hardware: Verify drivers are installed
3. Try software encoding first to isolate the issue

### Files Are Too Large

**Solutions:**
1. Increase CRF (try 28-32)
2. Use H.265 instead of H.264 (better compression)
3. Lower resolution or FPS
4. Use the "Discord 8MB" preset

### Can't Find Recordings

Default locations:
- **Recordings**: `~/.local/share/chadvis-projectm-qt/recordings/`
- **Config**: `~/.config/chadvis-projectm-qt/config.toml`

Check the Recording panel for the current output path.

## Advanced Usage

### Custom FFmpeg Options

Add raw FFmpeg options in config:
```toml
[recording.video]
codec = "libx264"
extra_options = "-x264opts rc-lookahead=60:ref=4"
```

### Batch Recording

Record multiple songs automatically:
```bash
for song in ~/Music/*.mp3; do
    chadvis-projectm-qt --record --output "$(basename "$song" .mp3).mp4" "$song"
done
```

### Recording with Specific Preset

```bash
chadvis-projectm-qt --preset "Geiss - Reaction Diffusion 2" --record song.mp3
```

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    VideoRecorder (Main Thread)               │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────┐  │
│  │  start()    │  │  stop()     │  │  submitVideoFrame() │  │
│  └─────────────┘  └─────────────┘  └─────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│              VideoRecorderThread (Background Thread)         │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────┐  │
│  │ FrameGrabber│  │ Audio Buffer│  │   FFmpeg Encoder    │  │
│  │  (Queue)    │  │  (PCM f32)  │  │  (libavcodec)       │  │
│  └─────────────┘  └─────────────┘  └─────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                      Output File                             │
│              (MP4/MKV/WebM with A/V sync)                    │
└─────────────────────────────────────────────────────────────┘
```

## Performance Tips

### For Maximum Quality (Slow Encoding)
```toml
[recording.video]
codec = "libx265"
crf = 18
preset = "slow"
```

### For Real-time Performance (Fast Encoding)
```toml
[recording.video]
codec = "h264_nvenc"  # or "ultrafast" for software
preset = "fast"
crf = 23
```

### For Arch Linux Users (I use Arch btw)

Install FFmpeg with all codecs:
```bash
sudo pacman -S ffmpeg

# For NVIDIA hardware acceleration
sudo pacman -S nvidia-utils

# For Intel/AMD VAAPI
sudo pacman -S libva-intel-driver libva-mesa-driver
```

## See Also

- [Architecture Documentation](../docs/ARCHITECTURE.md)
- [Configuration Guide](../docs/deepwiki/v1.0-RC1/Nsomnia/chadvis-projectm-qt/5_Application_Config.md)
- [FFmpeg Documentation](https://ffmpeg.org/documentation.html)

---

*"Recording visualizations is like catching lightning in a bottle - except the lightning is made of math and the bottle is a GPU."* - Ancient Arch Linux Proverb
