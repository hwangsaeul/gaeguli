[![Build Status](https://dev.azure.com/hwangsaeul/hwangsaeul/_apis/build/status/justinjoy.gaeguli?branchName=master)](https://dev.azure.com/hwangsaeul/hwangsaeul/_build/latest?definitionId=9&branchName=master)

# Gaeguli

*[gæguli]* is SRT streamer designed for edge devices that require strong security and ultra-low latency.


## Getting started

### Install meson and ninja

Meson 0.51 and newer is required.

### Build Gaeguli

You can get library and executables built runnning:

```
meson build
ninja -C build
```

### Run

#### Preparing srt listener

From GStreamer 1.14, SRT is supported from gst-plugins-bad. However, the plugins
are fully reconstructed so we recommend using srt plugins in GStreamer 1.16 or newer.

```
gst-launch-1.0 \
        srtsrc uri=srt://:8888?mode=listener ! decodebin ! autovideosink
```

Now, the port, 8888, is ready to listen srt stream.

#### Fifo transmitter

When running `fifo-transmitter`, it will create a fifo in `$tmpdir` which is
used for capturing byte stream.

```
fifo-transmit-1.0 -h 172.30.254.138 -p 8888
```

The above command will show the location of fifo. You should keep the path to run
`pipeline` tool.

#### Pipeline

`pipeline` will create a pipeline to capture video from camera, then sending
TS (Transport Stream) with given fifo.

```
pipeline-1.0 -f /var/folders/48/29v1_3bs77l8m6pyxd13c7gh0000gn/T/gaeguli-fifo-5VQ57Z/fifo
```

Then, in srt listener, you can play video captured from camera.
