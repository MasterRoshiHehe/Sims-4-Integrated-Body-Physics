// The settings menu, for the Sims 4 VR debug menu and this mod's own menu (ibp_menu(), docs/MENU-EXPORT.md).
// One row per line, fields separated by TABs: keep the TABs when editing.
LR"IBP(page	main		Integrated Body Physics
switch	main	Physics on	on		Turns all body physics on or off, for every Sim.
switch	main	Your own Sim	own		Your own Sim (the one you play) jiggles. Turn it off if it's too busy to look down at in first person.
switch	main	Other Sims	others		Every other young adult, adult and elder woman around you jiggles. Men never do.
switch	main	Body Physics on pie menus	outfit_pie		Women's pie menus get a Body Physics submenu: change her outfit, set the top she wears as nude or clothed, her Cup size, Gravity, Spacing and your relationship. Works from any distance.
link	main	Breasts	breasts
link	main	Butt	butt
link	main	Pushing and holding	hands
key	main	Menu key	menu_key	novr	The key that opens this menu when the VR mod isn't loaded.
page	breasts	main	Breasts
switch	breasts	Breasts jiggle	breasts_on		Turns breast jiggle on or off without changing the settings below.
number	breasts	Only within	breast_distance	0	50	1	1	%.0f m		Only Sims this close to your Sim jiggle; farther ones are left alone, which saves processing. 0 m = any distance.
switch	breasts	Swing on	swing_on		They tilt as they move (each size's Swing). Off = they only shift, as before 0.11.
switch	breasts	Squish on	squish_on		They flatten where an arm or hand presses (each size's Squish). Off = never.
switch	breasts	Weight on	weight_on		Each size's Weight: heavier ones lag behind sharp moves. Off = all the same, as before 0.11.
number	breasts	Holding a breast: finger bend	breast_grab_curl	0	1	0.05	100	%.0f%%	vr	How far your fingers close holding a breast (0% open, 100% a fist). Without Integrated Body Physics, Grab: finger bend applies.
link	breasts	Advanced	breasts_adv
page	breasts_adv	breasts	Breasts: advanced
switch	breasts_adv	Wall between them	middle_wall		An invisible wall down her middle: pushed together (by hands or Apart below 0), they press into it and squish instead of sliding into each other.
link	breasts_adv	Cup sizes from CAS	cups
page	cups	breasts_adv	Cup sizes from CAS
number	cups	Cup size from CAS: Medium at	cup_medium_at	0.5	4	0.01	1	%.2f		Sims whose CAS breast bone is at least this scale use Medium (smaller: Small), unless their pie Cup size says otherwise. Each Sim's scale is in integratedbodyphysics.log (Size:).
number	cups	Cup size from CAS: Bigger at	cup_bigger_at	0.5	4	0.01	1	%.2f		Sims whose CAS breast bone is at least this scale use Bigger.
number	cups	Cup size from CAS: Huge at	cup_huge_at	0.5	4	0.01	1	%.2f		Sims whose CAS breast bone is at least this scale use Huge.
link	breasts_adv	Small	small
link	breasts_adv	Medium	medium
link	breasts_adv	Bigger	bigger
link	breasts_adv	Huge	huge
page	small	breasts_adv	Small
number	small	Strength	small_breast_strength	0	8	0.1	1	%.1f		How much of the physics movement shows. 0 = no jiggle, 1 = what the physics does, higher exaggerates every bounce.
number	small	Bounce speed	small_breast_hz	0.5	12	0.25	1	%.2f Hz		How many times a second they swing back and forth. Low = slow, heavy, floppy sway. High = quick, tight, firm wobble.
number	small	Damping	small_breast_damping	0.05	1	0.05	1	%.2f		How fast the bouncing dies down. Low = keeps bouncing a long time after a move. 1 = settles at once without swinging back.
number	small	Swing	small_breast_swing	0	2	0.05	100	%.0f%%		How much they tilt as they move, not just shift: a breast that drops points down. 0% = they only shift.
number	small	Weight	small_breast_weight	0.1	3	0.05	1	%.2f kg		How heavy each one is. Heavier = falls behind sharp moves and swings back slower, less lively. Light = quick and springy.
number	small	Gravity	small_breast_gravity	0	1	0.05	100	%.0f%%		Lets gravity pull them down, so they sag and swing with more weight. For Sims without a pie Gravity preset. 0% = the pose alone decides how they hang.
link	small	How far they move	small_moves
link	small	Shape and touch	small_shape
link	small	Clothed	small_clothed
confirm	small	Scale the others from Small	scale_from_small		Sets Medium, Bigger and Huge from your Small settings, each scaled the way the sizes differ (smaller ones stiffer and lighter, bigger ones softer and heavier). This overwrites your Medium, Bigger and Huge settings.
page	small_moves	small	How far they move
number	small_moves	Most they move up	small_breast_max_up	0.005	0.25	0.005	100	%.1f cm		The furthest they can bounce up from where the pose puts them, before Strength.
number	small_moves	Most they move down	small_breast_max_down	0.005	0.25	0.005	100	%.1f cm		The furthest they can drop below where the pose puts them.
number	small_moves	Most they move sideways	small_breast_max_side	0.005	0.25	0.005	100	%.1f cm		The furthest they can swing to either side.
number	small_moves	Most they move front and back	small_breast_max_front	0.005	0.25	0.005	100	%.1f cm		The furthest they can swing forwards or back.
number	small_moves	Up and down	small_breast_up	0	3	0.1	1	%.1f×		How much of the movement shows up and down: the bounce from steps, jumps and sitting down.
number	small_moves	Side to side	small_breast_side	0	3	0.1	1	%.1f×		How much shows left and right: the sway when the Sim turns or shifts weight.
number	small_moves	Front and back	small_breast_front	0	3	0.1	1	%.1f×		How much shows forwards and backwards: the swing when the Sim starts, stops or leans.
page	small_shape	small	Shape and touch
number	small_shape	Size	small_breast_scale	0.5	2	0.05	100	%.0f%%		How big they look, times her own CAS size. Their sizes for pushing grow with it.
number	small_shape	Squish	small_breast_squish	0	2	0.05	100	%.0f%%		How much they flatten where an arm or hand presses, and bulge out to the sides. 0% = never.
number	small_shape	Apart	small_breast_apart	-0.05	0.05	0.0025	100	%+.2f cm		How much wider the gap between them is, for Sims without a pie Spacing preset. Below 0 = pushed together.
number	small_shape	Size for your hands	small_breast_size_you	0.02	0.12	0.005	100	%.1f cm	vr	How big they count as when your VR hands touch them (radius). Bigger = touched sooner. Doesn't change how they look.
number	small_shape	Size for her own arms	small_breast_size	0.02	0.12	0.005	100	%.1f cm		How big they count as when her own arms and hands push them (radius).
page	small_clothed	small	Clothed
number	small_clothed	Clothed: strength	small_breast_clothed_strength	0	2	0.05	100	%.0f%%		How much of the jiggle is left while a top covers them, compared with nude.
number	small_clothed	Clothed: bounce speed	small_breast_clothed_hz	0.5	3	0.05	1	%.2f×		Bounce speed while clothed, times the nude one. Above 1 = tighter, firmer wobble.
number	small_clothed	Clothed: damping	small_breast_clothed_damping	0.5	3	0.05	1	%.2f×		Damping while clothed, times the nude one. Above 1 = the bouncing dies down sooner.
number	small_clothed	Clothed: gravity	small_breast_clothed_gravity	-1	1	0.05	100	%+.0f%%		Added to her gravity while a top covers them. Below 0 = the top lifts them, like a push-up bra.
number	small_clothed	Clothed: apart	small_breast_clothed_apart	-0.05	0.05	0.0025	100	%+.2f cm		Added to her spacing while a top covers them. Below 0 = pushed together.
page	medium	breasts_adv	Medium
number	medium	Strength	medium_breast_strength	0	8	0.1	1	%.1f		How much of the physics movement shows. 0 = no jiggle, 1 = what the physics does, higher exaggerates every bounce.
number	medium	Bounce speed	medium_breast_hz	0.5	12	0.25	1	%.2f Hz		How many times a second they swing back and forth. Low = slow, heavy, floppy sway. High = quick, tight, firm wobble.
number	medium	Damping	medium_breast_damping	0.05	1	0.05	1	%.2f		How fast the bouncing dies down. Low = keeps bouncing a long time after a move. 1 = settles at once without swinging back.
number	medium	Swing	medium_breast_swing	0	2	0.05	100	%.0f%%		How much they tilt as they move, not just shift: a breast that drops points down. 0% = they only shift.
number	medium	Weight	medium_breast_weight	0.1	3	0.05	1	%.2f kg		How heavy each one is. Heavier = falls behind sharp moves and swings back slower, less lively. Light = quick and springy.
number	medium	Gravity	medium_breast_gravity	0	1	0.05	100	%.0f%%		Lets gravity pull them down, so they sag and swing with more weight. For Sims without a pie Gravity preset. 0% = the pose alone decides how they hang.
link	medium	How far they move	medium_moves
link	medium	Shape and touch	medium_shape
link	medium	Clothed	medium_clothed
confirm	medium	Scale the others from Medium	scale_from_medium		Sets Small, Bigger and Huge from your Medium settings, each scaled the way the sizes differ (smaller ones stiffer and lighter, bigger ones softer and heavier). This overwrites your Small, Bigger and Huge settings.
page	medium_moves	medium	How far they move
number	medium_moves	Most they move up	medium_breast_max_up	0.005	0.25	0.005	100	%.1f cm		The furthest they can bounce up from where the pose puts them, before Strength.
number	medium_moves	Most they move down	medium_breast_max_down	0.005	0.25	0.005	100	%.1f cm		The furthest they can drop below where the pose puts them.
number	medium_moves	Most they move sideways	medium_breast_max_side	0.005	0.25	0.005	100	%.1f cm		The furthest they can swing to either side.
number	medium_moves	Most they move front and back	medium_breast_max_front	0.005	0.25	0.005	100	%.1f cm		The furthest they can swing forwards or back.
number	medium_moves	Up and down	medium_breast_up	0	3	0.1	1	%.1f×		How much of the movement shows up and down: the bounce from steps, jumps and sitting down.
number	medium_moves	Side to side	medium_breast_side	0	3	0.1	1	%.1f×		How much shows left and right: the sway when the Sim turns or shifts weight.
number	medium_moves	Front and back	medium_breast_front	0	3	0.1	1	%.1f×		How much shows forwards and backwards: the swing when the Sim starts, stops or leans.
page	medium_shape	medium	Shape and touch
number	medium_shape	Size	medium_breast_scale	0.5	2	0.05	100	%.0f%%		How big they look, times her own CAS size. Their sizes for pushing grow with it.
number	medium_shape	Squish	medium_breast_squish	0	2	0.05	100	%.0f%%		How much they flatten where an arm or hand presses, and bulge out to the sides. 0% = never.
number	medium_shape	Apart	medium_breast_apart	-0.05	0.05	0.0025	100	%+.2f cm		How much wider the gap between them is, for Sims without a pie Spacing preset. Below 0 = pushed together.
number	medium_shape	Size for your hands	medium_breast_size_you	0.02	0.12	0.005	100	%.1f cm	vr	How big they count as when your VR hands touch them (radius). Bigger = touched sooner. Doesn't change how they look.
number	medium_shape	Size for her own arms	medium_breast_size	0.02	0.12	0.005	100	%.1f cm		How big they count as when her own arms and hands push them (radius).
page	medium_clothed	medium	Clothed
number	medium_clothed	Clothed: strength	medium_breast_clothed_strength	0	2	0.05	100	%.0f%%		How much of the jiggle is left while a top covers them, compared with nude.
number	medium_clothed	Clothed: bounce speed	medium_breast_clothed_hz	0.5	3	0.05	1	%.2f×		Bounce speed while clothed, times the nude one. Above 1 = tighter, firmer wobble.
number	medium_clothed	Clothed: damping	medium_breast_clothed_damping	0.5	3	0.05	1	%.2f×		Damping while clothed, times the nude one. Above 1 = the bouncing dies down sooner.
number	medium_clothed	Clothed: gravity	medium_breast_clothed_gravity	-1	1	0.05	100	%+.0f%%		Added to her gravity while a top covers them. Below 0 = the top lifts them, like a push-up bra.
number	medium_clothed	Clothed: apart	medium_breast_clothed_apart	-0.05	0.05	0.0025	100	%+.2f cm		Added to her spacing while a top covers them. Below 0 = pushed together.
page	bigger	breasts_adv	Bigger
number	bigger	Strength	bigger_breast_strength	0	8	0.1	1	%.1f		How much of the physics movement shows. 0 = no jiggle, 1 = what the physics does, higher exaggerates every bounce.
number	bigger	Bounce speed	bigger_breast_hz	0.5	12	0.25	1	%.2f Hz		How many times a second they swing back and forth. Low = slow, heavy, floppy sway. High = quick, tight, firm wobble.
number	bigger	Damping	bigger_breast_damping	0.05	1	0.05	1	%.2f		How fast the bouncing dies down. Low = keeps bouncing a long time after a move. 1 = settles at once without swinging back.
number	bigger	Swing	bigger_breast_swing	0	2	0.05	100	%.0f%%		How much they tilt as they move, not just shift: a breast that drops points down. 0% = they only shift.
number	bigger	Weight	bigger_breast_weight	0.1	3	0.05	1	%.2f kg		How heavy each one is. Heavier = falls behind sharp moves and swings back slower, less lively. Light = quick and springy.
number	bigger	Gravity	bigger_breast_gravity	0	1	0.05	100	%.0f%%		Lets gravity pull them down, so they sag and swing with more weight. For Sims without a pie Gravity preset. 0% = the pose alone decides how they hang.
link	bigger	How far they move	bigger_moves
link	bigger	Shape and touch	bigger_shape
link	bigger	Clothed	bigger_clothed
confirm	bigger	Scale the others from Bigger	scale_from_bigger		Sets Small, Medium and Huge from your Bigger settings, each scaled the way the sizes differ (smaller ones stiffer and lighter, bigger ones softer and heavier). This overwrites your Small, Medium and Huge settings.
page	bigger_moves	bigger	How far they move
number	bigger_moves	Most they move up	bigger_breast_max_up	0.005	0.25	0.005	100	%.1f cm		The furthest they can bounce up from where the pose puts them, before Strength.
number	bigger_moves	Most they move down	bigger_breast_max_down	0.005	0.25	0.005	100	%.1f cm		The furthest they can drop below where the pose puts them.
number	bigger_moves	Most they move sideways	bigger_breast_max_side	0.005	0.25	0.005	100	%.1f cm		The furthest they can swing to either side.
number	bigger_moves	Most they move front and back	bigger_breast_max_front	0.005	0.25	0.005	100	%.1f cm		The furthest they can swing forwards or back.
number	bigger_moves	Up and down	bigger_breast_up	0	3	0.1	1	%.1f×		How much of the movement shows up and down: the bounce from steps, jumps and sitting down.
number	bigger_moves	Side to side	bigger_breast_side	0	3	0.1	1	%.1f×		How much shows left and right: the sway when the Sim turns or shifts weight.
number	bigger_moves	Front and back	bigger_breast_front	0	3	0.1	1	%.1f×		How much shows forwards and backwards: the swing when the Sim starts, stops or leans.
page	bigger_shape	bigger	Shape and touch
number	bigger_shape	Size	bigger_breast_scale	0.5	2	0.05	100	%.0f%%		How big they look, times her own CAS size. Their sizes for pushing grow with it.
number	bigger_shape	Squish	bigger_breast_squish	0	2	0.05	100	%.0f%%		How much they flatten where an arm or hand presses, and bulge out to the sides. 0% = never.
number	bigger_shape	Apart	bigger_breast_apart	-0.05	0.05	0.0025	100	%+.2f cm		How much wider the gap between them is, for Sims without a pie Spacing preset. Below 0 = pushed together.
number	bigger_shape	Size for your hands	bigger_breast_size_you	0.02	0.12	0.005	100	%.1f cm	vr	How big they count as when your VR hands touch them (radius). Bigger = touched sooner. Doesn't change how they look.
number	bigger_shape	Size for her own arms	bigger_breast_size	0.02	0.12	0.005	100	%.1f cm		How big they count as when her own arms and hands push them (radius).
page	bigger_clothed	bigger	Clothed
number	bigger_clothed	Clothed: strength	bigger_breast_clothed_strength	0	2	0.05	100	%.0f%%		How much of the jiggle is left while a top covers them, compared with nude.
number	bigger_clothed	Clothed: bounce speed	bigger_breast_clothed_hz	0.5	3	0.05	1	%.2f×		Bounce speed while clothed, times the nude one. Above 1 = tighter, firmer wobble.
number	bigger_clothed	Clothed: damping	bigger_breast_clothed_damping	0.5	3	0.05	1	%.2f×		Damping while clothed, times the nude one. Above 1 = the bouncing dies down sooner.
number	bigger_clothed	Clothed: gravity	bigger_breast_clothed_gravity	-1	1	0.05	100	%+.0f%%		Added to her gravity while a top covers them. Below 0 = the top lifts them, like a push-up bra.
number	bigger_clothed	Clothed: apart	bigger_breast_clothed_apart	-0.05	0.05	0.0025	100	%+.2f cm		Added to her spacing while a top covers them. Below 0 = pushed together.
page	huge	breasts_adv	Huge
number	huge	Strength	huge_breast_strength	0	8	0.1	1	%.1f		How much of the physics movement shows. 0 = no jiggle, 1 = what the physics does, higher exaggerates every bounce.
number	huge	Bounce speed	huge_breast_hz	0.5	12	0.25	1	%.2f Hz		How many times a second they swing back and forth. Low = slow, heavy, floppy sway. High = quick, tight, firm wobble.
number	huge	Damping	huge_breast_damping	0.05	1	0.05	1	%.2f		How fast the bouncing dies down. Low = keeps bouncing a long time after a move. 1 = settles at once without swinging back.
number	huge	Swing	huge_breast_swing	0	2	0.05	100	%.0f%%		How much they tilt as they move, not just shift: a breast that drops points down. 0% = they only shift.
number	huge	Weight	huge_breast_weight	0.1	3	0.05	1	%.2f kg		How heavy each one is. Heavier = falls behind sharp moves and swings back slower, less lively. Light = quick and springy.
number	huge	Gravity	huge_breast_gravity	0	1	0.05	100	%.0f%%		Lets gravity pull them down, so they sag and swing with more weight. For Sims without a pie Gravity preset. 0% = the pose alone decides how they hang.
link	huge	How far they move	huge_moves
link	huge	Shape and touch	huge_shape
link	huge	Clothed	huge_clothed
confirm	huge	Scale the others from Huge	scale_from_huge		Sets Small, Medium and Bigger from your Huge settings, each scaled the way the sizes differ (smaller ones stiffer and lighter, bigger ones softer and heavier). This overwrites your Small, Medium and Bigger settings.
page	huge_moves	huge	How far they move
number	huge_moves	Most they move up	huge_breast_max_up	0.005	0.25	0.005	100	%.1f cm		The furthest they can bounce up from where the pose puts them, before Strength.
number	huge_moves	Most they move down	huge_breast_max_down	0.005	0.25	0.005	100	%.1f cm		The furthest they can drop below where the pose puts them.
number	huge_moves	Most they move sideways	huge_breast_max_side	0.005	0.25	0.005	100	%.1f cm		The furthest they can swing to either side.
number	huge_moves	Most they move front and back	huge_breast_max_front	0.005	0.25	0.005	100	%.1f cm		The furthest they can swing forwards or back.
number	huge_moves	Up and down	huge_breast_up	0	3	0.1	1	%.1f×		How much of the movement shows up and down: the bounce from steps, jumps and sitting down.
number	huge_moves	Side to side	huge_breast_side	0	3	0.1	1	%.1f×		How much shows left and right: the sway when the Sim turns or shifts weight.
number	huge_moves	Front and back	huge_breast_front	0	3	0.1	1	%.1f×		How much shows forwards and backwards: the swing when the Sim starts, stops or leans.
page	huge_shape	huge	Shape and touch
number	huge_shape	Size	huge_breast_scale	0.5	2	0.05	100	%.0f%%		How big they look, times her own CAS size. Their sizes for pushing grow with it.
number	huge_shape	Squish	huge_breast_squish	0	2	0.05	100	%.0f%%		How much they flatten where an arm or hand presses, and bulge out to the sides. 0% = never.
number	huge_shape	Apart	huge_breast_apart	-0.05	0.05	0.0025	100	%+.2f cm		How much wider the gap between them is, for Sims without a pie Spacing preset. Below 0 = pushed together.
number	huge_shape	Size for your hands	huge_breast_size_you	0.02	0.12	0.005	100	%.1f cm	vr	How big they count as when your VR hands touch them (radius). Bigger = touched sooner. Doesn't change how they look.
number	huge_shape	Size for her own arms	huge_breast_size	0.02	0.12	0.005	100	%.1f cm		How big they count as when her own arms and hands push them (radius).
page	huge_clothed	huge	Clothed
number	huge_clothed	Clothed: strength	huge_breast_clothed_strength	0	2	0.05	100	%.0f%%		How much of the jiggle is left while a top covers them, compared with nude.
number	huge_clothed	Clothed: bounce speed	huge_breast_clothed_hz	0.5	3	0.05	1	%.2f×		Bounce speed while clothed, times the nude one. Above 1 = tighter, firmer wobble.
number	huge_clothed	Clothed: damping	huge_breast_clothed_damping	0.5	3	0.05	1	%.2f×		Damping while clothed, times the nude one. Above 1 = the bouncing dies down sooner.
number	huge_clothed	Clothed: gravity	huge_breast_clothed_gravity	-1	1	0.05	100	%+.0f%%		Added to her gravity while a top covers them. Below 0 = the top lifts them, like a push-up bra.
number	huge_clothed	Clothed: apart	huge_breast_clothed_apart	-0.05	0.05	0.0025	100	%+.2f cm		Added to her spacing while a top covers them. Below 0 = pushed together.
page	butt	main	Butt
switch	butt	Butt jiggles	butt_on		Turns butt jiggle on or off without changing the settings below.
number	butt	Only within	butt_distance	0	50	1	1	%.0f m		Only Sims this close to your Sim jiggle; farther ones are left alone, which saves processing. 0 m = any distance.
number	butt	Strength	butt	0	8	0.1	1	%.1f		How much of the physics movement shows. The game has no butt bone, so this moves the thigh twist bones: the lower butt and upper thighs jiggle together.
number	butt	Holding the butt: finger bend	butt_grab_curl	0	1	0.05	100	%.0f%%	vr	How far your fingers close holding the butt (0% open, 100% a fist). Without Integrated Body Physics, Grab: finger bend applies.
link	butt	Advanced	butt_adv
page	butt_adv	butt	Butt: advanced
number	butt_adv	Bounce speed	butt_hz	0.5	12	0.25	1	%.2f Hz		How many times a second it swings back and forth. Low = slow, heavy sway. High = quick, firm wobble.
number	butt_adv	Damping	butt_damping	0.05	1	0.05	1	%.2f		How fast the bouncing dies down. Low = keeps bouncing a long time. 1 = settles at once.
number	butt_adv	Most it moves	butt_max	0.005	0.15	0.005	100	%.1f cm		The furthest it can move from where the pose puts it, before Strength.
number	butt_adv	Gravity	butt_gravity	0	1	0.05	100	%.0f%%		Lets gravity pull it down, for a heavier sag and swing. 0% = the pose alone decides.
number	butt_adv	Size for pushing	butt_size	0.02	0.12	0.005	100	%.1f cm		How big it counts as when hands push it (radius). Bigger = touched sooner.
link	butt_adv	Directions	butt_moves
link	butt_adv	Clothed	butt_clothed
page	butt_moves	butt_adv	Directions
number	butt_moves	Up and down	butt_up	0	3	0.1	1	%.1f×		How much of the movement shows up and down: the bounce from steps, jumps and sitting down.
number	butt_moves	Side to side	butt_side	0	3	0.1	1	%.1f×		How much shows left and right: the sway with each step and turn.
number	butt_moves	Front and back	butt_front	0	3	0.1	1	%.1f×		How much shows forwards and backwards: the swing when the Sim starts, stops or bends.
page	butt_clothed	butt_adv	Clothed
number	butt_clothed	Clothed: strength	butt_clothed_strength	0	2	0.05	100	%.0f%%		How much of the jiggle is left while a bottom covers it, compared with nude. Nude = the Sim's own bathing outfit part, as when undressed.
number	butt_clothed	Clothed: bounce speed	butt_clothed_hz	0.5	3	0.05	1	%.2f×		Bounce speed while clothed, times the nude one. Above 1 = tighter, firmer wobble, as when fabric holds it.
number	butt_clothed	Clothed: damping	butt_clothed_damping	0.5	3	0.05	1	%.2f×		Damping while clothed, times the nude one. Above 1 = the bouncing dies down sooner.
number	butt_clothed	Clothed: gravity	butt_clothed_gravity	-1	1	0.05	100	%+.0f%%		Added to Gravity above while a bottom covers it. Below 0 lifts it.
page	hands	main	Pushing and holding
switch	hands	Arms and hands push them	arm_push		Sims' arms and hands press into breasts (and hips) and push them aside. Yours follow your controllers.
switch	hands	Your hands push them	your_hands	vr	Your Sim's forearms, palms and fingers (your VR hands) push other Sims' breasts when you touch them. Uses Arm thickness too.
switch	hands	Hold with your hand	grab	vr	Press grip and trigger together with your VR hand on a breast to hold it and move it; let go of either to release it.
link	hands	Advanced	hands_adv
page	hands_adv	hands	Pushing and holding: advanced
number	hands_adv	Arm thickness for pushing	arm_size	0.5	2	0.1	1	%.1f×		How thick arms and hands count as when they push. Higher touches sooner and pushes further.
number	hands_adv	Holding: furthest you can pull	grab_reach	0.02	0.3	0.01	100	%.0f cm	vr	How far a held breast can be pulled from where it rests.
number	hands_adv	Holding: press grip and trigger	grab_press	0.1	1	0.05	100	%.0f%%	vr	How far grip and trigger must both be pressed to hold. Lower = a lighter squeeze holds.
)IBP"
