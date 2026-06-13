import base64
with open("loop.mp3","rb") as f: data=f.read()
b64=base64.b64encode(data).decode()
with open("loop_b64.txt","w") as f: f.write(b64)
