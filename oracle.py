import numpy as np
import socket

transition_matrix = np.full((26, 26), -1, dtype=int)

def parse(path):
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
        transition_matrix[i][i]=0

    for i in range(N-1):
        for j in range(len(token_rows[i])):
            transition_matrix[i][j+i]=int(token_rows[i][j])
            transition_matrix[j+i][i]=int(token_rows[i][j])

HOST=  "172.18.145.245"
PORT = 5000

with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
    s.bind((HOST, PORT))
    s.listen(1)
    conn, addr = s.accept()
    print("Connected by", addr)
    data = conn.recv(1024)
    print("Received:", data.decode())   
    conn.close()
