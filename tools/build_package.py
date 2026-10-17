"""Build integratedbodyphysics.package: the pie menu categories "Body Physics" and its
"Gravity" submenu (user 2026-10-15 02:12Z). A category the pie can show needs
three resources: the tuning XML (the game's Python loads it), its SimData (the
client draws the submenu from it) and the English text (a string table, copied
for every language so the names never show blank).

The SimData is a copy of the game's own adopt_Sim category (same schema),
with our values written in. Run with any Python 3:
    python3 tools/build_package.py <output_dir>
"""
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TEMPLATE = os.path.join(ROOT, 'tools', 'piemenucategory_simdata.bin')  # adopt_Sim's SimData (game 1.121)

TUNING = 0x03E9D964
SIMDATA = 0x545AC67A
SIMDATA_GROUP = 0x00E9D967
STBL = 0x220557DA


def fnv32(text):
    h = 0x811C9DC5
    for c in text.lower().encode():
        h = (h * 0x01000193) & 0xFFFFFFFF
        h ^= c
    return h


def fnv64(text):
    h = 0xCBF29CE484222325
    for c in text.lower().encode():
        h = (h * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
        h ^= c
    return h


def instance(name):
    return fnv64('integratedbodyphysics:' + name) | 0x8000000000000000


# Outfit categories, each its own submenu under Outfit (user 2026-10-15 02:44Z). Must match script pie.OUTFITS.
OUTFITS = ['Everyday', 'Formal', 'Athletic', 'Sleep', 'Party', 'Swimwear', 'Hot weather', 'Cold weather', 'Nude']


def outfit_category(name):
    return 'integratedbodyphysics_Outfit_' + name.replace(' ', '')


# name, English text, parent name, display priority (higher shows first: the game's Weddings 999,
# Funeral 900; 0.9.0's 20 still landed past page one, user 02:43Z), collapsible (one entry: shown as that entry)
CATEGORIES = [
    ('integratedbodyphysics_JiggleConfig', 'Body Physics', None, 1000, False),
    ('integratedbodyphysics_CupSize', 'Cup size', 'integratedbodyphysics_JiggleConfig', 5, False),
    ('integratedbodyphysics_Outfit', 'Outfit', 'integratedbodyphysics_JiggleConfig', 4, False),
    ('integratedbodyphysics_Pose', 'Pose', 'integratedbodyphysics_JiggleConfig', 3, False),
    ('integratedbodyphysics_Gravity', 'Gravity', 'integratedbodyphysics_JiggleConfig', 2, False),
    ('integratedbodyphysics_Spacing', 'Spacing', 'integratedbodyphysics_JiggleConfig', 1, False),
    # Your relationship with her (user 2026-10-17 05:38Z).
    ('integratedbodyphysics_Relationship', 'Relationship', 'integratedbodyphysics_JiggleConfig', 0, False),
    ('integratedbodyphysics_Friendship', 'Friendship', 'integratedbodyphysics_Relationship', 2, False),
    ('integratedbodyphysics_Romance', 'Romance', 'integratedbodyphysics_Relationship', 1, False),
] + [(outfit_category(n), n, 'integratedbodyphysics_Outfit', len(OUTFITS) - i, True) for i, n in enumerate(OUTFITS)]


def text_key(name):
    return fnv32('integratedbodyphysics:' + name)


def tuning_xml(name, parent, priority, collapsible):
    lines = ['<?xml version="1.0" encoding="utf-8"?>',
             '<I c="PieMenuCategory" i="pie_menu_category" m="interactions.pie_menu_category" n="{}" s="{}">'.format(
                 name, instance(name)),
             '  <T n="_collapsible">{}</T>'.format('True' if collapsible else 'False'),
             '  <T n="_display_name">0x{:08X}</T>'.format(text_key(name)),
             '  <T n="_display_priority">{}</T>'.format(priority)]
    if parent:
        lines.append('  <T n="_parent">{}</T>'.format(instance(parent)))
    lines.append('</I>')
    return '\n'.join(lines).encode()


def simdata(name, parent, priority, collapsible):
    d = bytearray(open(TEMPLATE, 'rb').read())
    assert d[:4] == b'DATA' and d.endswith(b'PieMenuCategory\0adopt_Sim\0'), 'unexpected template'
    row = 0x40
    d[row] = 1 if collapsible else 0                            # _collapsible
    struct.pack_into('<I', d, row + 4, text_key(name))          # _display_name
    struct.pack_into('<i', d, row + 8, priority)                # _display_priority
    struct.pack_into('<Q', d, row + 32, instance(parent) if parent else 0)  # _parent
    # The table is named after the tuning; its name is the last string.
    d = d[:-len(b'adopt_Sim\0')] + name.encode() + b'\0'
    struct.pack_into('<I', d, 0x24, fnv32(name))
    while len(d) % 4:
        d += b'\0'
    return bytes(d)


def string_table(strings):
    body = bytearray()
    total = 0
    for key, text in strings:
        raw = text.encode('utf-8')
        body += struct.pack('<IBH', key, 0, len(raw)) + raw
        total += len(raw) + 1
    return b'STBL' + struct.pack('<HBQHI', 5, 0, len(strings), 0, total) + bytes(body)


def write_package(path, resources):
    body = bytearray(96)
    entries = []
    for (t, g, i), data in resources:
        entries.append((t, g, i, len(body), len(data)))
        body += data
    index = bytearray(struct.pack('<I', 0))
    for t, g, i, off, size in entries:
        index += struct.pack('<IIIIIIIHH', t, g, i >> 32, i & 0xFFFFFFFF, off, size | 0x80000000, size, 0, 1)
    pos = len(body)
    body += index
    struct.pack_into('<4sII', body, 0, b'DBPF', 2, 1)
    struct.pack_into('<III', body, 36, len(entries), 0, len(index))
    struct.pack_into('<IQ', body, 60, 3, pos)
    open(path, 'wb').write(body)


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    resources = []
    strings = []
    for name, text, parent, priority, collapsible in CATEGORIES:
        resources.append(((TUNING, 0, instance(name)), tuning_xml(name, parent, priority, collapsible)))
        resources.append(((SIMDATA, SIMDATA_GROUP, instance(name)), simdata(name, parent, priority, collapsible)))
        strings.append((text_key(name), text))
    table = string_table(strings)
    stbl_instance = fnv64('integratedbodyphysics:strings') & 0x00FFFFFFFFFFFFFF
    for locale in range(0x12):  # every language the game has: the same English text
        resources.append(((STBL, 0x80000000, (locale << 56) | stbl_instance), table))
    write_package(os.path.join(out, 'integratedbodyphysics.package'), resources)
    for name, text, parent, _, _ in CATEGORIES:
        print('{} = {} ("{}")'.format(name, instance(name), text))


if __name__ == '__main__':
    main()
