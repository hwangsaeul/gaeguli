![Build Status](https://github.com/hwangsaeul/gaeguli/workflows/CI/badge.svg?branch=master)

# Gaeguli
*[gæguli]* is a set of library for video streaming over SRT that requires strong security and ultra-low latency.

## Overview
*Gaeguli* implements the supporting library for handling SRT streaming. It provides 'pipeline' component
that deals with receiving frames from a video source and streaming to a specific URI using SRT protocol.

## Build from sources
To build the from sources follow the procedure described in

[Build from sources](https://github.com/hwangsaeul/hwangsaeul.github.io/blob/master/build_from_sources.md)

## Run

### Preparing srt listener

*Gaeguli* requires GStreamer 1.16 or newer. To launch a listener that will receive our stream on port 8888:

```console
gst-launch-1.0 \
        srtsrc uri=srt://:8888?mode=listener ! queue ! decodebin ! autovideosink
```

Note that a `queue` is required right behind `srtsrc`.
Otherwise, You will see that the time on the receiving side gradually slows down.

### Pipeline

To start streaming to a listener running on the same machine, run:


```console
pipeline-1.0 -h 127.0.0.1 -p 8888
```

## PPA nightly builds

Experimental versions of Gaeguli are daily generated in [launchpad](https://launchpad.net/~hwangsaeul/+archive/ubuntu/nightly).

```console
$ sudo add-apt-repository ppa:hwangsaeul/nightly
$ sudo apt-get update
$ sudo apt-get install libgaeguli2 libgaeguli-dev gaeguli-tools
```
