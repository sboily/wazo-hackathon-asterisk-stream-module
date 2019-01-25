#!/usr/bin/env python3
import websocket
import sys

out = open("out.wav", "wb")

def on_message(ws, message):
    out.write(message)

def on_error(ws, error):
    print(error)

def on_close(ws):
    print("### closed ###")

if __name__ == "__main__":
    websocket.enableTrace(True)
    ws = websocket.WebSocketApp("ws://localhost:5039/ws",
                              on_message = on_message,
                              on_error = on_error,
                              on_close = on_close, subprotocols=["stream-channel"], header=['Channel-ID: ' + sys.argv[1]])
    ws.run_forever()
