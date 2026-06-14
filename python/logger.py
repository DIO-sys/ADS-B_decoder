import socket
import struct
import sqlite3
import time
import sys
import os

sys.path.insert(0, os.path.dirname(__file__))
from adsb_pb2 import AircraftRecord

TCP_HOST = '127.0.0.1'
TCP_PORT = 30003
DB_PATH = 'adsb_log.db'

def init_db(path):
    db = sqlite3.connect(path)
    db.execute('''CREATE TABLE IF NOT EXISTS records (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        timestamp_ms INTEGER,
        icao_address INTEGER,
        callsign TEXT,
        latitude REAL,
        longitude REAL,
        altitude_ft INTEGER,
        ground_speed REAL,
        heading REAL,
        vertical_rate REAL,
        signal_power REAL,
        crc_valid INTEGER,
        received_at REAL
    )''')
    db.commit()
    return db

def recv_exact(sock, n):
    data = b''
    while len(data) < n:
        chunk = sock.recv(n - len(data))
        if not chunk:
            raise ConnectionError("Connection closed")
        data += chunk
    return data

def main():
    db = init_db(DB_PATH)
    msg_count = 0

    while True:
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.connect((TCP_HOST, TCP_PORT))
            print(f"[Logger] Connected to {TCP_HOST}:{TCP_PORT}")

            while True:
                # Read 4-byte length prefix
                len_buf = recv_exact(sock, 4)
                msg_len = struct.unpack('>I', len_buf)[0]

                # Read protobuf payload
                msg_buf = recv_exact(sock, msg_len)

                record = AircraftRecord()
                record.ParseFromString(msg_buf)

                # Insert into database
                db.execute('''INSERT INTO records
                    (timestamp_ms, icao_address, callsign, latitude, longitude,
                     altitude_ft, ground_speed, heading, vertical_rate,
                     signal_power, crc_valid, received_at)
                    VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)''',
                    (record.timestamp_ms,
                     record.icao_address,
                     record.callsign,
                     record.latitude,
                     record.longitude,
                     record.altitude_ft,
                     record.ground_speed,
                     record.heading,
                     record.vertical_rate,
                     record.signal_power,
                     int(record.crc_valid),
                     time.time()))

                msg_count += 1
                if msg_count % 50 == 0:
                    db.commit()
                    icao = format(record.icao_address, '06X')
                    print(f"[Logger] {msg_count} records | Last: {icao} "
                          f"{record.callsign} {record.altitude_ft}ft")

        except ConnectionError:
            print("[Logger] Connection lost, reconnecting in 2s...")
            time.sleep(2)
        except KeyboardInterrupt:
            print(f"\n[Logger] Saved {msg_count} records to {DB_PATH}")
            db.commit()
            db.close()
            break

if __name__ == '__main__':
    main()