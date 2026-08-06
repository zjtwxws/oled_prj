import os, re

path = r"E:\BaiduNetdiskDownload\code\oled_prj\stm32f407\src\menu_items.c"
with open(path, "rb") as f:
    d = f.read()

# Convert ALL remaining \x escape sequences in .text strings to real UTF-8
# Pattern: \x followed by 2 hex chars
# We need to find C string literals with \x escapes and convert them

def replace_x_escapes(match):
    """Convert \xNN sequences to actual bytes"""
    text = match.group(0)
    # Use a simple approach: replace each \xNN with the actual byte
    result = b""
    i = 0
    while i < len(text):
        if text[i:i+2] == b"\\x" and i + 4 <= len(text):
            hex_val = text[i+2:i+4]
            try:
                byte_val = int(hex_val, 16)
                result += bytes([byte_val])
                i += 4
            except ValueError:
                result += text[i:i+1]
                i += 1
        else:
            result += text[i:i+1]
            i += 1
    return result

# Find all .text lines that contain \x escapes
# These are lines like: .text = "6.\xe4\xb8\x8a..."
pattern = re.compile(rb'\.text = "[^"]*\\x[0-9a-fA-F]{2}[^"]*"')
matches = list(pattern.finditer(d))
print(f"Found {len(matches)} lines with \\x escapes")

for m in reversed(matches):
    old = m.group(0)
    new = replace_x_escapes(old)
    d = d[:m.start()] + new + d[m.end():]

with open(path, "wb") as f:
    f.write(d)

# Verify
with open(path, "rb") as f:
    d2 = f.read()
remaining = len(re.findall(rb'\\x[0-9a-fA-F]{2}', d2))
print(f"Remaining \\x escapes: {remaining}")

# Decode and show
lines = d2.split(b"\n")
for line in lines:
    if b".text = " in line and not b"NULL" in line:
        print(line.decode("utf-8").strip())
