"""Writes tests/fixtures/cue_music: short silent WAV images with cue sheets covering the cases the
scanner and the PC indexer must agree on (a BOM with CRLF, Windows-1252, a cue naming the image's
old extension, a cue naming no file in its folder, two FILE lines, a missing INDEX 01 and a track
past the end of its file)."""

import argparse
import io
import sys
import wave
from pathlib import Path

RATE = 8000

ALBUM_CUE = ("﻿REM GENRE \"Live\"\r\n"
             "REM DATE 1999\r\n"
             "PERFORMER \"The Cue Band\"\r\n"
             "TITLE \"Cue Album\"\r\n"
             "FILE \"Image.wav\" WAVE\r\n"
             "  TRACK 01 AUDIO\r\n"
             "    TITLE \"Opening\"\r\n"
             "    INDEX 01 00:00:00\r\n"
             "  TRACK 02 AUDIO\r\n"
             "    TITLE \"Second Song\"\r\n"
             "    PERFORMER \"Guest Singer\"\r\n"
             "    INDEX 00 00:00:60\r\n"
             "    INDEX 01 00:01:00\r\n"
             "  TRACK 03 AUDIO\r\n"
             "    INDEX 01 00:02:15\r\n")

CONVERTED_CUE = ("FILE \"Disc.flac\" WAVE\n"
                 "TRACK 01 AUDIO\n"
                 "TITLE \"Été\"\n"
                 "INDEX 01 00:00:00\n"
                 "TRACK 02 AUDIO\n"
                 "TITLE \"Niño\"\n"
                 "PERFORMER \"Señor\"\n"
                 "INDEX 01 00:01:00\n")

ELSEWHERE_CUE = ("FILE \"Missing.wav\" WAVE\n"
                 "TRACK 01 AUDIO\n"
                 "INDEX 01 00:00:00\n"
                 "TRACK 02 AUDIO\n"
                 "INDEX 01 00:00:30\n")

BOTH_CUE = ("TITLE \"Both Files\"\n"
            "FILE \"a.wav\" WAVE\n"
            "TRACK 01 AUDIO\n"
            "TITLE \"A One\"\n"
            "INDEX 01 00:00:00\n"
            "TRACK 02 AUDIO\n"
            "TITLE \"A Two\"\n"
            "INDEX 01 00:01:00\n"
            "TRACK 03 AUDIO\n"
            "TITLE \"Too Late\"\n"
            "INDEX 01 00:05:00\n"
            "FILE \"b.wav\" WAVE\n"
            "TRACK 04 AUDIO\n"
            "TITLE \"B One\"\n"
            "INDEX 01 00:00:00\n"
            "TRACK 05 AUDIO\n"
            "TITLE \"No Start\"\n"
            "INDEX 00 00:00:30\n")


def silent_wav(seconds):
    data = io.BytesIO()
    with wave.open(data, "wb") as file:
        file.setnchannels(1)
        file.setsampwidth(1)
        file.setframerate(RATE)
        file.writeframes(bytes([0x80]) * int(RATE * seconds))
    return data.getvalue()


def fixture_files():
    return {
        "Album/Image.wav": silent_wav(3),
        "Album/Image.cue": ALBUM_CUE.encode("utf-8"),
        "Album/Bonus.wav": silent_wav(1),
        "Converted/Disc.wav": silent_wav(2),
        "Converted/Disc.cue": CONVERTED_CUE.encode("cp1252"),
        "Plain/Song.wav": silent_wav(1),
        "Plain/Other.cue": ELSEWHERE_CUE.encode("utf-8"),
        "Two Files/a.wav": silent_wav(2),
        "Two Files/b.wav": silent_wav(1),
        "Two Files/Both.cue": BOTH_CUE.encode("utf-8"),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--check", action="store_true", help="fail if the files differ")
    arguments = parser.parse_args()
    stale = []
    for name, data in fixture_files().items():
        path = arguments.output / name
        if arguments.check:
            if not path.is_file() or path.read_bytes() != data:
                stale.append(name)
            continue
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    for name in stale:
        print(f"out of date: {name}", file=sys.stderr)
    return 1 if stale else 0


if __name__ == "__main__":
    sys.exit(main())
