# make_b64.py
import base64

with open("loop.wav", "rb") as f:
    data = f.read()

b64 = base64.b64encode(data).decode("ascii")

with open("loop_b64.txt", "w") as f:
    f.write(b64)
