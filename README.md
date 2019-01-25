Websocket Stream audio channel module for Asterisk
============================================

This provides a websocket resource for streaming audio channel Asterisk.
It works with asterisk versions 16.x or later

Requirements
------------
- Asterisk 16.x (or later) header files (http://asterisk.org)

Installation
------------
    $ make
    $ make install

To use
------

Loading module

    asterisk -r
    CLI> modules unload res_ari_stream.so
    CLI> modules load res_ari_stream.so

Docker
------

Edit the sample to have the good configuration.
To build image to test with docker.

    docker build -t asterisk-ari-stream .
    docker run -it asterisk-ari-stream bash
    asterisk

Use it
------

Check our example in contrib directory. You need to send the subprotocol channel-stream with a http header Channel-ID which contains the channel id you would like to stream.
