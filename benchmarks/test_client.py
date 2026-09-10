#!/usr/bin/env python3
"""Quick test client for the order-book matching engine server.

Connects, sends a batch of order lines, prints each execution report, then closes.
Usage: python3 test_client.py [PORT]
"""
import socket
import sys
import time

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 9001

ORDERS = [
    "N,1,BID,LIMIT,105.00,100",   # rests (bid)
    "N,2,ASK,LIMIT,105.50,200",   # rests (ask)
    "N,3,ASK,LIMIT,105.25,150",   # rests (better ask)
    "N,4,BID,LIMIT,105.25,100",   # crosses -> trade vs order 3 @105.25
    "C,2",                        # cancel the 105.50 ask
    "N,5,BID,IOC,105.25,100",     # IOC: fills what it can (50 left on ask 3), cancels rest
    "N,6,BID,FOK,105.25,1000",    # FOK: not enough liquidity -> killed, 0 filled
    "N,7,ASK,LIMIT,105.25,100",   # add liquidity back
    "N,8,BID,FOK,105.25,100",     # FOK: exactly fillable -> fully filled
]

def main():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(3.0)
    s.connect(("localhost", PORT))

    # Send all orders at once (they are newline-delimited).
    payload = "".join(o + "\n" for o in ORDERS)
    s.sendall(payload.encode())

    # Read replies until we've seen one line per order (or timeout).
    buf = ""
    expected = len(ORDERS)
    got = 0
    deadline = time.time() + 3.0
    while got < expected and time.time() < deadline:
        try:
            chunk = s.recv(4096).decode()
        except socket.timeout:
            break
        if not chunk:
            break
        buf += chunk
        while "\n" in buf:
            line, buf = buf.split("\n", 1)
            if line.strip():
                print(f"  {ORDERS[got] if got < len(ORDERS) else '?':<28} -> {line}")
                got += 1

    s.close()
    print(f"\nReceived {got}/{expected} execution reports.")

if __name__ == "__main__":
    main()
