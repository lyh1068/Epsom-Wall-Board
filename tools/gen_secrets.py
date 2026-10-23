#!/usr/bin/env python3
"""
Generate wallboard_secrets.h from .env at build time.

Credentials live in a .env file rather than a C header so that the thing you
edit is not a source file. Real environment variables take precedence over the
file, so a CI job or a shell export can supply them without anything being
written to disk.

Missing values fall back to the documented placeholders rather than failing the
build. That is deliberate for this board: the rail key needs an approval that can
take days, so the radar, weather and clock phases have to be buildable and
flashable without it. Treat a placeholder as "this source is unavailable" and
show it as such on screen, rather than refusing to build.
"""

import argparse
import os
import pathlib
import sys

# name -> placeholder used when neither the environment nor .env supplies one
KEYS = {
    "WIFI_SSID": "your-ssid",
    "WIFI_PASSWORD": "your-password",
    # Rail Data Marketplace "Live Departure Board" (Darwin LDBWS). Registration
    # approval can take days, so the board has to boot usefully without it.
    "RAIL_DATA_KEY": "REPLACE-ME",
    # TfL Unified API app_key from api-portal.tfl.gov.uk.
    "TFL_APP_KEY": "REPLACE-ME",
}


def parse_env(path):
    """Minimal dotenv reader: KEY=VALUE, # comments, optional surrounding quotes."""
    found = {}
    if not path.is_file():
        return found
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("export "):
            line = line[len("export "):].lstrip()
        if "=" not in line:
            continue
        key, _, val = line.partition("=")
        key = key.strip()
        val = val.strip()
        # Strip one matching pair of outer quotes, so a password containing a
        # '#' or trailing spaces can be written as "hunter2 # not a comment".
        if len(val) >= 2 and val[0] == val[-1] and val[0] in ("'", '"'):
            val = val[1:-1]
        found[key] = val
    return found


def c_string(value):
    """Render a Python str as a C string literal, escaping what must be escaped."""
    out = ['"']
    for ch in value:
        if ch == "\\":
            out.append("\\\\")
        elif ch == '"':
            out.append('\\"')
        elif ch == "\n":
            out.append("\\n")
        elif ch == "\r":
            out.append("\\r")
        elif ch == "\t":
            out.append("\\t")
        elif ord(ch) < 0x20 or ord(ch) == 0x7F:
            out.append("\\%03o" % ord(ch))
        else:
            out.append(ch)
    out.append('"')
    return "".join(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--env", required=True, help="path to the .env file")
    ap.add_argument("--out", required=True, help="header to generate")
    args = ap.parse_args()

    env_path = pathlib.Path(args.env)
    out_path = pathlib.Path(args.out)
    from_file = parse_env(env_path)

    values = {}
    placeholders = []
    for key, placeholder in KEYS.items():
        val = os.environ.get(key) or from_file.get(key) or ""
        if not val:
            val = placeholder
            placeholders.append(key)
        values[key] = val

    lines = [
        "/*",
        " * Generated from .env by gen_secrets.py. Do not edit, do not commit.",
        " * Edit .env instead (see .env.example).",
        " */",
        "#pragma once",
        "",
    ]
    for key in KEYS:
        lines.append("#define %s %s" % (key, c_string(values[key])))
    lines.append("")
    body = "\n".join(lines)

    # Only touch the file when it actually changes, so a rebuild after an
    # unrelated edit does not invalidate every translation unit that includes it.
    out_path.parent.mkdir(parents=True, exist_ok=True)
    if not out_path.is_file() or out_path.read_text(encoding="utf-8") != body:
        out_path.write_text(body, encoding="utf-8")

    if placeholders:
        where = env_path if env_path.is_file() else "%s (not found)" % env_path
        sys.stderr.write(
            "gen_secrets: using placeholders for %s -- set them in %s\n"
            % (", ".join(sorted(placeholders)), where)
        )
    return 0


if __name__ == "__main__":
    sys.exit(main())
