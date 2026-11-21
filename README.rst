LuaDiant
========

A fork of `NetRadiant-custom <https://github.com/Garux/netradiant-custom>`_
that deeply integrates a Lua scripting layer into the editor.

Scripts can drive the editor in two ways:

* **Headless**: load, inspect, modify and save maps and run the build
  tools without a GUI, e.g. for batch processing, automation and CI.
* **Integrated**: hook into the running editor's UI and plugin system to
  add commands, tools and workflows on top of the regular editing
  experience.

Apart from that it tracks upstream NetRadiant-custom closely. The original
upstream README is kept at `.legacy/README.md <.legacy/README.md>`_.

Development
-----------

See ``./dev.py --help`` for building, running and testing.
