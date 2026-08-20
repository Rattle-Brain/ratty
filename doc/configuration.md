# Configuration


Settings are **YAML**, loaded in layers, each overriding only the keys it
contains:

1. built-in defaults (`Palette`'s constructor and `Config::applyBuiltInDefaults`)
2. `:/config/default_config.yaml` — compiled into the binary from
   `src/config/default_config.yaml`
3. `~/.config/ratty/config.yaml` — the user's overlay

The layering is the important property: an overlay is not a replacement, so a
file that mentions only `font.size` changes only that — every other colour,
binding, window setting and font preference keeps its default. A config without a
`keybindings` section must not leave the application with no keybindings, and the
bundled defaults must be found regardless of the working directory.

The one place the rule is deliberately not literal is keybindings, where naming
an *action* releases the keys it inherited; see
[Action ownership](#action-ownership).

Two settings interact, and the shipped defaults are arranged so the interaction
stays predictable: `colors.cursor` is deliberately **absent** from the bundled
file, so that it follows `colors.foreground`. Had the default set it explicitly, a
user who changed only the foreground would keep a cursor in the old colour —
close to invisible on an inverted theme.

YAML rather than JSON because a file people edit by hand wants comments, and
needs neither quoting of every key nor comma discipline. The parsing uses
`yaml-cpp`, kept out of every other translation unit by a nested
`Config::Parser` declared in the header and defined in the implementation file —
a nested class has access to its enclosing class's private members, so no
friendship is needed.

#### The one YAML sharp edge

`#` starts a comment, so `background: #1e1e1e` parses as an *empty value*, not a
colour. Every scalar reader returns `std::optional`, and the colour reader
recognises this specific case and says so:

```
Config: colour background is empty - hex colours must be quoted in YAML, e.g. "#1e1e1e"
```

The shipped default quotes every colour and documents the rule at the top of the
file. Silently reading an empty value here would paint the terminal black.

#### Failure behaviour

Each layer is independent and each key optional, so a broken overlay degrades
predictably rather than half-applying:

| Input | Result |
|---|---|
| YAML syntax error | whole overlay discarded, with line and column reported |
| top level is not a mapping | overlay discarded |
| empty file | no-op |
| unusable scalar (`size: "abc"`) | that key skipped and named; the rest of the file still applies |
| out-of-range value | clamped (`MIN_FONT_SIZE`..`MAX_FONT_SIZE`, padding, opacity) |
| unknown action name | reported and skipped |
| a `config.json` left over from before | reported as no longer read, with the path to move it to |

```yaml
font:
  family: [DroidSansMono Nerd Font, Menlo]   # or a single name
  fallback: []                               # rarely needed; see below
  size: 13

cursor:
  style: block        # block | hollow | underline | bar
  blink: true

colors:
  background: "#1e1e1e"
  foreground: "#dcdcdc"
  cursor: "#dcdcdc"
  red: "#cd3131"
  bright_red: "#f14c4c"

scrollback:
  lines: 10000        # 0 turns the scrollback off
  multiplier: 3       # lines moved per wheel notch

mouse:
  alternate_scroll: true

window:
  width: 1280
  height: 720
  padding: 4
  opacity: 1.0
  fullscreen: false

keybindings:
  ctrl+shift+t: new_tab
  ctrl+shift+w: none    # removes a default binding
```

- All 16 base ANSI colours are overridable by name (`black`, `red`, …,
  `bright_white`). Slots 16–255 are generated (6×6×6 cube plus greyscale ramp).
- `font.family` accepts a single name **or an array tried in order**. The array
  form is how a config can name a preferred font and still degrade gracefully on
  a machine where it is not installed; see
  [the font documentation](rendering.md#fontmanager) for the resolution rules. `"Monospace"` or `""` means
  "ask the platform".
- `font.fallback` names families to consult for code points the primary font
  lacks, ahead of automatic discovery, and a family that is not installed is
  skipped. Left empty it is not needed: the platform's monospaced font, any
  installed colour-emoji font, and the Nerd Fonts symbols font RaTTY *ships with*
  are all tried anyway. That bundled font is what draws a TUI's file-type icons,
  which live in private-use code points no stock font carries — the same reason
  kitty and Ghostty ship it too.
- `window.padding` is the gap between the text and the window edge, in logical
  pixels.
- `scrollback.lines` is per pane and applies to the primary screen only — the
  alternate screen keeps no history, because a full-screen application repaints
  instead of scrolling. `0` disables the buffer, so the wheel and
  `Shift+PageUp`/`PageDown` have nothing to move.
- `scrollback.multiplier` is how many rows one wheel notch moves. Fractional
  notches from a trackpad accumulate, so a slow drag still scrolls smoothly.
- `mouse.alternate_scroll` translates a wheel notch into cursor keys when a
  full-screen application is up and has not asked for the mouse, which is what
  makes the wheel scroll `less` and `man`. An application can turn it off for
  itself with `DECRST 1007`.
- An application that asks for the mouse (`DECSET 1000`/`1002`/`1003`) receives
  clicks, drags and wheel notches instead of the terminal acting on them. Hold
  **Shift** to bypass that and scroll — or middle-click paste — locally anyway.
- Cursor styles: `block`, `hollow`, `underline`, `bar`.
- Binding an action to `"none"` **removes** a default binding — the only way for
  a user overlay to unbind something it did not create.
- Action names live in one table (`kActionNames`) used for both directions of the
  string↔enum mapping, instead of a hand-maintained `switch` and `if`-chain.

Key sequences accept `ctrl`/`control`, `shift`, `alt`/`option`,
`meta`/`super`/`cmd`, named keys (`up`, `pageup`, `escape`, …), function keys
(`f1`…`f12`), and spelled-out punctuation (`plus`, `minus`, `underscore`,
`backslash`, `bracketleft`, …) — the last because `ctrl+shift++` cannot be split
on `+` unambiguously.

