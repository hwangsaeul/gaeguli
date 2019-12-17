[![Build Status](https://dev.azure.com/hwangsaeul/hwangsaeul/_apis/build/status/hwangsaeul.gaeguli?branchName=master)](https://dev.azure.com/hwangsaeul/hwangsaeul/_build/latest?definitionId=12&branchName=master)

# Gaeguli
*[gæguli]* is SRT streamer designed for edge devices that require strong security and ultra-low latency.

## Overview
*Gaeguli* implements the supporting library to handling SRT streaming. It is designed with two parts
*   pipeline: Reads streaming for a video source and injects it into a fifo
*   fifo-transmitter: Reads fifo and send streaming to a specific URI using SRT protocol

So basically generation and transmission are decoupled.

## Build from sources
To build the from sources follow the procedure described in

[Build from sources](https://github.com/hwangsaeul/hwangsaeul.github.io/blob/master/build_from_sources.md)

## Run

### Preparing srt listener

From GStreamer 1.14, SRT is supported from gst-plugins-bad. However, the plugins
are fully reconstructed so we recommend using srt plugins in GStreamer 1.16 or newer.

```console
gst-launch-1.0 \
        srtsrc uri=srt://:8888?mode=listener ! queue ! decodebin ! autovideosink
```

Now, the port, 8888, is ready to listen srt stream.
Note that a `queue` is required right behind `srtsrc`. 
Otherwise, You will see that the time on the receiving side gradually slows down.

### Fifo transmitter

When running `fifo-transmitter`, it will create a fifo in `$tmpdir` which is
used for capturing byte stream.

```console
fifo-transmit-1.0 -h 172.30.254.138 -p 8888
```

The above command will show the location of fifo. You should keep the path to run
`pipeline` tool.

### Pipeline

`pipeline` will create a pipeline to capture video from camera, then sending
TS (Transport Stream) with given fifo.

```console
pipeline-1.0 -f /var/folders/48/29v1_3bs77l8m6pyxd13c7gh0000gn/T/gaeguli-fifo-5VQ57Z/fifo
```

Then, in srt listener, you can play video captured from camera.

## PPA nightly builds

Experimental versions of Gaeguli are daily generated in [launchpad](https://launchpad.net/~hwangsaeul/+archive/ubuntu/nightly).

```console
$ sudo add-apt-repository ppa:hwangsaeul/nightly
$ sudo apt-get update
$ sudo apt-get install libgaeguli1 libgaeguli-dev gaeguli-tools
```
