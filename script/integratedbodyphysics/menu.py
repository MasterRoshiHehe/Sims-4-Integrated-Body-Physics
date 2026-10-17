"""Integrated Body Physics: the mod's own settings menu in game dialogs, for players without the Sims 4 VR
mod (whose debug menu shows the same pages). Opens with the menu key ('.' unless changed in the menu) or the
console command "ibp menu". The pages come from the DLL (docs/MENU-EXPORT.md)."""
POLL_SECONDS = 0.1

_owner = type('_Owner', (), {})()
_alarm = None
_open = False
_pages = None  # page id -> (parent id, title)
_rows = None  # page id -> [row fields]


def _log(text):
    from integratedbodyphysics import _log as log
    log('menu: ' + text)


def _mod():
    from integratedbodyphysics import _mod as mod
    return mod


def _load():
    global _pages, _rows
    if _pages is not None:
        return
    pages, rows = {}, {}
    for line in _mod().menu().split('\n'):
        f = line.split('\t')
        if f[0] == 'page' and len(f) >= 4:
            pages[f[1]] = (f[2], f[3])
            rows.setdefault(f[1], [])
        elif f[0] in ('link', 'switch', 'number', 'confirm', 'key') and len(f) >= 4:
            rows.setdefault(f[1], []).append(f)
    _pages, _rows = pages, rows


def _raw(text):
    from sims4.localization import LocalizationHelperTuning
    string = LocalizationHelperTuning.get_raw_text(text)
    return lambda *_, **__: string


def _flags(f):
    return set(x for x in f.split(',') if x)


def _shown(f, vr):
    """Whether the row shows: 'vr' rows only with the VR mod, 'novr' rows only without it."""
    at = {'switch': 4, 'number': 9, 'confirm': 4, 'key': 4}.get(f[0])
    flags = _flags(f[at] if at is not None and len(f) > at else '')
    return not (('vr' in flags and not vr) or ('novr' in flags and vr))


def _value(key):
    v = _mod().get(key)
    return 0.0 if v is None else v


def _number_text(f, v):
    try:
        return f[8] % (v * float(f[7]))
    except (TypeError, ValueError):
        return '{:g}'.format(v)


def _label(f):
    kind = f[0]
    if kind == 'link':
        return f[2] + '  >'
    if kind == 'switch':
        return '{}: {}'.format(f[2], 'On' if _value(f[3]) != 0 else 'Off')
    if kind == 'number':
        return '{}: {}'.format(f[2], _number_text(f, _value(f[3])))
    if kind == 'key':
        return '{}: {}'.format(f[2], _mod().key_name(int(_value(f[3]))))
    return f[2] + '...'


def _tip(f):
    return {'switch': 5, 'number': 10, 'confirm': 5, 'key': 5}.get(f[0])


def _who():
    import services
    return services.active_sim_info()


def _closed():
    global _open
    _open = False


def show(page='main'):
    """Opens a page of the menu."""
    global _open
    _load()
    owner = _who()
    if owner is None or page not in _pages:
        _closed()
        return
    _open = True
    from sims4.localization import LocalizationHelperTuning
    from ui.ui_dialog_picker import ObjectPickerRow, UiObjectPicker
    vr = bool(_mod().vr())
    parent, title = _pages[page]
    rows = [f for f in _rows.get(page, []) if _shown(f, vr)]
    dialog = UiObjectPicker.TunableFactory().default(
        owner, text=_raw('Integrated Body Physics settings. Pick one to change it.'), title=_raw(title),
        min_selectable=1, max_selectable=1, picker_type=UiObjectPicker.UiObjectPickerObjectPickerType.OBJECT)
    for i, f in enumerate(rows):
        tip = _tip(f)
        description = f[tip] if tip is not None and len(f) > tip else ''
        text = LocalizationHelperTuning.get_raw_text(description) if description else None
        dialog.add_row(ObjectPickerRow(
            option_id=i, name=LocalizationHelperTuning.get_raw_text(_label(f)), row_description=text,
            row_tooltip=(lambda text=text: text) if text else None, tag=tuple(f)))

    def on_response(d):
        from ui.ui_dialog import ButtonType
        try:
            picked = d.get_result_tags() if d.response == ButtonType.DIALOG_RESPONSE_OK else []
            if picked:
                _pick(page, picked[0])
            elif d.response == ButtonType.DIALOG_RESPONSE_CANCEL and parent:
                show(parent)
            else:
                _closed()
        except Exception as e:
            _closed()
            _log('page {} failed: {!r}'.format(page, e))

    dialog.show_dialog(on_response=on_response)


def _pick(page, f):
    kind = f[0]
    if kind == 'link':
        show(f[3])
    elif kind == 'switch':
        _mod().set(f[3], 0.0 if _value(f[3]) != 0 else 1.0)
        show(page)
    elif kind == 'number':
        _ask_number(page, f)
    elif kind == 'confirm':
        _ask_sure(page, f)
    elif kind == 'key':
        _ask_key(page, f)
    else:
        show(page)


def _ask_number(page, f):
    from ui.ui_dialog import ButtonType
    from ui.ui_dialog_generic import UiDialogTextInputOkCancel
    low, high, scale = float(f[4]), float(f[5]), float(f[7])
    v = _value(f[3])
    dialog = UiDialogTextInputOkCancel.TunableFactory(text_inputs=('value',)).default(
        _who(), title=_raw(f[2]),
        text=_raw('{}\n\nNow {}. From {} to {}.'.format(f[10], _number_text(f, v), _number_text(f, low),
                                                          _number_text(f, high))))

    def on_response(d):
        try:
            if d.response == ButtonType.DIALOG_RESPONSE_OK:
                typed = d.text_input_responses.get('value', '')
                number = ''.join(c for c in typed.replace(',', '.') if c in '+-.0123456789')
                try:
                    x = float(number) / scale
                except (ValueError, ZeroDivisionError):
                    x = None
                if x is not None:
                    _mod().set(f[3], min(high, max(low, x)))
            show(page)
        except Exception as e:
            _closed()
            _log('number {} failed: {!r}'.format(f[3], e))

    dialog.show_dialog(on_response=on_response, text_input_overrides={'value': _raw('{:g}'.format(round(v * scale, 4)))})


def _ask_sure(page, f):
    from ui.ui_dialog import ButtonType, UiDialogOkCancel
    dialog = UiDialogOkCancel.TunableFactory().default(_who(), title=_raw(f[2]), text=_raw(f[5] + '\n\nAre you sure?'))

    def on_response(d):
        try:
            if d.response == ButtonType.DIALOG_RESPONSE_OK:
                _mod().set(f[3], 1.0)
            show(page)
        except Exception as e:
            _closed()
            _log('confirm {} failed: {!r}'.format(f[3], e))

    dialog.show_dialog(on_response=on_response)


def _ask_key(page, f):
    from ui.ui_dialog import ButtonType, UiDialogOkCancel
    _mod().capture(1)
    dialog = UiDialogOkCancel.TunableFactory().default(
        _who(), title=_raw(f[2]),
        text=_raw('Press the key you want for this menu, then click OK.\n\nNow: {}'.format(
            _mod().key_name(int(_value(f[3]))))))

    def on_response(d):
        try:
            vk = _mod().capture(0)
            if d.response == ButtonType.DIALOG_RESPONSE_OK and vk:
                _mod().set(f[3], float(vk))
                _log('menu key now {}'.format(_mod().key_name(vk)))
            show(page)
        except Exception as e:
            _closed()
            _log('key failed: {!r}'.format(e))

    dialog.show_dialog(on_response=on_response)


def _poll(_handle=None):
    try:
        if _mod().menu_key() and not _open:
            show()
    except Exception as e:
        _closed()
        _log('key poll failed: {!r}'.format(e))


def start():
    global _alarm
    if _alarm is not None:
        return
    import alarms
    import clock
    _alarm = alarms.add_alarm_real_time(_owner, clock.interval_in_real_seconds(POLL_SECONDS), _poll, repeating=True)


def stop():
    global _alarm
    _closed()
    alarm, _alarm = _alarm, None
    if alarm is not None:
        import alarms
        alarms.cancel_alarm(alarm)
