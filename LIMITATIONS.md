# Known limitations

## Lua plugins: shortcuts and Reload Scripts

Every Lua action is registered as a regular editor command
(`Lua.<plugin>.<id>`). Editor commands cannot be removed or redefined once
they exist, and their shortcuts are read from `shortcuts.ini` only once at
startup. This has some consequences when using **Lua > Reload Scripts**:

- **Changed default shortcuts need a restart.** If a plugin changes the
  `shortcut` of an existing action, the new default is ignored until the
  editor is restarted. The command keeps the default it was registered
  with first.
- **New actions don't pick up saved bindings until a restart.** An action
  that first appears through a reload gets its default shortcut, even if
  `shortcuts.ini` has a binding saved for it. The saved binding applies
  from the next start.
- **Removed actions stay registered as commands.** An action which is no
  longer registered after a reload does nothing and has neither a menu
  item nor an active shortcut, but it is still listed in
  **Edit > Shortcuts...** until the editor is restarted.
