"""Body Physics > Pose (user 2026-10-15 02:23Z: test outfits and physics
without chasing Sims around). A pose stops what she is doing and holds her in
it: every listing tick (2 s) she gets it pushed again if she stopped (dances
end on their own after ~20 game minutes; autonomy would take her away).
"Let her go" ends the hold.
While held she is locked (user 02:42Z: new interactions queued up and walked
her away): her interaction queue refuses everything but the pose and its own
mixers (autonomy, other Sims' socials, situations, your picks).

- Neutral: the game's exclusive stand (sim-standExclusive): stands still.
- Dance: the game's solo dance (base-game dance moves; the first one found of
  a few packs' solo dance interactions, no stereo needed). Lots of arm moves
  across the chest, for the arm collision.
- Stretch: the game's Stretch (base game): bends and arm stretches.
No jumping jacks in the game outside the TV workout (which needs a TV).

0.11.7 (user 2026-10-16 12:47Z: the pose didn't stop the NPC dead, she
picked up a new schedule and left). Checked against the game's 1.121 Python:
- Stopping: what walks her is the queue's transition (a route), which
  cancelling her actions doesn't end, and SuperInteraction._cancel_eventually
  holds a cancel back while a must-run step plays unless immediate. Now the
  transition and every action are cancelled (ignore_must_run, immediate), and
  if she is still walked a moment later she gets the game's own reset
  (Sim.reset RESET_EXPECTED, as the resetsim cheat): she stands where she is.
  Not when she is in a social group with your Sim (a reset resets the group).
- Leaving: a schedule's end sends her off through SituationManager
  make_sim_leave / make_sim_leave_now_must_run (a leave situation whose role
  pushes the go-home action). While she is held those are put off; when you
  let her go she leaves the way the game asked.
"""
import time

NEUTRAL = (23835,)  # sim-standExclusive
DANCE = (218814, 115844, 293115)  # si_BehaviorBomb_Reactions_SoloDance (EP08), si_solo_SatelliteDishHiveMind_Dance (EP01), sI_HighSchoolFestival_SoloDance (EP12)
STRETCH = (96787,)  # sim_doStretching

POSES = (('Neutral', NEUTRAL), ('Dance', DANCE), ('Stretch', STRETCH))

_held = {}  # sim id -> affordance
_current = 0  # the frozen Sim's id, 0 = none
_since = {}  # sim id -> when the hold started (time.monotonic)
_reset_done = set()  # sim ids reset once for this hold
_leave_later = {}  # sim id -> 'leave' or 'now': the game asked her to leave while held
_wrapped = False
_leave_wrapped = False
RESET_AFTER = 1.0  # s: still walked this long after the hold started: the game's reset
_logs = 0


def _log(text):
    global _logs
    _logs += 1
    if _logs <= 60:
        from integratedbodyphysics import _log as log
        log('pose: ' + text)


def affordance(ids):
    import services
    import sims4.resources
    manager = services.get_instance_manager(sims4.resources.Types.INTERACTION)
    for i in ids:
        a = manager.get(i)
        if a is not None:
            return a
    return None


def _running(sim, aff):
    try:
        for si in sim.get_all_running_and_queued_interactions():
            if si.affordance is aff and not getattr(si, 'is_finishing', False):
                return True
    except Exception:
        pass
    return False


def _push(sim, aff, stop_others):
    from interactions.context import InteractionContext, QueueInsertStrategy
    from interactions.interaction_finisher import FinishingType
    from interactions.priority import Priority
    if stop_others:
        for si in list(sim.queue or ()):
            try:
                si.cancel(FinishingType.USER_CANCEL, cancel_reason_msg='Integrated Body Physics pose')
            except Exception:
                pass
    context = InteractionContext(sim, InteractionContext.SOURCE_SCRIPT_WITH_USER_INTENT, Priority.Critical,
                                 insert_strategy=QueueInsertStrategy.NEXT)
    return sim.push_super_affordance(aff, sim, context)


def _stop_running(sim, aff):
    """Stops her where she is: the walk (the queue's transition) and every action,
    without waiting for must-run steps (her posture plays its way out)."""
    from interactions.interaction_finisher import FinishingType
    why = 'Integrated Body Physics pose'
    try:
        controller = getattr(sim.queue, 'transition_controller', None)
        owner = getattr(controller, 'interaction', None)
        if controller is not None:
            controller.cancel(FinishingType.USER_CANCEL, why, si_to_cancel=owner)
        items = list(sim.si_state) + list(sim.queue)
        if owner is not None:
            items.append(owner)
        for si in items:
            if si.affordance is aff or getattr(si, 'is_finishing', False):
                continue
            if getattr(si.affordance, '_provided_posture_type', None) is not None:
                continue  # her posture (standing, sitting): the pose's own transition changes it
            si.cancel(FinishingType.USER_CANCEL, cancel_reason_msg=why, ignore_must_run=True, immediate=True)
    except Exception as e:
        _log('{}: stopping what she does failed: {!r}'.format(sim, e))


def _walked(sim):
    try:
        component = sim.routing_component
        return component.current_path is not None or component.is_moving
    except Exception:
        return False


def _with_you(sim):
    """In a social group with the Sim you control (a reset would reset you too)."""
    try:
        import services
        you = services.get_active_sim()
        if you is None or you is sim:
            return you is sim
        for group in getattr(sim, 'get_groups_for_sim_gen', lambda: ())():
            if you in group:
                return True
    except Exception:
        pass
    return False


def _reset(sim, why):
    from objects.object_enums import ResetReason
    try:
        sim.reset(ResetReason.RESET_EXPECTED, None, why)
        _log('{}: reset ({})'.format(sim, why))
        return True
    except Exception as e:
        _log('{}: reset failed: {!r}'.format(sim, e))
        return False


def hold(sim, ids, name):
    aff = affordance(ids)
    if aff is None:
        _log('{}: no {} interaction in this game'.format(sim, name))
        return
    wrap_queue()
    wrap_leaving()
    sim_id = sim.sim_info.sim_id
    _held[sim_id] = aff
    _since[sim_id] = time.monotonic()
    _reset_done.discard(sim_id)
    _stop_running(sim, aff)
    result = _push(sim, aff, True)
    _log('{}: {} ({}) -> {}'.format(sim, name, aff.__name__, result))


def holding(sim):
    return sim.sim_info.sim_id in _held


def held_affordance(sim):
    return _held.get(sim.sim_info.sim_id)


def _set_current(sim_id):
    """The frozen Sim is the debug menu's current Sim ("Configure current Sim")."""
    global _current
    _current = sim_id
    try:
        from integratedbodyphysics import _mod
        _mod.set_current(sim_id)
    except Exception as e:
        _log('set current failed: {!r}'.format(e))


def freeze(sim):
    """"Freeze all motor functions": holds her in the neutral pose and makes her the current Sim. One Sim at a
    time: a Sim frozen before is let go."""
    import services
    manager = services.sim_info_manager()
    for other_id in list(_held):
        info = manager.get(other_id) if manager is not None else None
        other = info.get_sim_instance() if info is not None else None
        if other is not None and other is not sim:
            release(other)
        elif other is None:
            _held.pop(other_id, None)
    hold(sim, NEUTRAL, 'Neutral')
    if holding(sim):
        _set_current(sim.sim_info.sim_id)


def release(sim):
    sim_id = sim.sim_info.sim_id
    if _current == sim_id:
        _set_current(0)
    _held.pop(sim_id, None)
    _since.pop(sim_id, None)
    _reset_done.discard(sim_id)
    _log('{}: let go'.format(sim))
    _leave_now_if_asked(sim)


def _leave_now_if_asked(sim):
    """The game asked her to leave while she was held: she goes now."""
    how = _leave_later.pop(sim.sim_info.sim_id, None)
    if how is None:
        return
    try:
        import services
        manager = services.get_zone_situation_manager()
        if how == 'now':
            manager.make_sim_leave_now_must_run(sim)
        else:
            manager.make_sim_leave(sim)
        _log('{}: leaves now (asked while held)'.format(sim))
    except Exception as e:
        _log('{}: leaving failed: {!r}'.format(sim, e))


def tick():
    """Pushes a held pose again when she stopped it."""
    if not _held:
        return
    import services
    manager = services.sim_info_manager()
    for sim_id, aff in list(_held.items()):
        info = manager.get(sim_id) if manager is not None else None
        sim = info.get_sim_instance() if info is not None else None
        if sim is None:
            if _current == sim_id:
                _set_current(0)
            _held.pop(sim_id, None)
            _since.pop(sim_id, None)
            _leave_later.pop(sim_id, None)
            continue
        started = _since.get(sim_id, 0)
        if (sim_id not in _reset_done and not _running(sim, aff) and _walked(sim)
                and time.monotonic() - started >= RESET_AFTER and not _with_you(sim)):
            _reset_done.add(sim_id)  # once per hold: the game's reset stops her where she is
            _reset(sim, 'Integrated Body Physics pose: still walking')
        if not _running(sim, aff):
            try:
                _push(sim, aff, False)
            except Exception as e:
                _log('{}: push again failed: {!r}'.format(sim, e))


def _allowed(interaction, aff):
    if interaction.affordance is aff:
        return True
    if not getattr(interaction, 'is_super', True):  # a mixer: only the pose's own
        si = getattr(interaction, 'super_interaction', None)
        return si is not None and si.affordance is aff
    return False


def wrap_leaving():
    """While held, the game's requests to send her off are put off until you let
    her go (SituationManager.make_sim_leave / make_sim_leave_now_must_run: a
    schedule's end, a situation that is over)."""
    global _leave_wrapped
    if _leave_wrapped:
        return
    from situations.situation_manager import SituationManager

    def wrap(name, how):
        original = getattr(SituationManager, name)

        def leave(self, sim, *args, **kwargs):
            try:
                if _held and sim is not None and sim.sim_info.sim_id in _held:
                    if _leave_later.get(sim.sim_info.sim_id) != 'now':
                        _leave_later[sim.sim_info.sim_id] = how
                    _log('{}: {} put off (held in a pose)'.format(sim, name))
                    return None
            except Exception as e:
                _log('leave check failed: {!r}'.format(e))
            return original(self, sim, *args, **kwargs)

        setattr(SituationManager, name, leave)

    wrap('make_sim_leave', 'leave')
    wrap('make_sim_leave_now_must_run', 'now')
    _leave_wrapped = True


def wrap_queue():
    """Held Sims' queues refuse everything but the pose (InteractionQueue.append
    is the game's only way into a queue)."""
    global _wrapped
    if _wrapped:
        return
    from event_testing.results import TestResult
    from interactions.interaction_finisher import FinishingType
    from interactions.interaction_queue import InteractionQueue
    original = InteractionQueue.append
    refused = {'n': 0}

    def append(self, interaction, *args, **kwargs):
        try:
            sim = getattr(interaction, 'sim', None)
            aff = _held.get(sim.sim_info.sim_id) if _held and sim is not None else None
            if aff is not None and not _allowed(interaction, aff):
                refused['n'] += 1
                if refused['n'] <= 30:
                    _log('{}: refused {} (held in a pose)'.format(sim, interaction))
                try:
                    interaction.cancel(FinishingType.INTERACTION_QUEUE, cancel_reason_msg='Integrated Body Physics: held in a pose')
                except Exception:
                    pass
                return TestResult(False, 'Integrated Body Physics: held in a pose')
        except Exception as e:
            _log('queue check failed: {!r}'.format(e))
        return original(self, interaction, *args, **kwargs)

    InteractionQueue.append = append
    _wrapped = True
