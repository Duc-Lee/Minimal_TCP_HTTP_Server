def read_http_response(s, expected_bytes=0):
    data = b""
    while b"\r\n\r\n" not in data:
        chunk = s.recv(1024)
        if not chunk:
            break
        data += chunk

    if b"\r\n\r\n" not in data:
        return data.decode("utf-8", "ignore"), b""

    hdr_part, body_part = data.split(b"\r\n\r\n", 1)
    hdr_text = hdr_part.decode("utf-8", "ignore")

    content_len = 0
    for line in hdr_text.splitlines():
        if line.lower().startswith("content-length:"):
            try:
                content_len = int(line.split(":", 1)[1].strip())
            except ValueError:
                pass

    if expected_bytes > 0:
        content_len = expected_bytes

    body = bytearray(body_part)
    while len(body) < content_len:
        chunk = s.recv(min(8192, content_len - len(body)))
        if not chunk:
            break
        body.extend(chunk)

    return hdr_text, bytes(body)
