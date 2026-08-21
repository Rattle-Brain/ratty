# The bundled default configuration

`src/config/default_config.yaml` is compiled into the binary through
`resources/config.qrc` and is the first thing read at start-up. It carries the
accepted values for each setting and nothing else — the *reasoning* lives here,
so that the shipped file stays short enough to scan while still telling you what
a field will take.

For the full list of every setting and what it accepts, see
[configuration.md](configuration.md). This page is about the *defaults* — what
they are, why they were chosen, and the keys the file does not mention.

---

## How to change anything

Do not edit `default_config.yaml`. Write `~/.config/ratty/config.yaml` instead;
it is an **overlay**, so it only needs the keys you want to change and everything
else keeps the default below. A complete, valid config can be one line.

## The one YAML trap

`#` starts a comment, so a hex colour must be quoted:

```yaml
background: "#1e1e1e"     # correct
background: #1e1e1e       # an empty value followed by a comment
```

RaTTY detects that specific mistake and names it rather than silently reading an
empty value, but the quotes are the fix.

---

## `font`

```yaml
font:
  family:
    - DroidSansMono Nerd Font
    - DroidSansM Nerd Font
    - Droid Sans Mono Nerd Font
    - DroidSansMono NF
    - Droid Sans Mono for Powerline
  fallback: []
  size: 13
  emoji_scale: 1.25
```

`family` is a preference *list*, tried in order — the first one actually
installed wins. The five entries are the same font under the names different Nerd
Fonts releases have given it, so one of them matches whatever the user happens to
have. If none are installed, RaTTY falls back to whatever the system has set as
its monospaced default; nothing about that fallback is hard-coded.

`emoji_scale` is how tall a colour emoji is drawn, as a multiple of the primary
font's **capital height** — `1.0` makes an emoji exactly as tall as an `M`.

The default is above parity, which looks wrong written down and right on screen.
An emoji is a round, busy shape and a capital is a flat one, so matching their
heights makes the emoji read as *smaller* than the text — the same optical
correction a typeface makes when it draws `O` slightly taller than `H`. Whatever
the value, it is bounded at use by the cell the glyph occupies, so raising it
cannot make emoji overlap; it just stops growing.

It is a dial rather than a fixed rule because the alternative does not work.
A colour font ships a handful of fixed bitmap strikes — Apple Color Emoji has 20,
26, 32, 40 and more — and FreeType answers a size request with the nearest one,
so left to the font the emoji size hops between strikes instead of following the
text. RaTTY picks the smallest strike at or above the size it wants and resamples
it down, which is why the size tracks the font exactly and an emoji can never
leave its cell.

`fallback` is consulted for code points the primary font lacks, *ahead* of
RaTTY's own search. It is empty by default because the automatic chain already
covers the cases that matter: the system monospaced font, any installed
colour-emoji font, and the Nerd Fonts symbols font bundled into the binary —
which is what draws a TUI's file-type icons, since those live in private-use code
points no stock font carries. Use this list only to pin a specific family of your
own.

## `cursor`

```yaml
cursor:
  style: bar
  blink: true
```

`style` is one of `block`, `hollow`, `underline` or `bar`. An application's own
request through `DECSCUSR` takes precedence for as long as it applies, so an
editor signalling its mode still works.

## `theme` and `colors`

```yaml
theme: ratty-dark
colors: {}
```

Eleven themes ship with RaTTY:

| | | |
| --- | --- | --- |
| `ratty-dark` | `catppuccin-mocha` | `nord` |
| `ratty-light` | `one-dark` | `tokyo-night` |
| `dracula` | `gruvbox-dark` | `gruvbox-light` |
| `solarized-dark` | `solarized-light` | |

`ratty-dark` is the default: a Nord-derived scheme with cool blue-grey surfaces,
muted aurora accents and a teal cursor. `ratty-light` is its daylight
counterpart, built from the same family — the background is Nord's dimmest
snow-storm grey rather than white, because a full-white terminal is the most
tiring thing a light scheme can do, and every accent is darkened until it holds
its weight against it.

Both state their `tab_bar.colors` rather than leaving them to be derived, which
is why they are the two themes where the bar is a deliberate composition rather
than a shift of the terminal background.

Anything under `colors:` overrides the theme, so you can take a scheme and change
one thing. Order does not matter — the theme is always applied first:

```yaml
theme: nord
colors:
  red: "#ff0000"
```

The keys `colors:` accepts, all optional:

```yaml
colors:
  background:           "#1e1e1e"
  foreground:           "#dcdcdc"
  cursor:               "#dcdcdc"
  selection_background: "#6495ed80"
  black:   "#000000"    bright_black:   "#666666"
  red:     "#cd3131"    bright_red:     "#f14c4c"
  green:   "#0dbc79"    bright_green:   "#23d18b"
  yellow:  "#e5e510"    bright_yellow:  "#f5f543"
  blue:    "#2472c8"    bright_blue:    "#3b8eea"
  magenta: "#bc3fbc"    bright_magenta: "#d670d6"
  cyan:    "#11a8cd"    bright_cyan:    "#29b8db"
  white:   "#e5e5e5"    bright_white:   "#ffffff"
```

`cursor` left unset follows the foreground, so a theme that inverts the terminal
does not leave the cursor invisible.

## `tab_bar`

```yaml
tab_bar:
  style: powerline
  position: bottom
  show: multiple
  colors: {}
```

`style` is one of:

| Style | Appearance |
| --- | --- |
| `minimal` | text only, with a short accent along the edge facing the terminal |
| `underline` | a full-width accent rule and a faint wash behind the active tab |
| `blocks` | the active tab is a filled rectangle, tabs divided by hairlines |
| `pills` | the active tab is a filled rounded capsule |
| `powerline` | angled chevrons, echoing a Powerline prompt |

`position` is `top` or `bottom`. `show` is `always`, `multiple` or `never`;
`multiple` hides the bar until a second tab exists, so a single-terminal window
looks like a terminal rather than a tabbed document.

Every colour under `tab_bar.colors` is optional. Left out, it is *derived* from
the terminal palette, which is what lets a colour theme be defined without
restating the chrome:

```yaml
tab_bar:
  colors:
    background:          "#252525"
    border:              "#3a3a3a"
    active_background:   "#1e1e1e"
    active_foreground:   "#dcdcdc"
    inactive_foreground: "#8a8a8a"
    accent:              "#3b8eea"
```

`accent` is worth knowing about: left unset it becomes the palette's bright blue,
which every theme defines, so it tracks the theme automatically. It is also what
the split separator is derived from.

## `scrollback`

```yaml
scrollback:
  lines: 10000
  multiplier: 3
```

`lines` is per pane and applies to the primary screen only — the alternate screen
keeps no history, because a full-screen application repaints rather than scrolls.
`0` turns the buffer off entirely.

`multiplier` is how many rows one wheel notch moves. `Shift+PageUp` and
`Shift+PageDown` move a whole page.

## `mouse`

```yaml
mouse:
  alternate_scroll: true
```

With a full-screen application up (`less`, `man`, `vim`) and no mouse reporting
active, translate a wheel notch into cursor keys so the pager scrolls.
Applications can still turn this off for themselves with `DECRST 1007`.

Separately, and not configurable: applications that ask for the mouse
(`DECSET 1000`/`1002`/`1003`) get it — clicks, drags and the wheel are reported to
them instead of scrolling the scrollback. Hold **Shift** to bypass that and
scroll locally anyway.

## `splits`

```yaml
splits:
  dim_unfocused: true
  dim_strength: 0.35
```

`dim_unfocused` fades every pane except the one holding the keyboard, so which is
which is obvious at a glance. A tab with only one pane is never dimmed — there
would be nothing to tell apart.

`dim_strength` runs from `0.0` (no dimming) to `0.9`. The default is deliberately
mild: an unfocused pane is still meant to be readable.

`separator` is not in the shipped file because its default is *derived*: the
theme's accent colour, muted back towards the background, so a blue theme gets a
blue hairline and a green one a green hairline without stating it. Name a colour
to override:

```yaml
splits:
  separator: "#3b8eea"
```

## `directories`

```yaml
directories:
  new_tab: home
  new_split: cwd
```

Where a newly opened pane's shell starts. Each accepts:

| Value | Meaning |
| --- | --- |
| `home` | the home directory |
| `cwd` | the directory the pane it was opened from is in (`inherit` is a synonym) |
| a path | e.g. `~/work` or `/srv/project`; `~` is expanded |

They differ by default because a tab and a split are asked for in different
frames of mind. A tab is a fresh piece of work, so it starts at `home`; a split
is nearly always a second view of the job already in hand, so it follows the pane
it came from. Set `new_split: home` for the old behaviour.

## `window`

```yaml
window:
  width: 1280
  height: 720
  padding: 4
  opacity: 1.0
  fullscreen: false
```

`padding` is the gap between the text and the window edge, in **logical** pixels,
scaled by the device pixel ratio when it is used. `width` and `height` are
start-up geometry only; a configuration reload deliberately leaves the window
where the user has since put it.

## `mac_os_bindings`

```yaml
mac_os_bindings: auto
```

Which set of default keybindings to load.

| Value | Meaning |
| --- | --- |
| `auto` | follow the platform: the macOS set on macOS, the Linux set elsewhere |
| `true` | force the macOS set — a Mac keyboard on a Linux machine, say |
| `false` | force the Linux set |

The two sets are the same bindings written two ways: the macOS file says `cmd`
and the Linux file says `super`, because that is what a reader of each platform
expects. Qt maps both to the same modifier, so they behave identically — an
invariant the test suite asserts, so the two cannot drift apart.

- macOS: `resources/keybindings/macos.yaml`
- Linux: `resources/keybindings/linux.yaml`

To change a binding, add a `keybindings:` section to
`~/.config/ratty/config.yaml`. Naming an action there replaces **every** key the
defaults gave it; set an action to `none` to remove it:

```yaml
keybindings:
  ctrl+shift+t: split_vertical   # move it off ctrl+shift+w
  ctrl+shift+w: none             # and drop the old key
```

See [keybindings.md](keybindings.md) for every action name and the default keys.

---

## Reloading without restarting

`cmd+f5` / `super+f5` (also `cmd+shift+r` / `super+shift+r`) re-reads the
configuration and applies it to every pane in every tab. The shells keep running.

Because the loader rebuilds every layer from scratch rather than merging onto what
is already there, a setting you **delete** from your config correctly reverts to
its default on reload rather than lingering.

Two deliberate exceptions: the window's size and fullscreen state are not
reasserted, since those are start-up settings and re-applying them would fight
whatever you have since done with the window. And a reload resets any palette an
application had set for itself through `OSC 4`/`10`/`11`/`12` — you asked to see
the configuration, so the configuration is what you get.
