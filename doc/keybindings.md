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

`Config::save()` no longer exists. It was a no-op that logged "not yet
implemented" while `closeEvent` dutifully wrote the window size into it;
persisting geometry is listed in `todo-ratty.md` instead of being pretended at.

---

