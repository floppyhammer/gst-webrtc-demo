## Plain RTP

Sender

```bash
gst-launch-1.0 -v \
  filesrc location=test.mp4 ! \
  decodebin3 !  \
  x264enc tune=zerolatency bitrate=8192 ! \
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
  autovideosink sync=false
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
  x264enc tune=zerolatency bitrate=8192 ! \
  rtph264pay config-interval=-1 aggregate-mode=zero-latency ! \
  application/x-rtp,encoding-name=H264,clock-rate=90000,media=video,payload=96 ! \
  rtpulpfecenc percentage=20 pt=122 ! \
  udpsink host=10.11.9.192 port=5000
```

Receiver

```bash                                                                                                                     
gst-launch-1.0 -v \
  udpsrc port=5000 caps='application/x-rtp,media=(string)video,clock-rate=(int)90000,encoding-name=(string)H264' ! \
  rtpstorage size-time=220000000 ! \
  rtpssrcdemux ! \
  application/x-rtp,payload=96,clock-rate=90000,media=video,encoding-name=H264 ! \
  rtpjitterbuffer do-lost=1 latency=5 ! \
  rtpulpfecdec pt=122 ! \
  rtph264depay ! \
  avdec_h264 ! \
  videoconvert ! \
  autovideosink sync=false
```

## rtpst2022-1-fecenc

Sender

```bash
gst-launch-1.0 \
  rtpbin name=rtp fec-encoders='fec,0="rtpst2022-1-fecenc\ rows\=5\ columns\=5\ enable-row-fec\=true\ enable-column-fec\=true";' \
  filesrc location=test.mp4 ! decodebin3 ! timeoverlay ! x264enc key-int-max=60 tune=zerolatency bitrate=8192 ! \
  queue ! mpegtsmux ! rtpmp2tpay ssrc=0 ! rtp.send_rtp_sink_0 \
  rtp.send_rtp_src_0 ! udpsink host=10.11.9.192 port=5000 \
  rtp.send_fec_src_0_0 ! udpsink host=10.11.9.192 port=5002 async=false \
  rtp.send_fec_src_0_1 ! udpsink host=10.11.9.192 port=5004 async=false
```

Receiver

```bash
gst-launch-1.0 rtpbin latency=500 fec-decoders='fec,0="rtpst2022-1-fecdec\ size-time\=1000000000";' \
  name=rtp udpsrc port=5002 caps="application/x-rtp, payload=96" ! \
  queue ! \
  rtp.recv_fec_sink_0_0 udpsrc port=5004 \
  caps="application/x-rtp, payload=96" ! \
  queue ! \
  rtp.recv_fec_sink_0_1 \
  udpsrc port=5000 caps="application/x-rtp, media=video, clock-rate=90000, encoding-name=mp2t, payload=33" ! \
  queue ! \
  netsim drop-probability=0.0 ! \
  rtp.recv_rtp_sink_0 \
  rtp. ! \
  avdec_h264 ! \
  videoconvert ! \
  queue ! \
  autovideosink sync=false
```

## Comparison with ffmpeg

Sender

```bash
ffmpeg -re -i test.mp4 -preset ultrafast -tune zerolatency -codec libx264 -f mpegts udp://10.11.9.192:5000
```

Receiver

```bash
ffplay -fflags nobuffer -flags low_delay -probesize 32 -analyzeduration 1 -strict experimental -framedrop -f mpegts -vf setpts=0 udp://0.0.0.0:5000
```
