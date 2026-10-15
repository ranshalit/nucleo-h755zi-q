#!/usr/bin/env python3
"""Record a labeled session from the board microphone stream (port 8082).

Saves the raw MIC1 capture and a 16 kHz mono WAV produced with the firmware's
left-channel 63-tap FIR and 3:1 decimation, so the WAV matches what the
on-board keyword model hears. Recordings may contain voices: keep them local.

Example:
  scripts/record_speech_session.py --label neg_keyboard --seconds 60
  scripts/record_speech_session.py --label pos_up --seconds 60
"""
import argparse
import datetime
import pathlib
import struct
import sys
import urllib.request
import wave

import numpy as np

TAPS = np.array([
    -19, -29, -23, 0, 32, 55, 48, 0, -72, -123, -104, 0, 148, 247, 204, 0,
    -277, -455, -372, 0, 499, 819, 674, 0, -938, -1598, -1392, 0, 2411,
    5175, 7371, 8206, 7371, 5175, 2411, 0, -1392, -1598, -938, 0, 674,
    819, 499, 0, -372, -455, -277, 0, 204, 247, 148, 0, -104, -123, -72,
    0, 48, 55, 32, 0, -23, -29, -19], np.int64)


def board_16k(raw):
    start = raw.find(b"MIC1")
    if start < 0:
        sys.exit("no MIC1 header in capture")
    rate, channels, bits = struct.unpack_from("<IHH", raw, start + 4)
    if (rate, channels, bits) != (48000, 2, 16):
        sys.exit(f"expected 48000 Hz stereo 16-bit, got {rate}/{channels}/{bits}")
    body = raw[start + 16:]
    pcm = np.frombuffer(body[:len(body) // 4 * 4], "<i2").reshape(-1, 2)[:, 0]
    y = np.convolve(pcm.astype(np.int64), TAPS)[:len(pcm)]
    return np.clip((y + 16384) >> 15, -32768, 32767)[2::3].astype(np.int16)


def capture(url, seconds):
    need = 16 + seconds * 48000 * 4
    data = bytearray()
    with urllib.request.urlopen(url, timeout=10) as stream:
        while len(data) < need:
            chunk = stream.read(min(65536, need - len(data)))
            if not chunk:
                break
            data += chunk
    return bytes(data)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--label", required=True,
                        help="session label, e.g. neg_keyboard, neg_hebrew, pos_up")
    parser.add_argument("--seconds", type=int, default=60)
    parser.add_argument("--board", default="192.168.55.2")
    parser.add_argument("--out", type=pathlib.Path,
                        default=pathlib.Path.home() / "kws-data" / "sessions")
    args = parser.parse_args()

    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    folder = args.out / args.label
    folder.mkdir(parents=True, exist_ok=True)
    base = folder / f"{args.label}_{stamp}"
    print(f"recording {args.seconds} s for '{args.label}' ... go", flush=True)
    raw = capture(f"http://{args.board}:8082/", args.seconds)
    base.with_suffix(".raw").write_bytes(raw)
    y = board_16k(raw)
    with wave.open(str(base.with_suffix(".wav")), "wb") as out:
        out.setnchannels(1)
        out.setsampwidth(2)
        out.setframerate(16000)
        out.writeframes(y.tobytes())
    rms = np.sqrt(np.mean(y.astype(float) ** 2))
    print(f"saved {base}.wav: {len(y) / 16000:.1f} s, rms {20 * np.log10(max(rms, 1) / 32768):.1f} dBFS")


if __name__ == "__main__":
    main()
