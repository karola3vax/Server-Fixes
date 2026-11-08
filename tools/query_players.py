"""Asks a CS2 server what the server browser sees (A2S_INFO and A2S_PLAYER) and checks server_list_players.

    python tools/query_players.py <host> [port]     # port 27015 by default
    python tools/query_players.py --self-test

Passes when A2S_PLAYER lists every human A2S_INFO counts with a name. Without the fix, the humans are
missing. GOTV counts as a bot and is listed without a name by the engine itself.
"""

import socket
import struct
import sys

HEADER = b"\xff\xff\xff\xff"


def c_string(data, at):
    end = data.index(b"\0", at)
    return data[at:end].decode("utf-8", "replace"), end + 1


def parse_info(data):
    """A2S_INFO reply (0x49) -> (name, map, players, max_players, bots)."""
    if data[:5] != HEADER + b"I":
        raise ValueError("not an A2S_INFO reply")
    at = 6  # header, type, protocol
    name, at = c_string(data, at)
    map_name, at = c_string(data, at)
    _, at = c_string(data, at)  # folder
    _, at = c_string(data, at)  # game
    players, max_players, bots = data[at + 2], data[at + 3], data[at + 4]  # after the 2-byte app id
    return name, map_name, players, max_players, bots


def parse_players(data):
    """A2S_PLAYER reply (0x44) -> [(name, score, seconds connected)]."""
    if data[:5] != HEADER + b"D":
        raise ValueError("not an A2S_PLAYER reply")
    count, at, players = data[5], 6, []
    for _ in range(count):
        at += 1  # index, always 0
        name, at = c_string(data, at)
        score, seconds = struct.unpack_from("<if", data, at)
        at += 8
        players.append((name, score, seconds))
    return players


def ask(sock, address, request, challenge_suffix):
    """Sends a request; when the server answers with a challenge (0x41), sends it again with that challenge."""
    sock.sendto(HEADER + request + challenge_suffix, address)
    data, _ = sock.recvfrom(65535)
    if data[:5] == HEADER + b"A":
        sock.sendto(HEADER + request + data[5:9], address)
        data, _ = sock.recvfrom(65535)
    return data


def query(host, port, tries=3):
    """UDP: a lost packet is asked again, up to `tries` times."""
    address = (host, port)
    for attempt in range(tries):
        try:
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
                sock.settimeout(5)
                info = parse_info(ask(sock, address, b"TSource Engine Query\0", b""))
                players = parse_players(ask(sock, address, b"U", b"\xff\xff\xff\xff"))
            return info, players
        except TimeoutError:
            if attempt == tries - 1:
                raise


def check(humans, bots, players):
    """None when the list is right, else what is wrong. GOTV is counted as a bot and listed by the engine
    without a name (it has no SteamID for the fix to name it by), so nameless entries are allowed up to the
    bot count."""
    named = sum(1 for player, _, _ in players if player)
    if named < humans:
        return f"{humans} human(s) on, {named} listed with a name"
    if len(players) - named > bots:
        return f"{len(players) - named} nameless entries, more than the {bots} bot(s)/GOTV"
    return None


def self_test():
    info = HEADER + b"I\x11Srv\0de_dust2\0csgo\0Counter-Strike 2\0\xda\x02\x03\x18\x01dl\x00\x01"
    assert parse_info(info) == ("Srv", "de_dust2", 3, 24, 1)
    players = HEADER + b"D\x02" + b"\0Ana\0" + struct.pack("<if", 7, 60.5) + b"\0Bo\0" + struct.pack("<if", -1, 2.0)
    assert parse_players(players) == [("Ana", 7, 60.5), ("Bo", -1, 2.0)]
    assert parse_players(HEADER + b"D\x00") == []
    gotv = ("", 0, 9000.0)
    assert check(1, 1, [gotv, ("Ana", 2, 60.0)]) is None   # what the live server answered with the fix
    assert check(1, 1, [gotv]) is not None                 # without it: the human is missing
    assert check(1, 1, [gotv, ("", 2, 60.0)]) is not None  # listed but nameless
    assert check(0, 0, []) is None
    print("self-test ok")
    return 0


def main(argv):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")  # server names have emoji; Windows consoles default to cp1252
    if argv[1:] == ["--self-test"]:
        return self_test()
    if len(argv) < 2:
        print(__doc__)
        return 2
    host, port = argv[1], int(argv[2]) if len(argv) > 2 else 27015
    (name, map_name, count, max_players, bots), players = query(host, port)
    humans = count - bots
    print(f"{name} on {map_name}: A2S_INFO {count}/{max_players} ({bots} bot(s)), A2S_PLAYER lists {len(players)}")
    for player, score, seconds in players:
        print(f"  {player or '<no name>'}  score {score}  {seconds / 60:.0f} min")
    verdict = check(humans, bots, players)
    if verdict:
        print("FAIL: " + verdict)
        return 1
    print("PASS" if humans else "PASS (no players on: join and run this again for a real check)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
