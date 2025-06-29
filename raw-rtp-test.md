## RTP 1

Sender

```bash
gst-launch-1.0 -v \
  filesrc location=test.mp4 ! \
  decodebin3 ! \
  timeoverlay ! \
  tee name=t1 ! \
  queue ! \
  videoconvert ! \
  autovideosink t1. ! \
  x264enc tune=zerolatency bitrate=4000 ! \
  rtph264pay config-interval=-1 aggregate-mode=zero-latency ! \
  application/x-rtp,encoding-name=H264,clock-rate=90000,media=video,payload=96 ! \
  udpsink host=10.11.9.192 port=5000
```

Receiver

```bash
gst-launch-1.0 -v \
  udpsrc port=5000 caps='application/x-rtp,media=(string)video,clock-rate=(int)90000,encoding-name=(string)H264' ! \
  rtpjitterbuffer ! \
  rtph264depay ! \
  avdec_h264 ! \
  videoconvert ! \
  autovideosink
```

## RTP 2

Sender

```bash
gst-launch-1.0 -v \
  filesrc location=test.mp4 ! \
  decodebin3 ! \
  timeoverlay ! \
  tee name=t1 ! \
  queue ! \
  videoconvert ! \
  autovideosink t1. ! \
  x264enc tune=zerolatency bitrate=4000 ! \
  rtph264pay config-interval=-1 aggregate-mode=zero-latency ! \
  application/x-rtp,encoding-name=H264,clock-rate=90000,media=video,payload=96 ! \
  queue ! \
  rtpsink uri=rtp://10.11.9.192:5000
```

Receiver

```bash
gst-launch-1.0 -v \
  rtpsrc uri=rtp://localhost:5000?encoding-name=H264 ! \
  rtph264depay ! \
  avdec_h264 ! \
  videoconvert ! \
  queue ! \
  autovideosink
```

## ulpfec

Sender

```bash
gst-launch-1.0 -v \
  filesrc location=test.mp4 ! \
  decodebin3 ! \
  timeoverlay ! \
  tee name=t1 ! \
  queue ! \
  videoconvert ! \
  autovideosink t1. ! \
  queue ! \
  encodebin2 profile='video/x-h264|element-properties,bitrate=4000' ! \
  rtph264pay config-interval=-1 aggregate-mode=zero-latency ! \
  application/x-rtp,encoding-name=H264,clock-rate=90000,media=video,payload=96 ! \
  rtpulpfecenc percentage=20 pt=122 ! \
  udpsink host=10.11.9.192 port=5000
```

Receiver

```bash
gst-launch-1.0 -v \
  udpsrc port=5000 caps='application/x-rtp,media=(string)video,clock-rate=(int)90000,encoding-name=(string)VP8' ! \
  rtpstorage size-time=220000000 ! \
  rtpssrcdemux ! \
  application/x-rtp,clock-rate=90000,media=video,encoding-name=H264 ! \
  rtpjitterbuffer do-lost=1 latency=5 ! \
  rtpulpfecdec pt=122 ! \
  decodebin3 ! \
  videoconvert ! \
  autovideosink
```

## rtpst2022-1-fecenc (MPEG-TS wrapped in RTP)

Sender

```bash
gst-launch-1.0 -v \
  rtpbin name=rtp fec-encoders='fec,0="rtpst2022-1-fecenc\ rows\=5\ columns\=5\ enable-row-fec\=true\ enable-column-fec\=true";' \
  filesrc location=test.mp4 ! \
  decodebin3 ! \
  timeoverlay ! \
  tee name=t1 ! \
  queue ! \
  videoconvert ! \
  autovideosink t1. ! \
  x264enc key-int-max=60 tune=zerolatency bitrate=4000 ! \
  queue ! mpegtsmux ! rtpmp2tpay ssrc=0 ! rtp.send_rtp_sink_0 \
  rtp.send_rtp_src_0 ! udpsink host=10.11.9.192 port=5000 \
  rtp.send_fec_src_0_0 ! udpsink host=10.11.9.192 port=5002 async=false \
  rtp.send_fec_src_0_1 ! udpsink host=10.11.9.192 port=5004 async=false
```

Receiver

```bash
gst-launch-1.0 -v \
  rtpbin latency=5 fec-decoders='fec,0="rtpst2022-1-fecdec\ size-time\=1000000000";' name=rtp \
  udpsrc port=5002 caps="application/x-rtp,payload=96" ! \
  queue ! \
  rtp.recv_fec_sink_0_0 \
  udpsrc port=5004 caps="application/x-rtp,payload=96" ! \
  queue ! \
  rtp.recv_fec_sink_0_1 \
  udpsrc port=5000 caps="application/x-rtp,media=video,clock-rate=90000,encoding-name=mp2t,payload=33" ! \
  queue ! \
  netsim drop-probability=0.0 ! \
  rtp.recv_rtp_sink_0 \
  rtp. ! \
  decodebin ! \
  videoconvert ! \
  queue ! \
  autovideosink
```

## FFmpeg (MPEG-TS)

Sender

```bash
ffmpeg -re -i test.mp4 -preset ultrafast -tune zerolatency -codec libx264 -f mpegts udp://10.11.9.192:5000
```

Receiver

```bash
ffplay -fflags nobuffer -flags low_delay -probesize 32 -analyzeduration 1 -strict experimental -framedrop -f mpegts -vf setpts=0 udp://localhost:5000
```

## FFmpeg (RTP)

Sender

```bash
ffmpeg -re -i test.mp4 -preset ultrafast -tune zerolatency -codec libx264 -f rtp -sdp_file test_video.sdp "rtp://10.11.9.192:5000"
```

Receiver

```bash
ffplay -fflags nobuffer -flags low_delay -probesize 32 -analyzeduration 1 -strict experimental -framedrop -protocol_whitelist rtp,udp,file -i "test_video.sdp"
```
