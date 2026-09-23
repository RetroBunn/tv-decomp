"""Grow the corpus by keeping only the inputs that reach new code.

Usage: python tools/covgen.py <out.txt> [--kind phon|text|esc] [--rounds N]
                             [--batch N] [--seed N] [-j N]

The duration tables in stage 2 and the rule routines in stage 3 branch on
what sits either side of a phoneme, how far away the next stress is and
where the word boundaries fall.  Writing frames by hand reaches a fraction
of that; generating candidates at random and keeping the ones that light up
a basic block nothing else has reached covers it far better.

Each round makes a batch of candidates, runs the original over all of them
with block coverage on, and keeps -- one at a time, so none is kept for
blocks another already covered -- those that add something.  The kept lines
are written out as a corpus file.
"""
import argparse
import concurrent.futures as cf
import os
import random
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import difftest  # noqa: E402

ROOT = difftest.ROOT
BLOCKS = os.path.join(ROOT, "work", "cgrm_en", "blocks.txt")
ESC = "\x1b"

CONS = ["B", "CH", "D", "DH", "DT", "F", "G", "HH", "HW", "HX", "JH", "K",
        "L", "LX", "M", "N", "NG", "P", "QQ", "R", "S", "SH", "T", "TH",
        "V", "W", "XX", "Y", "Z", "ZH"]
VOWEL = ["AA", "AE", "AH", "AO", "AR", "AW", "AX", "AY", "EH", "ER", "EY",
         "IH", "IR", "IX", "IY", "OR", "OW", "OY", "RR", "UH", "UL", "UM",
         "UN", "UR", "UW", "UX", "YR"]
PH_MARK = ["!", '"', "~", "#", "$", "%", "&", "'", "*", "+", "-", "/", ":",
           "<", "=", "@", "\\", "_", "`", "{", "|", "}"]
END = [".", ",", "?", "!", ";", ":", " --", "..."]

WORDS = ["Mr.", "Dr.", "St.", "Inc.", "etc.", "e.g.", "i.e.", "vs.",
         "they", "we", "you", "not", "will", "can"]
# The closed-class lists the engine keeps (lexicon.c: g_words_article and
# the rest).  Stage 2 sizes a phoneme partly by the class of the word it is
# in and of the word before, so the duration rules are only reachable with
# words the classifier recognises.
CLASSED = (
    "a an the he she it such other another much some each any half "
    "every little few many all several both and or yet nor but unless "
    "whether if although while lest because as since anyway though "
    "after until till before when whenever why whyever how that what "
    "whatever who whom whomever which whichever where wherever whose "
    "either neither than despite between without by via in from like "
    "into through upon against beside besides near with within at on "
    "beyond plus minus throughout across behind nearby aboard atop "
    "during toward per about off of to unlike for contrary among zero "
    "one two three four five six seven eight nine ten eleven twelve "
    "hundred thousand million billion first second third last next our "
    "his her their my your its new old be am are is was were been being "
    "have has had having this these those let's").split()
# Content words with a fricative in every position, for the widest of the
# duration trees.
CONTENT = ("safe five thief wash badge judge these those thus breathe "
           "fifth sixths asks fists wasps crisps months clothes "
           "mischief office assist success surface fashion vision measure "
           "pleasure treasure leisure usual casual visual "
           "haphazard housefly bookshelf switchback dishcloth "
           "sphere sphinx thistle thousandth "
           "yes this fuss buzz fizz fuzz muff love serve curve").split()
FRAGS = ["ough", "tion", "ight", "augh", "eigh", "sch", "phth", "rh", "wr",
         "kn", "gn", "mb", "lk", "que", "cch", "dge", "tch", "ngue", "ps",
         "x", "qu", "y", "'s", "'t", "'re", "'ll", "'ve", "n't"]
# The shapes TextIn_Expand rewrites: a leading minus, a spelled-out run of
# one vowel, the words it splits, a time, a month between numbers, and the
# cp1252 symbols it has a word for.
SHAPES = ["-", "- ", "unknown", "unsent", "UnKnown", "UNSENT",
          "am", "AM", "pm", "jan", "Jan", "JAN", "mar", "Mar", "MAR",
          "jan.", "mar.", "jan-", "mar-"]


def phon_word(rnd, pool):
    out = []
    for _ in range(rnd.randint(1, 10)):
        if rnd.random() < 0.4:
            v = rnd.choice(VOWEL)
            if rnd.random() < 0.5:
                v += rnd.choice("012")
            out.append(v)
        else:
            out.append(rnd.choice(pool))
        if rnd.random() < 0.08:
            out.append("#")      # a word boundary inside the bracket
    if rnd.random() < 0.15:
        out.append(rnd.choice(PH_MARK))
    return " ".join(out)


def phon_line(rnd):
    # Stage 2 looks a fixed distance ahead for the next stress, so how long
    # the phrase is changes what it finds; and some rules only fire on a
    # particular consonant next to a particular one, so sometimes draw from
    # a narrowed pool to make those pairs likely.
    pool = CONS
    if rnd.random() < 0.4:
        pool = rnd.sample(CONS, rnd.randint(2, 6))
    n = rnd.randint(1, 8)
    return " ".join("[%s]" % phon_word(rnd, pool) for _ in range(n)) \
        + rnd.choice(END)


def text_word(rnd):
    r = rnd.random()
    if r < 0.14:
        return rnd.choice(SHAPES)
    if r < 0.18:
        # a run of one vowel, which the front end spells out
        return rnd.choice("aeiouy") * rnd.randint(1, 6)
    if r < 0.21:
        # the cp1252 symbols that have a word of their own, written as the
        # character that byte stands for: corpus files are UTF-8 and
        # difftest recodes them to cp1252 on the way in
        return bytes([rnd.randint(0xa0, 0xff)]).decode("cp1252")
    if r < 0.24:
        return rnd.choice("abcdefghijklmnopqrstuvwxyz") + rnd.choice(["!", "! "])
    if r < 0.30:
        return rnd.choice(CLASSED)
    if r < 0.42:
        return rnd.choice(CONTENT)
    if r < 0.48:
        return rnd.choice(WORDS)
    if r < 0.58:
        n = rnd.randint(0, 10 ** rnd.randint(1, 9))
        fmt = rnd.choice(["%d", "%d.%02d", "%d%%", "$%d", "%dth", "%d-%d",
                          "%d:%02d", "%d/%d", "%d,%03d"])
        return fmt % ((n, n % 100) if fmt.count("%") == 2 else (n,))
    if r < 0.65:
        w = "".join(rnd.choice("abcdefghijklmnopqrstuvwxyz")
                    for _ in range(rnd.randint(1, 9)))
        if rnd.random() < 0.4:
            w += rnd.choice(FRAGS)
        return w.capitalize() if rnd.random() < 0.25 else w
    if r < 0.8:
        return "".join(rnd.choice("ABCDEFGHIJKLMNOPQRSTUVWXYZ")
                       for _ in range(rnd.randint(2, 5)))
    return rnd.choice(FRAGS) + rnd.choice(FRAGS)


def text_line(rnd):
    return " ".join(text_word(rnd) for _ in range(rnd.randint(1, 9))) \
        + rnd.choice(END)


def esc_line(rnd):
    cmd = rnd.choice("ACEFIKNPQSVfgilprvw")
    args = ";".join(str(rnd.randint(0, 300)) for _ in range(rnd.randint(0, 3)))
    return "%s%s[%s%s%s" % (text_line(rnd)[:20], ESC, args, cmd, text_line(rnd))


FRIC = ["S", "Z", "SH", "ZH", "F", "V", "TH", "DH", "HH", "HX", "CH", "JH"]


def fric_line(rnd):
    """Phoneme input built around one fricative at a time.

    Stage2_DurFric is the widest decision tree in the engine: which table a
    fricative reads depends on what is either side of it, whether the
    syllable is stressed, how far ahead the next stress is and whether a
    pause follows.  Putting a fricative in every word and varying the rest
    makes those combinations come up often enough for the search to find
    the ones that reach new code.
    """
    f = rnd.choice(FRIC)
    words = []
    for _ in range(rnd.randint(1, 6)):
        w = []
        for _ in range(rnd.randint(0, 3)):
            w.append(rnd.choice(CONS + VOWEL))
        w.append(f if rnd.random() < 0.8 else rnd.choice(FRIC))
        for _ in range(rnd.randint(0, 3)):
            p = rnd.choice(CONS + VOWEL)
            if p in VOWEL and rnd.random() < 0.6:
                p += rnd.choice("012")
            w.append(p)
        if rnd.random() < 0.2:
            w.append(rnd.choice(PH_MARK))
        words.append("[%s]" % " ".join(w))
    return " ".join(words) + rnd.choice(END)


# One phoneme per group of the fricative duration tree, worked out from
# g_fric_prev_idx and g_fric_next_idx in the image.
FRIC_PREV = ["IY", "B", "CH", "F", "L", "M", "V", "UM"]
FRIC_NEXT = ["IY", "B", "CH", "F", "L", "M", "V"]
_fricmap = None


def fricmap_line(rnd):
    """Walk the fricative duration tree on purpose rather than at random.

    Which rule a fricative reads is picked by the group of the phoneme
    before it, the group of the one after, whether its syllable is stressed
    and whether a stress follows within the look-ahead window.  These are
    all four crossed, one phoneme per group; after them it falls back to
    the random shapes.
    """
    global _fricmap
    if _fricmap is None:
        _fricmap = iter(["[AA %s %s %s AA%s]%s" % (p, f, n, s, t)
                         for f in FRIC for p in FRIC_PREV for n in FRIC_NEXT
                         for s in ("", "1")
                         for t in (".", " [T IY1 T].", ", [M AA M].")])
    try:
        return next(_fricmap)
    except StopIteration:
        return fric_line(rnd)


GEN = {"phon": phon_line, "text": text_line, "esc": esc_line,
       "fric": fric_line, "fricmap": fricmap_line}


def hits_of(job):
    """Run the original over one candidate and return the blocks it reached."""
    text, kind, tmp, extra = job
    src = os.path.join(tmp, "in.txt")
    hits = os.path.join(tmp, "hits.txt")
    wav = os.path.join(tmp, "out.wav")
    head = (ESC + "[6;7A") if kind in ("phon", "fric", "fricmap") else ""
    with open(src, "w", encoding="cp1252", errors="replace", newline="\n") as fp:
        fp.write(head + text + "\n")
    r = subprocess.run([difftest.TVH, "-c", BLOCKS, "-C", hits] + extra +
                       [difftest.DLL, "@" + src, wav],
                       capture_output=True, text=True)
    if r.returncode != 0 or not os.path.exists(hits):
        return text, None
    with open(hits, encoding="utf-8") as fp:
        return text, set(line.split()[0] for line in fp if line.strip())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--kind", default="phon", choices=sorted(GEN))
    ap.add_argument("--rounds", type=int, default=40)
    ap.add_argument("--batch", type=int, default=32)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("-j", type=int, default=os.cpu_count() or 4)
    ap.add_argument("--baseline", default=os.path.join(ROOT, "work", "cov_merged.txt"))
    ap.add_argument("--args", default="-v 0",
                    help="harness options to generate for; write the same "
                         "ones into <out>.opts so the corpus uses them too")
    a = ap.parse_args()

    covered = set()
    if os.path.exists(a.baseline):
        with open(a.baseline, encoding="utf-8") as fp:
            covered = set(line.split()[0] for line in fp if line.strip())
    print("baseline: %d blocks" % len(covered))

    extra = a.args.split()
    rnd = random.Random(a.seed)
    gen = GEN[a.kind]
    kept = []
    tmps = [tempfile.mkdtemp(prefix="covgen") for _ in range(a.j)]
    with cf.ThreadPoolExecutor(a.j) as ex:
        for rd in range(a.rounds):
            batch = [(gen(rnd), a.kind, tmps[i % a.j], extra)
                     for i in range(a.batch)]
            got = 0
            for text, hits in ex.map(hits_of, batch):
                if hits is None:
                    continue
                gain = hits - covered
                if gain:
                    covered |= hits
                    kept.append(text)
                    got += len(gain)
            print("round %2d/%d: +%d blocks, %d lines kept, %d covered"
                  % (rd + 1, a.rounds, got, len(kept), len(covered)))

    head = (ESC + "[6;7A") if a.kind in ("phon", "fric", "fricmap") else ""
    # corpus files are UTF-8; difftest recodes them to cp1252 on load
    with open(a.out, "w", encoding="utf-8", newline="\n") as fp:
        for i, line in enumerate(kept):
            fp.write((head if i == 0 else "") + line + "\n")
    if extra != ["-v", "0"]:
        with open(a.out[:-4] + ".opts", "w", newline="\n") as fp:
            fp.write("\n".join(extra) + "\n")
    print("wrote %s: %d lines, %d bytes"
          % (a.out, len(kept), os.path.getsize(a.out)))


if __name__ == "__main__":
    main()
