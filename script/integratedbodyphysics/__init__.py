"""Integrated Body Physics: loads integratedbodyphysics.dll (a Python extension module, the
game's Python has no ctypes) and tells it which Sims jiggle: young adults,
adults and elders with a feminine frame, NPCs included. Never teens or younger, never men.

Console (Ctrl+Shift+C): ibp on | off | breasts <0-2> | butt <0-2> | menu | status
Settings menu without the VR mod: the '.' key (changeable in the menu), see menu.py.
"""
import os
import struct

DLL_NAME = 'integratedbodyphysics.dll'
REFRESH_SECONDS = 2.0
RECORD_BYTES = 32  # id u64, breasts f32, butt f32, flags u32 (1 = the active Sim), nude breast gravity f32 (NaN = setting, -1..1),
#                    nude breasts apart f32 (metres, NaN = setting), cup size i32 (0 small .. 3 huge, -1 = from her CAS size)
RECORD = "<QffIffi"
NAN = float('nan')

_mod = None
_owner = type('_Owner', (), {})()
_alarm = None
_errors = 0
_frame_traits = {}  # trait type -> 'f', 'm' or None (not a frame trait)


def _find_dll():
    path = os.path.dirname(os.path.abspath(__file__))
    for _ in range(5):
        candidate = os.path.join(path, DLL_NAME)
        if os.path.isfile(candidate):
            return candidate
        parent = os.path.dirname(path)
        if parent == path:
            break
        path = parent
    return None


def _log(text):
    if _mod is not None:
        try:
            _mod.log(text)
        except Exception:
            pass


def _load():
    global _mod
    path = _find_dll()
    if path is None:
        return
    import importlib.machinery
    import importlib.util
    loader = importlib.machinery.ExtensionFileLoader('_integratedbodyphysics', path)
    spec = importlib.util.spec_from_file_location('_integratedbodyphysics', path, loader=loader)
    mod = importlib.util.module_from_spec(spec)
    loader.exec_module(mod)
    _mod = mod


def _frame(sim_info):
    """'f' for a feminine body frame, 'm' masculine: from the frame trait if
    the Sim has one (custom gender settings), else from its gender."""
    try:
        for trait in sim_info.trait_tracker.equipped_traits:
            kind = type(trait) if not isinstance(trait, type) else trait
            frame = _frame_traits.get(kind, '')
            if frame == '':
                name = getattr(kind, '__name__', '').lower()
                frame = None
                if 'frame' in name:
                    if 'fem' in name:
                        frame = 'f'
                    elif 'masc' in name:
                        frame = 'm'
                _frame_traits[kind] = frame
            if frame:
                return frame
    except Exception:
        pass
    return 'f' if getattr(sim_info.gender, 'name', '') == 'FEMALE' else 'm'


ADULT_AGES = ('YOUNGADULT', 'ADULT', 'ELDER')

# Outfit categories and body types (sims.outfits.outfit_enums).
BATHING = 5
FULL_BODY, UPPER_BODY, LOWER_BODY = 5, 6, 7


def _parts(sim_info, category, index):
    try:
        outfit = sim_info.get_outfit(category, index)
        return dict(zip(outfit.body_types, outfit.part_ids))
    except Exception:
        return None


def top_part(sim_info):
    """The CAS part over her breasts now (a top, else a full-body outfit), or None."""
    try:
        category, index = sim_info.get_current_outfit()
    except Exception:
        return None
    now = _parts(sim_info, category, index) or {}
    return now.get(UPPER_BODY, now.get(FULL_BODY))


def _clothed(sim_info):
    """Flags 2 (top covers the breasts) and 4 (bottom covers the butt). Nude =
    the part matches the Sim's own bathing (nude) outfit, as when undressed, or
    was set as nude in the pie ("Body Physics > Set as nude"); a top set as
    clothed counts as clothed even in the bathing outfit."""
    try:
        category, index = sim_info.get_current_outfit()
    except Exception:
        return 0
    from integratedbodyphysics import store
    now = _parts(sim_info, category, index)
    if now is None:
        return 6
    bathing = int(category) == BATHING
    nude = _parts(sim_info, BATHING, 0) or {}
    nude_parts = set(nude.values())
    flags = 0
    for bit, body_type in ((2, UPPER_BODY), (4, LOWER_BODY)):
        part = now.get(body_type, now.get(FULL_BODY))
        if part is None:
            continue
        state = store.part_state(part) if bit == 2 else None
        if state == 'clothed' or (state is None and not bathing and part not in nude_parts):
            flags |= bit
    return flags


_ticks = 0


def _records():
    import services
    from integratedbodyphysics import store
    from sims.sim_info_types import Species
    out = []
    seen = []
    manager = services.sim_info_manager()
    if manager is None:
        return b'', 'no sim_info_manager'
    try:
        active = services.active_sim_info()
    except Exception:
        active = None
    for sim in manager.instanced_sims_gen():
        info = sim.sim_info
        # The Sim's species is a plain number here (1 = human), not an enum with a name.
        species = info.species
        human = species == Species.HUMAN or getattr(species, 'name', '') == 'HUMAN'
        species = getattr(species, 'name', str(species))
        age = getattr(info.age, 'name', str(info.age))
        if not human or age not in ADULT_AGES:
            seen.append('{} {} skipped'.format(species, age))
            if active is not None and info.sim_id == active.sim_id:
                out.append(struct.pack(RECORD, sim.id, 0.0, 0.0, 1, NAN, NAN, -1))
            continue
        frame = _frame(info)
        own = 1 if active is not None and info.sim_id == active.sim_id else 0
        if frame != 'f':  # women only (feminine frame): men never jiggle
            seen.append('{} {} skipped'.format(age, frame))
            if own:  # still sent (strength 0): the distance settings measure from your Sim
                out.append(struct.pack(RECORD, sim.id, 0.0, 0.0, 1, NAN, NAN, -1))
            continue
        seen.append('{} {}'.format(age, frame))
        breasts = 1.0
        clothed = _clothed(info)
        seen[-1] += {0: ' nude', 2: ' top on', 4: ' bottom on', 6: ' dressed'}[clothed]
        gravity = store.gravity(info.sim_id)
        apart = store.apart(info.sim_id)
        cup = store.cup(info.sim_id)
        out.append(struct.pack(RECORD, sim.id, breasts, 1.0, own | clothed, NAN if gravity is None else gravity,
                               NAN if apart is None else apart, -1 if cup is None else cup))
        if len(out) >= 96:
            break
    return b''.join(out), ', '.join(seen)


def _tick(_handle=None):
    global _errors
    global _ticks
    try:
        try:
            from integratedbodyphysics import pose
            pose.tick()
        except Exception as e:
            _log('pose tick failed: {!r}'.format(e))
        data, seen = _records()
        found = _mod.set_sims(data)
        _ticks += 1
        if _ticks <= 3 or _ticks % 150 == 0:
            _log('tick {}: sent {}, the game has {} of them as bodies ({})'.format(_ticks, len(data) // RECORD_BYTES, found, seen))
    except Exception as e:
        _errors += 1
        if _errors <= 10:
            _log('refresh failed: {!r}'.format(e))


def _start():
    global _alarm
    if _mod is None or _alarm is not None:
        return
    import alarms
    import clock
    _log('start: listing Sims every {:.0f} s'.format(REFRESH_SECONDS))
    _alarm = alarms.add_alarm_real_time(_owner, clock.interval_in_real_seconds(REFRESH_SECONDS), _tick, repeating=True)
    _tick()
    from integratedbodyphysics import pie
    pie.start()
    from integratedbodyphysics import menu
    menu.start()


def _stop():
    global _alarm, _ticks
    _ticks = 0
    try:
        from integratedbodyphysics import menu
        menu.stop()
    except Exception as e:
        _log('menu stop: {!r}'.format(e))
    alarm, _alarm = _alarm, None
    if alarm is not None:
        import alarms
        alarms.cancel_alarm(alarm)
    if _mod is not None:
        _mod.set_sims(b'')


def _wrap(name, start):
    import zone
    original = getattr(zone.Zone, name, None)
    if original is None:
        _log('Zone.{} not found'.format(name))
        return

    def wrapper(self, *args, **kwargs):
        _log('Zone.{}'.format(name))
        try:
            _start() if start else _stop()
        except Exception as e:
            _log('{}: {!r}'.format(name, e))
        return original(self, *args, **kwargs)

    setattr(zone.Zone, name, wrapper)


def _command():
    import sims4.commands

    @sims4.commands.Command('ibp', command_type=sims4.commands.CommandType.Live)
    def jiggle(what='status', value=None, _connection=None):
        output = sims4.commands.CheatOutput(_connection)
        if _mod is None:
            output('Integrated Body Physics: integratedbodyphysics.dll not loaded')
            return
        if what == 'menu':
            from integratedbodyphysics import menu
            menu.show()
            return
        if what in ('on', 'off'):
            _mod.set('on', 1.0 if what == 'on' else 0.0)
        elif what == 'breasts' and value is not None:  # every size preset's strength
            for size in ('small', 'medium', 'bigger', 'huge'):
                _mod.set(size + '_breast_strength', float(value))
        elif what == 'butt' and value is not None:
            _mod.set(what, float(value))
        try:
            _start()
        except Exception as e:
            output('start failed: {!r}'.format(e))
        hooked, on, breasts, butt, sims = _mod.status()
        output('Integrated Body Physics: {}, {}, breasts {:.2f}, butt {:.2f}, {} Sims'.format(
            'hooked' if hooked else 'NOT hooked (see integratedbodyphysics.log)', 'on' if on else 'off', breasts, butt, sims))


try:
    _load()
    _wrap('on_loading_screen_animation_finished', True)
    _wrap('on_teardown', False)
    _command()
    _log('script loaded')
except Exception as _e:
    _log('script failed: {!r}'.format(_e))
