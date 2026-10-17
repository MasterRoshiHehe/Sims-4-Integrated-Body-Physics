"""What the "Body Physics" pie remembers, in integratedbodyphysics_sims.json next to
integratedbodyphysics.dll (shipped empty so MO2 keeps it in the mod's own folder):
- gravity: Sim id -> her nude breast gravity (the Gravity presets, 0..1)
- apart: Sim id -> how much wider the gap between her breasts is, metres (Spacing presets)
- cup: Sim id -> her size preset (0 Small, 1 Medium, 2 Bigger, 3 Huge; absent = from her CAS size)
- nude_parts / clothed_parts: CAS part ids of tops or full-body outfits that
  count as nude or as clothed, for every Sim ("Set as nude" / "Set as clothed").
"""
import json
import os

FILE_NAME = 'integratedbodyphysics_sims.json'

_data = None
_path = None


def _file():
    global _path
    if _path is None:
        from integratedbodyphysics import _find_dll
        dll = _find_dll()
        _path = os.path.join(os.path.dirname(dll), FILE_NAME) if dll else None
    return _path


_migrated = [False]


def _load():
    global _data
    if _data is not None:
        return _data
    _data = {}
    path = _file()
    try:
        if path and os.path.isfile(path) and os.path.getsize(path) > 0:
            with open(path, 'r') as f:
                _data = json.load(f)
    except Exception as e:
        from integratedbodyphysics import _log
        _log('store: {} unreadable ({!r}), starting empty'.format(FILE_NAME, e))
        _data = {}
    _data.setdefault('gravity', {})
    # 0.11.14: pie Gravity on the debug menu's -1..1 scale (user 2026-10-17 05:38Z: it didn't override the
    # debug menu); 0.9-0.11.13 stored 0..1, so Bolt on 0 .. Saggy 1 become -1 .. 1.
    if _data.get('gravity_scale') != 2:
        _data['gravity'] = {k: 2 * v - 1 for k, v in _data['gravity'].items()}
        _data['gravity_scale'] = 2
        _migrated[0] = True
    _data.setdefault('apart', {})
    _data.setdefault('cup', {})
    _data['nude_parts'] = set(_data.get('nude_parts', []))
    _data['clothed_parts'] = set(_data.get('clothed_parts', []))
    if _migrated[0]:
        _migrated[0] = False
        try:
            _save()
        except Exception:
            pass
    return _data


def _save():
    path = _file()
    if not path:
        return
    d = _load()
    out = {'gravity_scale': 2, 'gravity': d['gravity'], 'apart': d['apart'], 'cup': d['cup'], 'nude_parts': sorted(d['nude_parts']), 'clothed_parts': sorted(d['clothed_parts'])}
    with open(path, 'w') as f:
        json.dump(out, f, indent=1)


def gravity(sim_id):
    """Her nude breast gravity, or None: the debug menu's Gravity."""
    return _load()['gravity'].get(str(sim_id))


def set_gravity(sim_id, value):
    """None: back to her size preset's Gravity (debug menu)."""
    if value is None:
        _load()['gravity'].pop(str(sim_id), None)
    else:
        _load()['gravity'][str(sim_id)] = value
    _save()


def apart(sim_id):
    """Her nude breasts apart (metres), or None: the debug menu's Apart."""
    return _load()['apart'].get(str(sim_id))


def set_apart(sim_id, value):
    """None: back to her size preset's Apart (debug menu)."""
    if value is None:
        _load()['apart'].pop(str(sim_id), None)
    else:
        _load()['apart'][str(sim_id)] = value
    _save()


def cup(sim_id):
    """Her size preset (0..3), or None: picked from her CAS size."""
    return _load()['cup'].get(str(sim_id))


def set_cup(sim_id, value):
    if value is None:
        _load()['cup'].pop(str(sim_id), None)
    else:
        _load()['cup'][str(sim_id)] = value
    _save()


def part_state(part):
    """'nude', 'clothed' or None (the usual rule: nude = her bathing outfit's part)."""
    d = _load()
    if part in d['nude_parts']:
        return 'nude'
    if part in d['clothed_parts']:
        return 'clothed'
    return None


def set_part(part, state):
    d = _load()
    d['nude_parts'].discard(part)
    d['clothed_parts'].discard(part)
    d[state + '_parts'].add(part)
    _save()
