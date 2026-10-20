"""Authenticated camera snapshots as a bounded multipart JPEG producer."""

import http.client
import os
import sys
import time
import urllib.error
import urllib.request

MAX_IMAGE_BYTES = 4 * 1024 * 1024
REQUEST_TIMEOUT = 5
MAX_FAILURES = 3
BOUNDARY = b"--frameboundary\r\n"


def read_snapshot(opener, url):
    with opener.open(url, timeout=REQUEST_TIMEOUT) as response:
        image = response.read(MAX_IMAGE_BYTES + 1)
    if (
        len(image) > MAX_IMAGE_BYTES
        or not image.startswith(b"\xff\xd8")
        or not image.endswith(b"\xff\xd9")
    ):
        raise ValueError("incomplete or invalid JPEG")
    return image


def main():
    url = sys.argv[1]
    manager = urllib.request.HTTPPasswordMgrWithDefaultRealm()
    manager.add_password(
        None, url, os.environ["CAMERA_USERNAME"], os.environ["CAMERA_PASSWORD"]
    )
    opener = urllib.request.build_opener(urllib.request.HTTPDigestAuthHandler(manager))
    failures = 0
    started = False
    while True:
        try:
            image = read_snapshot(opener, url)
        except (OSError, ValueError, urllib.error.URLError, http.client.HTTPException) as error:
            failures += 1
            if failures == 1:
                # Exception text can contain credentials or URLs: report only its type.
                print("snapshot unavailable: " + type(error).__name__, file=sys.stderr, flush=True)
            # Opening an exec producer waits for its first bytes. Exit promptly
            # when offline, so an empty pipe cannot block that stream forever.
            if not started or failures >= MAX_FAILURES:
                return 1
            time.sleep(1)
            continue
        if failures:
            print("snapshot recovered", file=sys.stderr, flush=True)
        failures = 0
        header = (
            BOUNDARY
            + b"Content-Type: image/jpeg\r\n"
            + f"Content-Length: {len(image)}\r\n\r\n".encode()
        )
        try:
            sys.stdout.buffer.write(header + image + b"\r\n")
            sys.stdout.buffer.flush()
        except BrokenPipeError:
            return 0
        started = True
        time.sleep(0.1)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(0)
