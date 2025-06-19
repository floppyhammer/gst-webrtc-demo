Sender

```bash
gst-launch-1.0 -v filesrc location=test.mp4 ! decodebin3 ! timeoverlay ! tee name=tee ! queue ! videoconvert ! autovideosink tee. ! queue ! encodebin2 profile="video/x-h264|element-properties,bitrate=2192" ! rtph264pay config-interval=-1 aggregate-mode=zero-latency ! application/x-rtp,encoding-name=H264,clock-rate=90000,media=video,payload=96 ! rtpulpfecenc percentage=80 pt=122 ! udpsink host=127.0.0.1 port=5000
```

Receiver

```bash
gst-launch-1.0 -v udpsrc port=5000 caps='application/x-rtp,media=(string)video,clock-rate=(int)90000,encoding-name=(string)H264' ! rtpstorage size-time=220000000 ! rtpssrcdemux ! application/x-rtp,payload=96,clock-rate=90000,media=video,encoding-name=H264 ! rtpjitterbuffer do-lost=1 latency=5 ! rtpulpfecdec pt=122 ! rtph264depay ! avdec_h264 ! videoconvert ! autovideosink
```