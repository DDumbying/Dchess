"""Scratch homes with the profiles and history the demo scenes need."""
import datetime, os, random, shutil, sys

root = sys.argv[1]
random.seed(7)

def home(name, profiles):
    d = os.path.join(root, name)
    shutil.rmtree(d, ignore_errors=True)
    os.makedirs(f"{d}/.config/dchess")
    os.makedirs(f"{d}/.local/share/dchess")
    if profiles:
        open(f"{d}/.config/dchess/profiles.conf", "w").write(profiles)
    return d

home("welcome", None)
home("play", "active = saeed\n\n[saeed]\ntheme  = gruvbox\nblack  = medium\nclock  = 5+3\nanalysis = builtin\n")
home("replay", "active = saeed\n\n[saeed]\ntheme  = gruvbox\n")
stats = home("stats", "active = saeed\n\n[saeed]\ntheme  = gruvbox\nclock  = 5+3\n\n[alice]\ntheme  = catppuccin\n")

lines = [
    "1. e4 e5 2. Nf3 Nc6 3. Bb5 a6 4. Ba4 Nf6 5. O-O Be7 6. Re1 b5 7. Bb3 d6 8. c3 O-O",
    "1. d4 d5 2. c4 e6 3. Nc3 Nf6 4. Bg5 Be7 5. e3 O-O 6. Nf3 Nbd7 7. Rc1 c6",
    "1. e4 c5 2. Nf3 d6 3. d4 cxd4 4. Nxd4 Nf6 5. Nc3 a6 6. Be2 e5 7. Nb3 Be7",
    "1. c4 e5 2. Nc3 Nf6 3. Nf3 Nc6 4. g3 d5 5. cxd5 Nxd5 6. Bg2 Nb6 7. O-O Be7",
]
opponents = [("dchess", "dchess", "Hard"), ("dchess", "dchess", "Medium"), ("dchess", "dchess", "Easy"),
             ("Stockfish 17", "engine", "1500 Elo"), ("alice", "profile", ""), ("Guest", "guest", "")]
controls = ["300+3", "300+3", "60+0", "180+2", "900+10", "", "300+3"]
flip = {"1-0": "0-1", "0-1": "1-0"}
games = []
for i in range(34):
    name, kind, strength = random.choice(opponents)
    mine = random.choices(["1-0", "0-1", "1/2-1/2"], [5, 3, 2])[0]
    end = "repetition" if mine == "1/2-1/2" else random.choice(["checkmate", "checkmate", "resigned", "time"])
    white = random.random() < 0.55
    when = datetime.datetime(2026, 9, 27, 10 + i % 10, 15) - datetime.timedelta(days=60 - i * 1.7)
    w, wk, b, bk = ("saeed", "profile", name, kind) if white else (name, kind, "saeed", "profile")
    res = mine if white else flip.get(mine, mine)
    tags = [("Event", "Casual game"), ("Site", "dchess"), ("Date", when.strftime("%Y.%m.%d")), ("Round", "-"),
            ("White", w), ("Black", b), ("Result", res), ("Time", when.strftime("%H:%M:%S")),
            ("WhiteKind", wk), ("BlackKind", bk)]
    if strength:   # the opponent's strength, on the opponent's side
        tags.append(("BlackStrength" if white else "WhiteStrength", strength))
    plies = random.randint(30, 110)
    tags += [("EndReason", end), ("PlyCount", str(plies)), ("Seconds", str(plies * 7))]
    tc = random.choice(controls)
    if tc:
        tags.append(("TimeControl", tc))
    games.append("".join(f'[{k} "{v}"]\n' for k, v in tags) + "\n" + random.choice(lines) + f" {res}\n\n")
open(f"{stats}/.local/share/dchess/games.pgn", "w").write("".join(games))
