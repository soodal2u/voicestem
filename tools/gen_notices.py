# SPDX-License-Identifier: AGPL-3.0-or-later
"""Generates THIRD_PARTY_NOTICES.txt for the VoiceStem binary.

Collects the licence texts of everything compiled into the plugin:
  - DeepFilterNet (libDF + the two embedded models), from the cargo git checkout
  - every Rust crate linked into rust/ (Windows target, normal dependencies)
  - the third-party code bundled inside the JUCE modules VoiceStem compiles

Identical licence texts are printed once with the list of components using them.

    python tools/gen_notices.py <juce-dir> <output-file>
"""

import json
import os
import subprocess
import sys
from collections import OrderedDict
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RUST = ROOT / "rust"
TARGET = "x86_64-pc-windows-msvc"

# Only the JUCE dependencies compiled into VoiceStem's modules (juce_core, _events, _graphics,
# _gui_basics, _gui_extra, _data_structures, _audio_basics, _audio_devices, _audio_formats,
# _audio_processors(_headless), _audio_utils, _dsp). ASIO, AAX, AU, Oboe, LV2 hosting, OpenGL,
# Box2D and the JavaScript engine are not built.
JUCE_DEPS = [
    ("VST3 SDK (Steinberg Media Technologies)", "modules/juce_audio_processors_headless/format_types/VST3_SDK/LICENSE.txt"),
    ("FLAC (Xiph.Org Foundation)", "modules/juce_audio_formats/codecs/flac/Flac Licence.txt"),
    ("Ogg Vorbis (Xiph.Org Foundation)", "modules/juce_audio_formats/codecs/oggvorbis/Ogg Vorbis Licence.txt"),
    ("libjpeg (Independent JPEG Group)", "modules/juce_graphics/image_formats/jpglib/README"),
    ("libpng", "modules/juce_graphics/image_formats/pnglib/LICENSE"),
    ("zlib (Jean-loup Gailly and Mark Adler)", "modules/juce_core/zip/zlib/README"),
    ("HarfBuzz", "modules/juce_graphics/fonts/harfbuzz/COPYING"),
    ("SheenBidi (Muhammad Tayyab Akram)", "modules/juce_graphics/unicode/sheenbidi/LICENSE"),
]

LICENSE_FILE_PREFIXES = ("license", "licence", "copying", "notice", "unlicense", "copyright")

MIT_TEMPLATE = """MIT License

Copyright (c) {holders}

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE."""


def cargo(*args):
    env = dict(os.environ, VOICESTEM_MODEL_DIR=os.environ.get("VOICESTEM_MODEL_DIR", str(RUST)))
    return subprocess.run(["cargo", *args], cwd=RUST, env=env, check=True,
                          capture_output=True, text=True, encoding="utf-8").stdout


def linked_crates():
    """name -> version of crates linked for the Windows target (normal edges only)."""
    tree = cargo("tree", "--target", TARGET, "-e", "normal", "--prefix", "none", "--format", "{p}")
    crates = {}
    for line in tree.splitlines():
        parts = line.replace(" (*)", "").split()
        if len(parts) >= 2 and parts[1].startswith("v"):
            crates[(parts[0], parts[1][1:])] = True
    return crates


def licence_files(directory: Path):
    if not directory.is_dir():
        return []
    return sorted(p for p in directory.iterdir()
                  if p.is_file() and p.name.lower().startswith(LICENSE_FILE_PREFIXES))


def read(p: Path):
    return p.read_text(encoding="utf-8", errors="replace").strip().replace("\r\n", "\n")


def main():
    juce = Path(sys.argv[1])
    out = Path(sys.argv[2])

    meta = json.loads(cargo("metadata", "--format-version", "1", "--filter-platform", TARGET))
    linked = linked_crates()

    texts = OrderedDict()   # licence text -> [component names]
    listing = []            # (component, licence expression, source)

    def add(text, who):
        texts.setdefault(text, []).append(who)

    # DeepFilterNet: crate plus models, licence files live at the repository root.
    for pkg in meta["packages"]:
        if pkg["name"] == "deep_filter":
            repo = Path(pkg["manifest_path"]).parent.parent
            who = f"DeepFilterNet {pkg['version']} (libDF + DeepFilterNet3 / DeepFilterNet3-LL models), Hendrik Schröter"
            listing.append((who, "MIT OR Apache-2.0", "https://github.com/Rikorose/DeepFilterNet"))
            for f in licence_files(repo):
                add(read(f), who)

    for pkg in sorted(meta["packages"], key=lambda p: (p["name"], p["version"])):
        key = (pkg["name"], pkg["version"])
        if key not in linked or pkg["name"] in ("dfwrap", "deep_filter"):
            continue
        who = f"{pkg['name']} {pkg['version']}"
        listing.append((who, pkg.get("license") or "see licence file", pkg.get("repository") or ""))
        files = licence_files(Path(pkg["manifest_path"]).parent)
        if files:
            for f in files:
                add(read(f), who)
        elif "MIT" in (pkg.get("license") or ""):
            # The crate ships no licence file; reproduce the MIT terms with its authors.
            authors = ", ".join(a.split("<")[0].strip() for a in pkg.get("authors", [])) or f"the {pkg['name']} authors"
            add(MIT_TEMPLATE.format(holders=authors), who)
        else:
            raise SystemExit(f"{who}: no licence file and not MIT ({pkg.get('license')}) - add its text by hand")

    for name, rel in JUCE_DEPS:
        who = f"{name} - bundled in JUCE"
        listing.append((who, "see licence text", "https://github.com/juce-framework/JUCE"))
        add(read(juce / rel), who)

    with out.open("w", encoding="utf-8", newline="\n") as f:
        f.write("VoiceStem - third-party notices\n")
        f.write("================================\n\n")
        f.write("VoiceStem is licensed under the GNU Affero General Public License v3 (see LICENSE).\n")
        f.write("Complete source code: https://github.com/soodal2u/voicestem\n\n")
        f.write("It is built with the JUCE framework (https://juce.com), used under the AGPLv3,\n")
        f.write("and includes the following third-party components.\n\n")
        f.write("Components\n----------\n")
        for who, lic, src in listing:
            f.write(f"- {who}: {lic}" + (f"  <{src}>" if src else "") + "\n")
        f.write("\n\nLicence texts\n-------------\n")
        for text, users in texts.items():
            f.write("\n" + "=" * 78 + "\n")
            f.write("Applies to: " + ", ".join(users) + "\n")
            f.write("=" * 78 + "\n\n" + text + "\n")

    print(f"{out}: {len(listing)} components, {len(texts)} distinct licence texts")


if __name__ == "__main__":
    main()
