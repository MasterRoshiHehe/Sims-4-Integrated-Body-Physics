"""The "Body Physics" pie submenu on women's pie menus (user 2026-10-15
01:23Z / 02:12Z: in first person you can't change Sims' clothes):
- Outfit > Everyday, Formal, ... Nude, each a submenu of her outfits in it
  (Everyday 1, 2, ...; one outfit: shown directly): changes it at once.
- Set as nude / Set as clothed: makes the top (or full-body outfit) she wears
  count as nude or clothed for the breast jiggle, for every Sim (store.py).
- Pose > Neutral, Dance, Stretch, Let her go (pose.py): holds her in it.
- Spacing > Very together ... Very apart: her nude breasts apart; the debug
  menu's "Clothed: apart" adds to it while clothed.
- Gravity > Bolt on, Enhanced, Natural, Heavy, Saggy: her own nude breast
  gravity; the debug menu's "Clothed: gravity" adds to it while clothed.
- Cup size > Auto, Small, Medium, Bigger, Huge (user 2026-10-15 03:54Z): which
  of the debug menu's four breast size presets she uses; Auto = picked from
  her CAS breast size.
The butt is left out on purpose (user: act as if it doesn't exist for now).

The submenus are pie menu categories from integratedbodyphysics.package (tools/
build_package.py). Far Sims aren't called over (vr_from_afar, read by the
Sims 4 VR mod's reach gate). Only on Sims that jiggle (young adult and up,
feminine frame). Switch: outfit_pie in integratedbodyphysics.ini (debug menu: Jiggle >
Body Physics on Sims).
"""
GUID_BASE = 0x434A4F5554000000  # 'CJOUT' + a number per option



def _instance(name):
    """A category's tuning instance, as tools/build_package.py makes it."""
    h = 0xCBF29CE484222325
    for c in ('integratedbodyphysics:' + name).lower().encode():
        h = (h * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
        h ^= c
    return h | 0x8000000000000000


OUTFITS_PER_CATEGORY = 5  # CAS allows five per category

# (OutfitCategory value, name). BATHING (5) is the nude one.
OUTFITS = (
    (0, 'Everyday'), (1, 'Formal'), (2, 'Athletic'), (3, 'Sleep'), (4, 'Party'),
    (9, 'Swimwear'), (10, 'Hot weather'), (11, 'Cold weather'), (5, 'Nude'),
)
# Gravity presets on the debug menu's Gravity scale (-1..1; 0: the pose already hangs them). 0.11.14: was
# 0..1, so next to a debug value of 0.65 the presets barely differed (user 2026-10-17 05:38Z).
GRAVITY = (('Bolt on', -1.0), ('Enhanced', -0.5), ('Natural', 0.0), ('Heavy', 0.5), ('Saggy', 1.0))
# Spacing presets: how much wider the gap between her breasts gets, metres (user 2026-10-15 03:18Z).
SPACING = (('Very together', -0.03), ('Together', -0.015), ('Natural', 0.0), ('Apart', 0.015), ('Very apart', 0.03))
# Your relationship with her (user 2026-10-17 05:38Z), the game's -100..100 tracks. The labels are the
# game's own friendship / romance level names near those scores.
FRIENDSHIP = (('Enemy', -100), ('Disliked', -50), ('Neutral', 0), ('Acquaintance', 20), ('Friend', 40),
              ('Good friend', 70), ('Best friend', 100))
ROMANCE = (('Hostile', -50), ('None', 0), ('Flirty', 25), ('Romantic', 50), ('Lovers', 75), ('Soulmates', 100))
# Size presets, in the DLL's order (small_breast_..., medium_breast_...).
SIZES = ('Small', 'Medium', 'Bigger', 'Huge')

_affordances = []
_wrapped = False
_logs = 0


def _log(text):
    global _logs
    _logs += 1
    if _logs <= 60:
        from integratedbodyphysics import _log as log
        log('pie: ' + text)


def _on():
    from integratedbodyphysics import _mod
    try:
        return _mod is not None and (_mod.get('outfit_pie') or 0) != 0
    except Exception:
        return False


def _eligible(sim):
    if sim is None or not getattr(sim, 'is_sim', False):
        return False
    from integratedbodyphysics import ADULT_AGES, _frame
    from sims.sim_info_types import Species
    info = sim.sim_info
    species = info.species
    if not (species == Species.HUMAN or getattr(species, 'name', '') == 'HUMAN'):
        return False
    if getattr(info.age, 'name', str(info.age)) not in ADULT_AGES:
        return False
    return _frame(info) == 'f'


def _refresh():
    """The jiggle follows at once, not at the next 2 s listing."""
    try:
        from integratedbodyphysics import _tick
        _tick()
    except Exception:
        pass


def _wear(sim, category, index=0):
    from sims.outfits.outfit_enums import OutfitCategory
    info = sim.sim_info
    outfit = (OutfitCategory(category), index)
    if not info.has_outfit(outfit):
        _log('{} has no {} outfit'.format(sim, category))
        return
    before = info.get_current_outfit()
    info.set_current_outfit(outfit)
    _log('{}: outfit {} -> {} {} (now {})'.format(sim, before, category, index, info.get_current_outfit()))
    _refresh()


def _top_clothed(sim):
    from integratedbodyphysics import _clothed
    return (_clothed(sim.sim_info) & 2) != 0


def _toggle_top(sim):
    from integratedbodyphysics import store, top_part
    part = top_part(sim.sim_info)
    if part is None:
        return
    state = 'nude' if _top_clothed(sim) else 'clothed'
    store.set_part(part, state)
    _log('{}: top part {:#x} set as {}'.format(sim, part, state))
    _refresh()


def _name(sim):
    try:
        return '{} {}'.format(sim.sim_info.first_name, sim.sim_info.last_name)
    except Exception:
        return str(sim)


def _set_gravity(sim, value):
    from integratedbodyphysics import store
    store.set_gravity(sim.sim_info.sim_id, value)
    _log('{}: nude gravity {}'.format(_name(sim), 'from the debug menu' if value is None else '{:+.2f}'.format(value)))
    _refresh()


def _set_apart(sim, value):
    from integratedbodyphysics import store
    store.set_apart(sim.sim_info.sim_id, value)
    _log('{}: breasts apart {}'.format(sim, 'from the debug menu' if value is None else '{:+.1f} cm'.format(value * 100)))
    _refresh()


def _auto_size(sim):
    """The size preset her CAS size picks (0..3), or None before the DLL has measured her."""
    from integratedbodyphysics import _mod
    try:
        got = _mod.size_of(sim.id)
        return None if got is None else int(got[0])
    except Exception:
        return None


def _size(sim):
    """The size preset she uses now (0..3)."""
    from integratedbodyphysics import store
    cup = store.cup(sim.sim_info.sim_id)
    if cup is None:
        cup = _auto_size(sim)
    return 1 if cup is None else cup


def _preset(sim, field):
    from integratedbodyphysics import _mod
    try:
        return _mod.get('{}_breast_{}'.format(SIZES[_size(sim)].lower(), field))
    except Exception:
        return None


def _set_cup(sim, value):
    from integratedbodyphysics import store
    store.set_cup(sim.sim_info.sim_id, value)
    _log('{}: cup size {}'.format(sim, 'auto' if value is None else SIZES[value]))
    _refresh()


def _cup_check(value):
    def check(sim):
        from integratedbodyphysics import store
        return ('Her size now', True) if store.cup(sim.sim_info.sim_id) == value else None
    return check


def _auto_label(sim):
    size = _auto_size(sim)
    return 'Auto' if size is None else 'Auto ({})'.format(SIZES[size])


def _current_apart(sim):
    from integratedbodyphysics import store
    a = store.apart(sim.sim_info.sim_id)
    return _preset(sim, 'apart') if a is None else a


def _spacing_check(value):
    def check(sim):
        a = _own_apart(sim)
        return ('Her spacing now', True) if a is not None and abs(a - value) < 0.001 else None
    return check


def _own_gravity(sim):
    from integratedbodyphysics import store
    return store.gravity(sim.sim_info.sim_id)


def _own_apart(sim):
    from integratedbodyphysics import store
    return store.apart(sim.sim_info.sim_id)


def _current_gravity(sim):
    from integratedbodyphysics import store
    g = store.gravity(sim.sim_info.sim_id)
    return _preset(sim, 'gravity') if g is None else g


def _base_class():
    """A plain immediate interaction of the game's to copy (every tuned value exists).
    The lowest id: EA's own, not another mod's (0.8.0 took a Basemental one, whose
    picker opened as well)."""
    import services
    import sims4.resources
    from interactions.base.immediate_interaction import ImmediateSuperInteraction
    manager = services.get_instance_manager(sims4.resources.Types.INTERACTION)
    best = None
    for cls in list(manager.types.values()):
        try:
            if cls.__bases__[0] is not ImmediateSuperInteraction or getattr(cls, 'debug', False) or \
                    getattr(cls, 'cheat', False):
                continue
            if best is None or cls.guid64 < best.guid64:
                best = cls
        except (TypeError, AttributeError, IndexError):
            continue
    return best


def _category(instance):
    import services
    import sims4.resources
    try:
        return services.get_instance_manager(sims4.resources.Types.PIE_MENU_CATEGORY).get(instance)
    except Exception:
        return None


def _make(base, number, label, category, check, run, order=0):
    """check(sim) -> None (offered) or (why, shown greyed); label: text or fn(sim)."""
    from event_testing.results import TestResult
    from sims4.localization import LocalizationHelperTuning
    from sims4.utils import flexmethod
    from singletons import DEFAULT

    @flexmethod
    def test(cls, inst, target=DEFAULT, context=DEFAULT, **kwargs):
        try:
            if target is DEFAULT:
                target = inst.target
            if not _on() or not _eligible(target):
                return TestResult(False, 'Integrated Body Physics: not a jiggling Sim')
            verdict = check(target)
            if verdict is None:
                return TestResult.TRUE
            why, greyed = verdict
            if greyed:
                return TestResult(False, 'Integrated Body Physics: ' + why,
                                  tooltip=lambda *_, **__: LocalizationHelperTuning.get_raw_text(why))
            return TestResult(False, 'Integrated Body Physics: ' + why)
        except Exception as e:
            return TestResult(False, 'Integrated Body Physics test failed: {!r}'.format(e))

    @flexmethod
    def _get_name(cls, inst, target=DEFAULT, context=DEFAULT, **kwargs):
        text = label
        if callable(label):
            if target is DEFAULT and inst is not None:
                target = inst.target
            try:
                text = label(target)
            except Exception:
                text = '?'
        return LocalizationHelperTuning.get_raw_text(text)

    def _run_interaction_gen(self, timeline):
        try:
            run(self.target)
        except Exception as e:
            _log('option {} failed: {!r}'.format(number, e))
        return True
        yield

    attrs = {
        'test': test,
        '_get_name': _get_name,
        '_run_interaction_gen': _run_interaction_gen,
        'allow_autonomous': False,
        'allow_user_directed': True,
        'category': category,
        'category_on_forwarded': None,
        'pie_menu_priority': max(0, 30 - order),  # its place inside its submenu (0 = first)
        'basic_extras': (),  # none of the copied interaction's extras
        'guid64': GUID_BASE + number,
        'vr_from_afar': True,
    }
    return type('integratedbodyphysics_Pie{}'.format(number), (base,), attrs)


def _outfit_check(value, index):
    from sims.outfits.outfit_enums import OutfitCategory

    def check(sim):
        info = sim.sim_info
        if not info.has_outfit((OutfitCategory(value), index)):
            return 'she has no such outfit', False
        category, now = info.get_current_outfit()
        if int(category) == value and now == index:
            return 'Wearing it now', True
        return None
    return check


def _top_check(sim):
    from integratedbodyphysics import top_part
    return None if top_part(sim.sim_info) is not None else ('no top', False)


def _track(romance):
    """The friendship (rel inspector) or romance main relationship track."""
    import services
    import sims4.resources
    if not romance:
        from relationships.global_relationship_tuning import RelationshipGlobalTuning
        return RelationshipGlobalTuning.REL_INSPECTOR_TRACK
    manager = services.get_instance_manager(sims4.resources.Types.STATISTIC)
    return next((t for t in manager.types.values() if t.__name__ == 'LTR_Romance_Main'), None)


def _you():
    import services
    return services.active_sim_info()


def _rel_check(romance, value):
    def check(sim):
        you = _you()
        track = _track(romance)
        if you is None or track is None:
            return 'not available', False
        if you.sim_id == sim.sim_info.sim_id:
            return 'that is you', False
        now = you.relationship_tracker.get_relationship_score(sim.sim_info.sim_id, track=track)
        return ('Now ({:.0f})'.format(now), True) if now is not None and abs(now - value) < 1 else None
    return check


def _set_rel(sim, romance, value):
    you = _you()
    track = _track(romance)
    if you is None or track is None:
        return
    # As the relationship.set_score cheat does (server_commands/relationship_commands.py).
    you.relationship_tracker.set_relationship_score(sim.sim_info.sim_id, value, track)
    _log('{}: your {} set to {} (now {})'.format(_name(sim), 'romance' if romance else 'friendship', value,
                                                   you.relationship_tracker.get_relationship_score(
                                                       sim.sim_info.sim_id, track=track)))


def _gravity_check(value):
    def check(sim):
        g = _own_gravity(sim)
        return ('Her gravity now', True) if g is not None and abs(g - value) < 0.01 else None
    return check


def _build(base):
    config = _category(_instance('integratedbodyphysics_JiggleConfig'))
    gravity = _category(_instance('integratedbodyphysics_Gravity'))
    spacing = _category(_instance('integratedbodyphysics_Spacing'))
    posemenu = _category(_instance('integratedbodyphysics_Pose'))
    cupmenu = _category(_instance('integratedbodyphysics_CupSize'))
    _log('categories: Body Physics {}, Gravity {}, Pose {}, Outfit {}'.format(
        config, gravity, posemenu, _category(_instance('integratedbodyphysics_Outfit'))))
    prefix = '' if config is not None else 'Jiggle: '
    out = []
    for value, name in OUTFITS:
        menu = _category(_instance('integratedbodyphysics_Outfit_' + name.replace(' ', '')))
        for index in range(OUTFITS_PER_CATEGORY):
            text = '{} {}'.format(name, index + 1) if value != 5 or index else 'Nude'
            out.append(_make(base, len(out), (prefix if menu is None else '') + text, menu or config,
                             _outfit_check(value, index), lambda sim, v=value, i=index: _wear(sim, v, i), index))
    out.append(_make(base, len(out), lambda sim: prefix + ('Set as nude' if _top_clothed(sim) else 'Set as clothed'),
                     config, _top_check, _toggle_top, 9))
    from integratedbodyphysics import pose
    pprefix = '' if posemenu is not None else prefix + 'Pose: '
    for order, (name, ids) in enumerate(pose.POSES):
        out.append(_make(base, len(out), pprefix + name, posemenu, _pose_check(ids),
                         lambda sim, i=ids, n=name: pose.hold(sim, i, n), order))
    # Was "Let her go" in the Pose menu: now the freeze toggle in the top menu (user 2026-10-19 00:08Z); the
    # Pose menu only shows while she is frozen.
    out.append(_make(base, len(out),
                     lambda sim: prefix + ('As you were' if pose.holding(sim) else 'Freeze all motor functions'),
                     config, _freeze_check, lambda sim: pose.release(sim) if pose.holding(sim) else pose.freeze(sim), 8))
    gprefix = '' if gravity is not None else prefix + 'Gravity: '
    for order, (name, value) in enumerate(GRAVITY):
        out.append(_make(base, len(out), gprefix + name, gravity, _gravity_check(value),
                         lambda sim, v=value: _set_gravity(sim, v), order))
    cprefix = '' if cupmenu is not None else prefix + 'Cup size: '
    out.append(_make(base, len(out), lambda sim: cprefix + _auto_label(sim), cupmenu, _cup_check(None),
                     lambda sim: _set_cup(sim, None), 0))
    for order, name in enumerate(SIZES):
        out.append(_make(base, len(out), cprefix + name, cupmenu, _cup_check(order),
                         lambda sim, v=order: _set_cup(sim, v), order + 1))
    # "Debug menu": her own preset cleared, so her size preset's Gravity / Apart (debug menu) applies again
    # (user 2026-10-15 05:59Z: the debug Gravity did nothing on Sims with a pie Gravity preset).
    out.append(_make(base, len(out), gprefix + 'Debug menu', gravity,
                     lambda sim: ('Following the debug menu', True) if _own_gravity(sim) is None else None,
                     lambda sim: _set_gravity(sim, None), 9))
    sprefix = '' if spacing is not None else prefix + 'Spacing: '
    for order, (name, value) in enumerate(SPACING):
        out.append(_make(base, len(out), sprefix + name, spacing, _spacing_check(value),
                         lambda sim, v=value: _set_apart(sim, v), order))
    out.append(_make(base, len(out), sprefix + 'Debug menu', spacing,
                     lambda sim: ('Following the debug menu', True) if _own_apart(sim) is None else None,
                     lambda sim: _set_apart(sim, None), 9))
    # Appended last: the options before keep their numbers (guid64).
    for romance, levels, key in ((False, FRIENDSHIP, 'Friendship'), (True, ROMANCE, 'Romance')):
        menu = _category(_instance('integratedbodyphysics_' + key))
        rprefix = '' if menu is not None else prefix + key + ': '
        for order, (name, value) in enumerate(levels):
            out.append(_make(base, len(out), '{}{} ({})'.format(rprefix, name, value), menu or config,
                             _rel_check(romance, value), lambda sim, r=romance, v=value: _set_rel(sim, r, v), order))
    return out


def _freeze_check(sim):
    from integratedbodyphysics import pose
    if pose.holding(sim):
        return None
    return None if pose.affordance(pose.NEUTRAL) is not None else ('not in this game', False)


def _pose_check(ids):
    def check(sim):
        from integratedbodyphysics import pose
        if not pose.holding(sim):
            return 'not frozen', False
        aff = pose.affordance(ids)
        if aff is None:
            return 'not in this game', False
        return ('Holding it now', True) if pose.held_affordance(sim) is aff else None
    return check


def _wrap():
    global _wrapped
    if _wrapped:
        return
    from sims.sim import Sim
    from sims4.utils import flexmethod
    raw = next((k.__dict__['super_affordances'] for k in Sim.__mro__ if 'super_affordances' in k.__dict__), None)
    if raw is None:
        _log('Sim.super_affordances not found; no Body Physics')
        return

    # Keep it a flexmethod (cls, inst, context=None), or every pie breaks.
    @flexmethod
    def super_affordances(cls, inst, *args, **kwargs):
        yield from raw.__get__(inst, cls)(*args, **kwargs)
        try:
            if inst is not None and _affordances and _on() and _eligible(inst):
                yield from _affordances
        except Exception:
            pass

    Sim.super_affordances = super_affordances
    _wrapped = True


def start():
    try:
        if not _affordances:
            base = _base_class()
            if base is None:
                _log('no immediate interaction to build on; no Body Physics')
                return
            _affordances.extend(_build(base))
            _log('{} options built on {} ({:#x})'.format(len(_affordances), base.__name__, base.guid64))
        _wrap()
    except Exception as e:
        _log('no Body Physics: {!r}'.format(e))
