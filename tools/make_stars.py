#!/usr/bin/env python3
"""
Generate main/sky/stars.h from the Yale Bright Star Catalogue (BSC5, CDS V/50).

Run by hand; the header is committed, so the build needs no network:

    curl -o /tmp/catalog.gz https://cdsarc.cds.unistra.fr/ftp/V/50/catalog.gz
    gunzip /tmp/catalog.gz
    python3 tools/make_stars.py --bsc /tmp/catalog --out main/sky/stars.h

Constellation figures and proper names are defined here by Bayer designation
("Alp UMa") and resolved against the catalogue; an unknown designation is an
error, so a typo cannot silently draw a wrong line.
"""

import argparse
import pathlib
import sys

MAG_LIMIT = 4.5     # ~900 stars: what a dark suburban sky shows

# Stick figures: each entry is a chain of designations joined in order.
# Chains are split with "|" to start a new line elsewhere in the figure.
FIGURES = {
    "Ursa Major": "Alp UMa,Bet UMa,Gam UMa,Del UMa,Alp UMa|Del UMa,Eps UMa,Zet UMa,Eta UMa",
    "Ursa Minor": "Alp UMi,Del UMi,Eps UMi,Zet UMi,Bet UMi,Gam UMi,Eta UMi,Zet UMi",
    "Cassiopeia": "Eps Cas,Del Cas,Gam Cas,Alp Cas,Bet Cas",
    "Orion": "Lam Ori,Alp Ori,Zet Ori,Kap Ori|Lam Ori,Gam Ori,Del Ori,Bet Ori|Alp Ori,Gam Ori|Del Ori,Eps Ori,Zet Ori",
    "Cygnus": "Alp Cyg,Gam Cyg,Bet Cyg|Del Cyg,Gam Cyg,Eps Cyg",
    "Lyra": "Alp Lyr,Zet Lyr,Del Lyr,Gam Lyr,Bet Lyr,Zet Lyr",
    "Aquila": "Gam Aql,Alp Aql,Bet Aql|Alp Aql,Del Aql,Lam Aql|Del Aql,Zet Aql",
    "Leo": "Alp Leo,Eta Leo,Gam Leo,Zet Leo,Mu Leo,Eps Leo|Gam Leo,Del Leo,Bet Leo,The Leo,Alp Leo|Del Leo,The Leo",
    "Gemini": "Alp Gem,Bet Gem|Alp Gem,Tau Gem,Eps Gem,Mu Gem,Eta Gem|Bet Gem,Del Gem,Zet Gem,Gam Gem",
    "Taurus": "Gam Tau,Del Tau,Eps Tau,Bet Tau|Gam Tau,The Tau,Alp Tau,Zet Tau",
    "Auriga": "Alp Aur,Bet Aur,The Aur,Bet Tau,Iot Aur,Alp Aur",
    "Canis Major": "Bet CMa,Alp CMa,Del CMa,Eps CMa|Del CMa,Eta CMa",
    "Canis Minor": "Alp CMi,Bet CMi",
    "Perseus": "Gam Per,Alp Per,Del Per,Eps Per,Zet Per|Alp Per,Bet Per",
    "Pegasus": "Alp Peg,Bet Peg,Alp And,Gam Peg,Alp Peg|Alp Peg,Zet Peg,The Peg,Eps Peg|Bet Peg,Eta Peg",
    "Andromeda": "Alp And,Del And,Bet And,Gam And",
    "Bootes": "Alp Boo,Eps Boo,Del Boo,Bet Boo,Gam Boo,Rho Boo,Alp Boo|Alp Boo,Eta Boo",
    "Corona Borealis": "The CrB,Bet CrB,Alp CrB,Gam CrB,Del CrB,Eps CrB",
    "Scorpius": "Bet Sco,Del Sco,Pi Sco|Del Sco,Sig Sco,Alp Sco,Tau Sco,Eps Sco,Mu Sco,Zet Sco,Eta Sco,The Sco,Iot Sco,Kap Sco,Lam Sco",
    "Virgo": "Alp Vir,Gam Vir,Del Vir,Eps Vir|Gam Vir,Eta Vir,Bet Vir",
    "Sagittarius": "Gam Sgr,Del Sgr,Eps Sgr,Gam Sgr|Del Sgr,Lam Sgr,Phi Sgr,Del Sgr|Phi Sgr,Sig Sgr,Tau Sgr,Zet Sgr,Phi Sgr|Zet Sgr,Eps Sgr",
    "Aries": "Alp Ari,Bet Ari,Gam Ari",
}

NAMES = {
    "Alp CMa": "Sirius", "Alp Boo": "Arcturus", "Alp Lyr": "Vega", "Alp Aur": "Capella",
    "Bet Ori": "Rigel", "Alp CMi": "Procyon", "Alp Ori": "Betelgeuse", "Alp Aql": "Altair",
    "Alp Tau": "Aldebaran", "Alp Sco": "Antares", "Alp Vir": "Spica", "Bet Gem": "Pollux",
    "Alp PsA": "Fomalhaut", "Alp Cyg": "Deneb", "Alp Leo": "Regulus", "Alp Gem": "Castor",
    "Gam Ori": "Bellatrix", "Alp UMi": "Polaris", "Alp Per": "Mirfak", "Bet Per": "Algol",
    "Alp UMa": "Dubhe", "Eta UMa": "Alkaid", "Alp Hya": "Alphard", "Bet Leo": "Denebola",
    "Zet UMa": "Mizar", "Alp Ari": "Hamal", "Alp And": "Alpheratz", "Alp Oph": "Rasalhague",
    "Alp Cas": "Schedar", "Bet Cyg": "Albireo",
}


def parse(path):
    stars = []
    for r in open(path, encoding="latin-1").read().splitlines():
        if len(r) < 107 or not r[75:83].strip() or not r[102:107].strip():
            continue        # novae and other entries with no position or magnitude
        ra = (int(r[75:77]) + int(r[77:79]) / 60 + float(r[79:83]) / 3600) * 15
        dec = int(r[84:86]) + int(r[86:88]) / 60 + int(r[88:90]) / 3600
        if r[83] == "-":
            dec = -dec
        bv = float(r[109:114]) if len(r) >= 114 and r[109:114].strip() else 0.6
        greek, con = r[7:10].strip(), r[11:14].strip()
        stars.append({"ra": ra, "dec": dec, "mag": float(r[102:107]), "bv": bv,
                      "key": "%s %s" % (greek, con) if greek and con else None})
    return stars


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bsc", required=True, help="BSC5 'catalog' file (CDS V/50)")
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    allstars = parse(args.bsc)
    # Brightest star per designation, so "Zet Lyr" means the brighter of zeta-1/zeta-2.
    by_key = {}
    for s in allstars:
        if s["key"] and (s["key"] not in by_key or s["mag"] < by_key[s["key"]]["mag"]):
            by_key[s["key"]] = s

    keep = [s for s in allstars if s["mag"] <= MAG_LIMIT]
    # Figure stars and named stars are kept even if fainter than the limit.
    wanted = {k.strip() for f in FIGURES.values() for chain in f.split("|") for k in chain.split(",")}
    wanted |= set(NAMES)
    missing = sorted(k for k in wanted if k not in by_key)
    if missing:
        sys.exit("make_stars: unknown designations: %s" % ", ".join(missing))
    for k in wanted:
        if by_key[k] not in keep:
            keep.append(by_key[k])
    keep.sort(key=lambda s: s["mag"])   # brightest first: labels pick from the front
    index = {id(s): i for i, s in enumerate(keep)}

    lines = []
    for name, fig in FIGURES.items():
        for chain in fig.split("|"):
            keys = [k.strip() for k in chain.split(",")]
            for a, b in zip(keys, keys[1:]):
                lines.append((index[id(by_key[a])], index[id(by_key[b])]))

    out = [
        "/*",
        " * Generated by tools/make_stars.py from the Yale Bright Star Catalogue",
        " * (BSC5, CDS V/50): stars to magnitude %.1f, J2000. Do not edit by hand." % MAG_LIMIT,
        " */",
        "#pragma once",
        "",
        "#include <stdint.h>",
        "",
        "typedef struct {",
        "    float ra, dec;          /* degrees, J2000 */",
        "    int8_t mag10;           /* visual magnitude x10 */",
        "    int8_t bv10;            /* B-V colour index x10: blue < 0 < white < orange */",
        "} sky_star_t;",
        "",
        "/* Brightest first. */",
        "static const sky_star_t SKY_STARS[] = {",
    ]
    for s in keep:
        out.append("    { %.4ff, %.4ff, %d, %d }," % (s["ra"], s["dec"], round(s["mag"] * 10),
                                                     max(-127, min(127, round(s["bv"] * 10)))))
    out += ["};", "#define SKY_STAR_COUNT ((int)(sizeof(SKY_STARS) / sizeof(SKY_STARS[0])))", "",
            "/* Constellation stick figures: pairs of indices into SKY_STARS. */",
            "static const uint16_t SKY_LINES[][2] = {"]
    out += ["    { %d, %d }," % l for l in lines]
    out += ["};", "#define SKY_LINE_COUNT ((int)(sizeof(SKY_LINES) / sizeof(SKY_LINES[0])))", "",
            "static const struct { uint16_t star; const char *name; } SKY_NAMES[] = {"]
    for k, n in sorted(NAMES.items(), key=lambda kv: index[id(by_key[kv[0]])]):
        out.append('    { %d, "%s" },' % (index[id(by_key[k])], n))
    out += ["};", "#define SKY_NAME_COUNT ((int)(sizeof(SKY_NAMES) / sizeof(SKY_NAMES[0])))", ""]

    pathlib.Path(args.out).write_text("\n".join(out), encoding="utf-8")
    print("wrote %d stars, %d figure lines, %d names to %s" % (len(keep), len(lines), len(NAMES), args.out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
