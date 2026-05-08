#$bytes = [System.IO.File]::ReadAllBytes("C:\Users\magic\Downloads\best_640x480_k230d.kmodel")
#$client = New-Object System.Net.Sockets.TcpClient("172.20.10.11", 8080)
#$stream = $client.GetStream()
#$stream.Write($bytes, 0, $bytes.Length)
#$stream.Flush()
#Start-Sleep -Seconds 2
#$stream.Close()
#$client.Close()
#Write-Host "Sent!"

import network
import socket
import time

nic = network.WLAN(network.STA_IF)
nic.active(True)
nic.connect("Spadeyache32", "pkxc9kvozip2")

print("Connecting to WiFi...")
while not nic.isconnected():
    time.sleep(0.5)

print("IP:", nic.ifconfig()[0])

HOST = '0.0.0.0'
PORT = 8080

s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind((HOST, PORT))
s.listen(1)
print("Listening on port 8080")

conn = None
while conn is None:
    try:
        conn, addr = s.accept()
    except OSError:
        time.sleep(0.1)

print("Connected by", addr)
conn.settimeout(10)

f = open('/data/kmodels/received.kmodel', 'wb')
total = 0
while True:
    try:
        chunk = conn.read(256)
        if not chunk:
            break
        f.write(chunk)
        total += len(chunk)
        print(total, end="\r")
    except OSError as e:
        print("OSError:", e.args[0], e)
        break

f.close()
conn.close()
s.close()
print("\nDone:", total, "bytes")
