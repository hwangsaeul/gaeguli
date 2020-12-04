![Build Status](https://github.com/hwangsaeul/gaeguli/workflows/CI/badge.svg?branch=master)

# Gaeguli
*[gæguli]* is a library for video streaming using SRT that provides strong security and ultra-low latency.

## Overview
*Gaeguli* implements the supporting library for handling SRT streaming. It provides 'pipeline' component
that deals with receiving frames from a video source and streaming to a specific URI using SRT protocol.

## Build from sources
To build the from sources follow the procedure described in

[Build from sources](https://github.com/hwangsaeul/hwangsaeul.github.io/blob/master/build_from_sources.md)

*Gaeguli* requires GStreamer 1.16 or newer.

## Usage examples

*Gaeguli* includes an executable binary `pipeline-2.0` which serves as a demonstration of basic streaming
scenarios. For full capabilities of `libgaeguli` please refer to the API documentation and for coding example
see `tools/pipeline.c`.

### Stream in SRT caller mode on port 8888

Sender:

```console
pipeline-2.0 -d /dev/video0  srt://127.0.0.1:8888
```

Receiver:

```console
gst-launch-1.0 srtsrc uri=srt://:8888 ! queue ! decodebin ! autovideosink
```

Note that a `queue` is required right after `srtsrc`. Otherwise, you will see that the time on the receiving side gradually slows down.

### Stream in SRT listener mode on port 8888

Sender:

```console
pipeline-2.0 -d /dev/video0  srt://:8888
```

Receiver (several independent connections at a time are possible):

```console
gst-launch-1.0 srtsrc uri=srt://127.0.0.1:8888 ! queue ! decodebin ! autovideosink
```

## PPA nightly builds

Experimental versions of Gaeguli are daily generated in [launchpad](https://launchpad.net/~hwangsaeul/+archive/ubuntu/nightly).

```console
$ sudo add-apt-repository ppa:hwangsaeul/nightly
$ sudo apt-get update
$ sudo apt-get install libgaeguli2 libgaeguli-dev gaeguli-tools
```
