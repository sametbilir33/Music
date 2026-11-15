# Music ![Music](data/icons/hicolor/scalable/apps/io.github.sametbilir33.Music.svg)

**Music** is a native Linux music player built with C, GTK4, Libadwaita, and GStreamer.

## Features

- Modern GTK4 and Libadwaita interface
- Audio playback powered by GStreamer
- Browse and play music from a selected directory
- Open and play individual music files independently of the directory playlist
- Remember the last selected music directory
- Play, pause, and stop controls
- Previous and next track navigation
- Playlist management
  - Remove individual tracks from the playlist
  - Clear the playlist
  - Custom track ordering
  - Sort tracks alphabetically (A-Z / Z-A)
  - Sort tracks by modification date
- Volume control
- Playback progress and seeking
- MPRIS integration for desktop media controls
- Persistent application settings
- No database required

## Technologies

- **Language:** C
- **GUI:** GTK4
- **Desktop integration:** Libadwaita
- **Audio playback:** GStreamer
- **Build system:** CMake
- **Configuration:** GLib GKeyFile

## Dependencies

The following packages are required to build the application:

- GCC or another C17-compatible compiler
- CMake
- pkg-config
- GTK4
- Libadwaita
- GStreamer

On Arch Linux, install them with:

```bash
sudo pacman -S --needed base-devel cmake pkgconf gtk4 libadwaita gstreamer
```

## Building

Clone the repository:

```bash
git clone https://github.com/sametbilir33/Music.git
cd Music
```

Configure the project:

```bash
cmake -S . -B build
```

Build:

```bash
cmake --build build
```

Run:

```bash
./build/Music
```

## Installation

Install the application system-wide:

```bash
sudo cmake --install build
```

## Configuration

Application settings are stored in:

```text
~/.config/Music/config.ini
```

The configuration file stores application preferences and playlist information.

## License

This project is licensed under the **GNU General Public License v3.0**.

See the [LICENSE](LICENSE) file for details.
