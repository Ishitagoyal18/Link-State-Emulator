import numpy as np
with open("config.txt", "r", encoding="utf-8") as f:
    text = f.read()

lines = text.splitlines()
token_rows = []

for raw in lines:
    # remove inline comment, ignore leading whitespace
    no_comment = raw.split('#', 1)[0].lstrip()
    if not no_comment.strip():
        continue
    tokens = no_comment.split()
    token_rows.append(tokens)

r = len(token_rows)
N = r + 1

transition_matrix=np.full((26, 26), -1, dtype=float)

for i in range(26):
    transition_matrix[i][i]=0

for i in range(N-1):
    for j in range(len(token_rows[i])):
        transition_matrix[i][j+i]=int(token_rows[i][j])
        transition_matrix[j+i][i]=int(token_rows[i][j])

print(token_rows)
print(transition_matrix)