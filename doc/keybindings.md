# Keybindings

### Two default keybinding files

The defaults live in `:/keybindings/macos.yaml` and `:/keybindings/linux.yaml`,
and `mac_os_bindings` (`auto` | `true` | `false`) chooses between them. `auto`
follows the platform; forcing it covers a Mac keyboard on a Linux machine.

The two files describe the *same* bindings and differ only in whether the Meta
modifier is spelled `cmd` or `super` — Qt maps both to `Qt::MetaModifier`, so the
resolved key combinations are identical. The split exists for readability: a
Linux user should not have to mentally translate `cmd`. Because that makes the
files duplicates, `tests/test_input.cpp` asserts they resolve identically, so the
two cannot drift apart.

Loading them is the awkward part. *Which* file to load depends on
`mac_os_bindings`, which the user's own configuration may set — so the defaults
can only be read **after** the user's file, and must still merge **underneath**
it. Bindings are therefore staged rather than applied as they are parsed:

```
load()
  built-in defaults
  bundled default_config.yaml   -> may set mac_os_bindings
  user config.yaml              -> may set mac_os_bindings; stages userBindings_
  resolvePlatformBindings()     -> decide which file
  loadKeybindings()             -> stages builtInBindings_
  resolveKeybindings()          -> builtIn (no ownership), then user (ownership)
```

There is a single `keybindings:` section for user overrides. An earlier design had
one section per platform, which is unnecessary once the two sets are known to be
equivalent — and it meant a user could silently edit the inactive one.

### `cmd` and `ctrl` mean the same thing everywhere

Qt swaps Control and Meta on macOS by default: `Qt::ControlModifier` is the
Command key and `Qt::MetaModifier` is physical Control. For a terminal that is
backwards in the worst way — `InputHandler` maps `Qt::ControlModifier` to C0
control characters, so **Command+C sent SIGINT and physical Ctrl+C did nothing**
— and it would make a `cmd+t` binding fire on Ctrl+T.

`main()` therefore sets `Qt::AA_MacDontSwapCtrlAndMeta`, after which
`Qt::ControlModifier` is always physical Control and `Qt::MetaModifier` is always
Command. Verified:

```
default:                  Qt::ControlModifier + T -> ⌘T    Qt::MetaModifier + T -> ⌃T
AA_MacDontSwapCtrlAndMeta: Qt::ControlModifier + T -> ⌃T    Qt::MetaModifier + T -> ⌘T
```

### Layout tolerance

Qt reports either the unshifted key or the shifted symbol for the same physical
key, depending on platform and layout: `Ctrl+Shift+1` arrives as `Key_1` on one
machine and `Key_Exclam` on another. A binding matched only against the literal
combination would therefore work on some keyboards and not others.

`Config::lookupAction(const QKeyEvent*)` handles this by retrying with the key's
shift partner (`1`↔`!`, `\`↔`|`, `-`↔`_`, `=`↔`+`, …). The retry happens **only**
when Shift is held: without Shift there is no ambiguity about which symbol was
meant, and rewriting unshifted keys would risk turning `Ctrl+C` into a shortcut.

A consequence worth keeping in mind when editing the defaults: two actions must
never be bound to the two halves of the same physical key, because which one wins
would then depend on the user's keyboard. The default bindings put font sizing on
`+`/`-` and splits on letters (plus `\` as an alias) for exactly this reason.

Font sizing needs care for the same reason, since "plus" is not one key event.
`⌘=`, `⌘⇧=` (which types `+`) and a numeric-keypad `⌘+` are three different
combinations, so both files bind `equal`, `shift+equal` and `plus`, with `minus`
and `shift+minus` mirroring it.

There is a third fallback, which exists for a case the test suite caught: after
trying the literal combination and then the key's shift partner, the lookup
retries **without Shift**. A binding such as `cmd+1` carries no Shift, yet on
layouts where the digits are the *shifted* symbols — AZERTY among others — typing
it necessarily holds Shift down. `shiftPartner()` only knows digits and
punctuation, never letters, and the fallback runs only for keys it recognises, so
`ctrl+shift+c` can never decay into `ctrl+c` and steal an interrupt from the
shell. Both halves of that are asserted.

### Splits are on `v` and `w`

`ctrl+shift+v` splits vertically (top / bottom) and `ctrl+shift+w` horizontally
(left / right), with `ctrl+shift+c` closing the focused pane. The two split keys
were the other way round until the mnemonic won: `v` for vertical.

**On Linux this is a known problem, not a preference.** `ctrl+shift+c` and
`ctrl+shift+v` are copy and paste in nearly every other terminal on that
platform, and here they close a pane and split one. Copy and paste do work — on
`super+c` and `super+v`, because the two default sets are the same bindings with
`cmd` spelled `super` — but that is the macOS habit transplanted, and `super`
belongs to the desktop on most Linux systems anyway. Rethinking the Linux set is
the top item in [`todo-ratty.md`](../todo-ratty.md); it is listed there rather
than quietly fixed because closing a pane is muscle memory too, and because it
means deciding whether the two files should stay equivalent at all.

Opening a split focuses the pane it created, and closing one returns the caret to
the pane it was opened from rather than to whichever leaf sits nearest. Neither
is a keybinding concern as such — see
[which pane is current](ui.md#which-pane-is-current).

### Word and line editing

Alt/Option for a word, Cmd/Super for the whole line, is what every native text
field on both platforms does, and a shell prompt is a text field as far as the
user is concerned. None of it is a RaTTY *action*: these keys belong to whatever
is running, so `InputHandler::encodeEditingKey()` translates each one into the
readline binding that does the same job (zsh's emacs mode agrees on all of them).

| Key | Sent | readline |
| --- | --- | --- |
| `Alt+Left` / `Alt+Right` | `ESC b` / `ESC f` | `backward-word` / `forward-word` |
| `Alt+Backspace` | `ESC DEL` | `backward-kill-word` |
| `Alt+Delete` | `ESC d` | `kill-word` |
| `Cmd+Left` / `Cmd+Right` | `Ctrl+A` / `Ctrl+E` | line start / line end |
| `Cmd+Backspace` | `Ctrl+U` | kill back to the start of the line |
| `Cmd+Delete` | `Ctrl+K` | kill to the end of the line |

Forwarding the literal xterm form instead — `Alt+Left` is `CSI 1;3D` — is why
these keys used to do nothing at all: a default bash or zsh binds none of it.
That form is still what `Ctrl+Arrow` sends (`CSI 1;5C`, which readline *does*
bind), and `encodeEditingKey()` bows out whenever Control is held for a second
reason: Ctrl+Alt is how X11 reports AltGr.

### Typing `~`, and the rest of the third level

Two separate things stood between a Spanish keyboard and a tilde, and only the
second one is really about keys at all.

**`~` is a dead key.** `Option+ñ` on macOS and `AltGr+ñ` on Linux both start a
composition rather than producing a character, exactly as the accents do (acute,
then `a`, for `á`). The platform's input method holds that composition and
delivers the result as a `QInputMethodEvent` — but only to a widget that has set
`Qt::WA_InputMethodEnabled`, which `TerminalWidget` never did. So the
composition was dropped on the floor on both platforms, and no input method
(CJK and friends) worked either. See
[composed input](ui.md#composed-input-dead-keys-and-input-methods).

**Option and AltGr are compose keys, not Meta.** The parts of the third level
that are *not* dead keys — `|`, `@`, `[`, `]`, `{`, `}`, `€`, `\` — do arrive as
ordinary key events, with `Qt::AltModifier` set. RaTTY used to ESC-prefix
anything carrying that modifier, turning them into Meta keys nothing would
recognise.

`InputHandler::isComposedText()` tells the two apart using the fact that Qt
reports `key()` as the character the key carries with **no** modifiers applied at
all: the ñ key is `Key_Ntilde` no matter what Option did to it. So a character
that disagrees with its own key was composed by the layout and is sent as itself,
while `Alt+B` — where `'B'` and `'b'` agree — still becomes the `ESC b` that
readline binds to `backward-word`. Multi-character results (dead keys, surrogate
pairs) are text by definition; control characters never come from a layout.

The word- and line-editing keys above are unaffected either way, because arrows
and Backspace carry no text for a layout to compose.

### Scrolling

`scroll_up` and `scroll_down` move the scrollback view by a page — one screenful
less a row, so the line the eye stopped on survives the jump — and
`clear_scrollback` discards the history of the focused pane. The defaults are
`Shift+PageUp` / `Shift+PageDown` and `cmd+k` (`super+k` on Linux).

They are bound in the keybinding files and dispatched by `MainWindow` like any
other action, which also means they never reach the shell: `Shift+PageUp` would
otherwise arrive as `CSI 5 ; 2 ~` and be interpreted by whatever is running.

Any keystroke that produces terminal input returns the view to the live screen
first. Scrolling back and then typing must not leave the echo out of sight.

### Selection and the clipboard

`copy` is on `cmd+c` (`super+c` on Linux) and `paste` on `cmd+v` / `super+v`.
Neither is a terminal input key on either platform once
`AA_MacDontSwapCtrlAndMeta` is set, which is what makes them available at all:
`ctrl+c` remains the interrupt, as it must.

Selecting is done with the mouse rather than the keyboard — drag, double-click for
a word, triple-click for a line, `Alt`+drag for a rectangle — and none of it is a
bindable action, so there is nothing here to configure. `copy` with nothing
selected does nothing rather than complaining; it is bound to a key, and a key
that sometimes objects is worse than one that sometimes does nothing.

Middle-click pastes the primary selection where the platform has one, which is
also what a completed selection is written to. See
[selection](ui.md#selection) for the gestures and
[`clipboard`](default-config.md#clipboard) for the two `OSC 52` switches.

Typing returns the view to the live screen *and* drops the selection: the text is
about to move, and a highlight left behind on it reads as a bug. Keybindings are
consulted before that happens, so `copy` never clears what it is copying.

### Searching the scrollback

| Action | Default | Does |
| --- | --- | --- |
| `search` | `cmd+f` / `super+f` | opens the prompt over the bottom row |
| `find_next` | `cmd+g` / `super+g` | steps towards newer output |
| `find_previous` | `cmd+shift+g` / `super+shift+g` | steps towards older output |

Inside the prompt the keys are its own: typing refines the query, `Backspace` and
`Ctrl+U` edit it, `Return` steps back through the buffer and `Shift+Return`
forward, the arrows do the same, and `Escape` closes it leaving the current match
selected so it can be copied. Bindings still fire while it is open, because they
are checked first.

`find_next` with no query open starts a search, and a query survives closing the
prompt — so `cmd+g` after `Escape` resumes the last one rather than starting from
nothing.

### Reloading the configuration

`reload_config` re-reads `~/.config/ratty/config.yaml` and applies it to every
pane in every tab, so a theme, font, colour or split setting can be tried without
restarting. The shells keep running: the pty and the child process have nothing to
do with any of these settings, and losing a running command to a colour change
would make the feature not worth having. Bound to `cmd+f5` and `cmd+shift+r`
(`super+` on Linux).

The loader rebuilds every layer from scratch rather than merging onto what is
already there, so a setting **deleted** from the file reverts to its default on
reload rather than lingering. Two things are deliberately left alone: the window's
size and fullscreen state, which are start-up settings and would otherwise fight
whatever the user has since done with the window. A reload does reset a palette an
application had set for itself through `OSC 4`/`10`/`11`/`12`, on the grounds that
the user has just asked to see the configuration.

There is no on-screen confirmation — a terminal has nowhere to put one — so a
reload writes `Config: reloaded` to stderr, and any YAML error in the file is
reported there too.

### Fullscreen

`fullscreen` is on `cmd+ctrl+f`, `F11` and `cmd+Enter` (`super+` on Linux). Both
spellings of Enter are bound, because Qt reports the main key as `Key_Return` and
the numeric keypad's as `Key_Enter` — binding one leaves the other dead. Plain
Enter is untouched and still sends a carriage return to the shell.

`Config::save()` no longer exists. It was a no-op that logged "not yet
implemented" while `closeEvent` dutifully wrote the window size into it;
persisting geometry is listed in `todo-ratty.md` instead of being pretended at.

---

