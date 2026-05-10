# OBS WebM Reaction Plugin

Joue une vidéo WebM (ou tout autre format supporté par ffmpeg_source) quand le son est
en dessous du seuil, et une autre quand le son est au-dessus.  
Inspiré de [obs-image-reaction](https://github.com/scaledteam/obs-image-reaction).

## Fonctionnement

| État         | Vidéo jouée         |
|-------------|---------------------|
| Silencieux  | **Vidéo silencieux** (ex. avatar idle, boucle repos) |
| Parle / fort | **Vidéo loud** (ex. avatar qui parle, animation) |

Le plugin crée deux sources **ffmpeg_source** internes et bascule entre elles en lisant
le volume de n'importe quelle source audio OBS.

## Formats supportés

Tout ce que `ffmpeg_source` d'OBS accepte : `.webm`, `.mp4`, `.mov`, `.gif`, `.mkv`, `.avi`, etc.

## Build — Linux

### Prérequis
- OBS Studio ≥ 30 (headers + libobs)
- CMake ≥ 3.16
- GCC ou Clang

```bash
# Installer les headers OBS (Ubuntu / Debian)
sudo apt install obs-studio libobs-dev

# Cloner et compiler
git clone https://github.com/yourname/obs-webm-reaction
cd obs-webm-reaction
mkdir build && cd build
cmake ..
make -j$(nproc)

# Installer dans le répertoire plugin utilisateur
make install-user
```

OBS détectera automatiquement le plugin au prochain lancement.

### Chemin d'installation manuel (Linux)
```
~/.config/obs-studio/plugins/webm-reaction/bin/64bit/webm-reaction.so
~/.config/obs-studio/plugins/webm-reaction/data/locale/en-US.ini
```

## Build — Windows (depuis Linux avec MinGW)

```bash
cmake .. \
  -DCMAKE_SYSTEM_NAME=Windows \
  -DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc-win32 \
  -DLIBOBS_INCLUDE_DIR=~/obs-studio/libobs \
  -DLIBOBS_LIB=~/.wine/drive_c/Program\ Files/obs-studio/bin/64bit/obs.dll
make
```

Copier `webm-reaction.dll` dans `C:\Program Files\obs-studio\obs-plugins\64bit\`  
et le dossier `data\` dans `C:\Program Files\obs-studio\data\obs-plugins\webm-reaction\`.

## Build — macOS

Utiliser le SDK OBS CMake (voir [obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate)) :

```bash
brew install cmake obs-studio
mkdir build && cd build
cmake .. -DCMAKE_PREFIX_PATH="$(brew --prefix obs-studio)"
make -j$(sysctl -n hw.ncpu)
```

## Utilisation dans OBS

1. Ajouter une source → **Réaction WebM** (WebM Reaction)
2. Sélectionner la vidéo à jouer quand c'est **silencieux**
3. Sélectionner la vidéo à jouer quand c'est **fort**
4. Choisir la **source audio** à écouter (micro, capture son, etc.)
5. Ajuster le **Seuil** (dB) et le **Lissage**

### Options
| Paramètre | Description |
|-----------|-------------|
| Seuil | Volume minimum (dB) pour déclencher la vidéo "fort". Par défaut -40 dB. |
| Lissage | Inertie de la détection. Plus la valeur est haute, plus la réaction est lente mais stable. |
| Redémarrer | Si activé, la vidéo redémarre depuis le début à chaque changement d'état. |

## Licence

GPL-2.0 — même licence qu'OBS Studio.
