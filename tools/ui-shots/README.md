# ui-shots

Renders 590Ctrl's main window to PNG files, for checking layout changes without a radio:
both arrangements (tabs below, tabs beside), each tab, with and without the tabs, dark and light.

```sh
cmake --build build --target ui-shots
QT_QPA_PLATFORM=offscreen build/ui-shots /tmp/590ctrl-shots
```

It connects to the built-in simulator and keeps its settings in the output folder, so it never
opens a serial port and never reads or writes 590Ctrl's real settings.
