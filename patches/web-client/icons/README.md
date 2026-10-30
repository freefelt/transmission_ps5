# Icon sets of the web interface

`ps5-icons-color.css` and `ps5-icons-mono.css` replace the icons of the Transmission web interface (toolbar buttons,
the overflow menu, file and folder icons in the torrent list, the disk icons of the disk panel and of the folder tiles)
through CSS only. The SVG pictures are embedded in the CSS as `data:` URLs, so the build needs nothing else.
Which set is used is a setting on the settings page (`web_icons`: `color` or `mono`).

The icons come from these projects:

| Set | Source | License |
|---|---|---|
| mono | [Lucide](https://github.com/lucide-icons/lucide) | ISC |
| color: toolbar buttons | [MingCute](https://github.com/Richard9394/MingCute) (fill) | Apache-2.0 |
| color: torrent type icons | [Fluent Emoji Flat](https://github.com/microsoft/fluentui-emoji) | MIT |
| disk icons (M.2, USB flash drive, internal disk) | drawn for this project, based on the sets above | same as the project |
