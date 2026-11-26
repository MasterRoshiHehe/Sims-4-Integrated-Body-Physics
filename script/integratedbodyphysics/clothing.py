"""Clothing affects jiggle (user 2026-10-28): what she wears over her breasts and
butt scales the clothed jiggle, from the game's own CAS tags (garment kind and
fabric; re/client/cas/GARMENTS.md). A bikini moves more than a tee, a tee more
than a dress, a dress more than a winter coat. The factors are settings
(cloth_*), times the size presets' "Clothed: strength".
"""

# Garment kind tags (category Top 81, Bottom 82, FullBody 83): tag id -> setting key.
TOP_KINDS = (
    ((1236,), 'cloth_swim'),                        # Top_Bikini
    ((1237,), 'cloth_swim'),                        # FullBody_Swimsuit
    ((944, 950), 'cloth_underwear'),                # Top_Brassiere, FullBody_Lingerie
    ((947, 949, 952), 'cloth_outer'),               # FullBody_Outerwear, Robe, Overall
    ((297, 941, 295, 942, 156, 377), 'cloth_heavy'),  # Sweater, Sweatshirt, Jacket, SuitJacket, Vest, Suits
    ((376, 375, 374), 'cloth_dress'),               # Shortdress, Longdress, Jumpsuits
    ((360, 296, 155, 943, 395), 'cloth_light'),     # Tanktop, ShirtTee, Blouse, Polo, ButtonUps
    ((948, 951), 'cloth_other'),                    # Costume, Apron
)
BOTTOM_KINDS = (
    ((1544, 1238, 1237), 'cloth_swim_bottom'),      # Bottom_Swimwear, Swimshort, FullBody_Swimsuit
    ((1543, 950), 'cloth_underwear_bottom'),        # Bottom_Underwear, FullBody_Lingerie
    ((153, 154, 381, 376), 'cloth_skirt'),          # Skirt, Shorts, Leggings, Shortdress
    ((152, 382, 945, 947, 952, 377, 374, 375), 'cloth_pants'),  # Pants, Jeans, Cropped, Outerwear, Overall, Suits, Jumpsuits, Longdress
)
# Fabric tags (category 96): how freely it lets them move.
FABRICS = {585: 1.1, 584: 1.1, 532: 1.0, 586: 0.9, 587: 0.8, 531: 0.7, 932: 0.4}  # silk, synthetic, cotton, wool, denim, leather, metal
UPPER_BODY, LOWER_BODY, FULL_BODY = 6, 7, 5
# Part-name classes (integratedbodyphysics_garments.tsv: CAS part instance hex -> class, vanilla + the user's CC,
# from the parts' names; only parts whose name says something the tags don't) -> setting key. Over the tags.
NAME_CLASSES = {'SWIM_BRA': 'cloth_swim', 'UNDER_BRA': 'cloth_underwear', 'TUBE_CROP': 'cloth_tube',
                'TANK_TEE': 'cloth_light', 'OUTERWEAR': 'cloth_heavy'}
NAMES_FILE = 'integratedbodyphysics_garments.tsv'
_names = None

_probed = set()


def _get(mod, key, fallback):
    try:
        x = mod.get(key)
        return fallback if x is None else float(x)
    except Exception:
        return fallback


def _name_classes(log):
    global _names
    if _names is None:
        _names = {}
        try:
            import os
            from integratedbodyphysics import _find_dll
            path = os.path.join(os.path.dirname(_find_dll()), NAMES_FILE)
            with open(path, 'r') as f:
                for line in f:
                    parts = line.split()
                    if len(parts) >= 2 and parts[1] in NAME_CLASSES:
                        _names[int(parts[0], 16)] = NAME_CLASSES[parts[1]]
            log('clothing: {} garment names from {}'.format(len(_names), NAMES_FILE))
        except Exception as e:
            log('clothing: no garment names ({!r}): tags only'.format(e))
    return _names


def _factor(mod, tags, kinds, named=None):
    if named is not None:
        base, key = _get(mod, named, 1.0), named
    else:
        for ids, key in kinds:
            if any(t in tags for t in ids):
                base = _get(mod, key, 1.0)
                break
        else:
            return None, None
    fabric = 1.0
    for t, f in FABRICS.items():
        if t in tags:
            fabric = f
            break
    blend = _get(mod, 'cloth_fabric', 1.0)
    return base * (1.0 + (fabric - 1.0) * blend), key


def factors(mod, sim_info, clothed, log):
    """(breasts, butt) multipliers for her clothed jiggle; 1.0 when nude or off."""
    if not clothed or _get(mod, 'cloth_on', 1.0) == 0:
        return 1.0, 1.0
    try:
        import cas.cas
        category, index = sim_info.get_current_outfit()
        worn = cas.cas.get_tags_from_outfit(sim_info._base, category, index) or {}
        outfit = sim_info.get_outfit(category, index)
        slots = dict(zip(outfit.body_types, outfit.part_ids))
    except Exception as e:
        if 'error' not in _probed:
            _probed.add('error')
            log('clothing: tags not readable: {!r}'.format(e))
        return 1.0, 1.0
    def tags_of(part):
        return set(int(t) for t in worn.get(part, ())) if part is not None else set()
    top = slots.get(UPPER_BODY, slots.get(FULL_BODY))
    bottom = slots.get(LOWER_BODY, slots.get(FULL_BODY))
    scale = _get(mod, 'cloth_scale', 1.0)
    named = _name_classes(log).get(top) if top is not None and _get(mod, 'cloth_names', 1.0) != 0 else None
    breasts, bkey = _factor(mod, tags_of(top), TOP_KINDS, named) if clothed & 2 else (None, None)
    butt, ukey = _factor(mod, tags_of(bottom), BOTTOM_KINDS) if clothed & 4 else (None, None)
    if breasts is None:
        breasts = 1.0 if not clothed & 2 else _get(mod, 'cloth_unknown', 1.0)
    if butt is None:
        butt = 1.0 if not clothed & 4 else _get(mod, 'cloth_unknown', 1.0)
    breasts = min(2.0, max(0.0, 1.0 + (breasts - 1.0) * scale))
    butt = min(2.0, max(0.0, 1.0 + (butt - 1.0) * scale))
    key = (sim_info.sim_id, top, bottom)
    if key not in _probed and len(_probed) < 400:  # probe: each Sim's worn parts once per outfit
        _probed.add(key)
        log('clothing: Sim {} top {} tags {} -> {} ({}) x{:.2f}; bottom {} tags {} -> {} x{:.2f}'.format(
            sim_info.sim_id, top, sorted(tags_of(top)), bkey, 'name' if named else 'tags', breasts, bottom, sorted(tags_of(bottom)), ukey, butt))
    return breasts, butt
