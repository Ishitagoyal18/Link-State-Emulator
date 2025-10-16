import numpy as np
import socket
import time
import os

transition_matrix = np.full((26, 26), -1, dtype=int)
TIMEOUT=1
N=0
ports=[]
IP=[]
HOST=  "0.0.0.0"
PORT = 5000
cons=[]


def parse(path):
    global N
    tm= np.full((26, 26), -1, dtype=int)
    with open(path, "r", encoding="utf-8") as f:
        text = f.read()

    lines = text.splitlines()
    token_rows = []

    for raw in lines:
        no_comment = raw.split('#', 1)[0].lstrip()
        if not no_comment.strip():
            continue
        tokens = no_comment.split()
        token_rows.append(tokens)

    r = len(token_rows)
    N = r + 1
    for i in range(26):
        tm[i][i]=0

    for i in range(N-1):
        for j in range(len(token_rows[i])):
            tm[i][j+i+1]=int(token_rows[i][j])
            tm[j+i+1][i]=int(token_rows[i][j])
    return tm

def encode_msg(msgto,neighbor):
    msg=""
    nei=bin(neighbor)
    nei=nei[2:]
    msg+=nei.zfill(5)

    msg+=IP[neighbor].zfill(32)
    msg+=ports[neighbor].zfill(13)
    cost=bin(transition_matrix[msgto][neighbor])
    cost=cost[2:]
    msg+=cost.zfill(16)
    return msg


def decode_connect(msg):
    return msg[:32],msg[32:]


def send_all():
    for i in range(N):
        msg = ""
        for j in range(N):
            if transition_matrix[i][j] != -1:
                msg += encode_msg(i, j)
        cons[i].sendall(msg.encode())
        print(f"Sent LINK-STATE to VN {i}")
        
def detect_change(path):
    new_tm=parse(path)
    for i in range(N):
        for j in range(N):
            if(new_tm[i][j]!=transition_matrix[i][j]):
                return True
    return False


def run_oracle(config_path):
    global N, transition_matrix,IP, ports, cons
    transition_matrix = parse(config_path)

    print(f"Oracle ready, expecting {N} virtual nodes...")
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        s.bind((HOST, PORT))
        s.listen(N)

        try:
            while len(IP) < N:
                conn, addr = s.accept()
                data = conn.recv(1024)
                if not data:
                    continue
                ip, port = decode_connect(data.decode())
                IP.append(ip)
                ports.append(port)
                cons.append(conn)
                print(f"[Oracle] Received CONNECT from VN {len(IP)}: IP={ip}, Port={port}")
                
            print("[Oracle] All nodes connected. Sending initial LINK-STATE info...")
            send_all()

            # print("[Oracle] Monitoring config file for topology changes...")
            while True:
                time.sleep(TIMEOUT)
                if detect_change(config_path):
                    send_all()

        except KeyboardInterrupt:
            s.close()
        

config_file="config.txt"
run_oracle(config_file)