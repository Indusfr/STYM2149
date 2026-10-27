# STYM2149

*[English version](README.md)*

Instrument logiciel (VST3, CLAP, LV2 et Standalone ; AU et AAX à venir) qui émule la puce sonore YM2149F de l'Atari ST, avec une interface GEM de l'Atari.

Programming: Pascal "Indus" Nowak (with Claude's help) — Copyright © 2026 Indus / PRISM. Licence : AGPLv3.

- Jusqu'à 16 pistes : une puce à 3 voix, deux puces (2 × 3 voix) ou 16 pistes ; horloges de l'Atari ST, du ZX Spectrum et de l'Amstrad CPC ; chaînes de sortie Raw, ST et STE.
- Effets de timer de l'Atari ST (SID, Sync Buzzer, Digidrums), buzzer accordé, son DMA du STE, ADSR, vibrato, un LFO par piste, arpèges synchronisés au tempo, glissés, un écho des notes dans tous les modes et un delay en 16 voies.
- Séquenceur pas à pas de 16 pistes avec Song, mixeur, 32 instruments et séquences d'instrument, MIDI learn.
- 483 paramètres d'hôte, à identifiants stables ; motifs, morceaux et réglages enregistrés avec le projet.
- Fichiers : lecteur YM et SNDH, import et export MIDI, export WAV et stems, export VBL, YM3, YM6 et SNDH pour un vrai ST ou STE, import XM.
- Aucun accès réseau, aucune télémétrie : STYM2149 ne se connecte jamais à rien.

Le [manuel d'utilisation](docs/MANUAL.fr.md) explique l'installation et chaque fenêtre.

## Installer

La version 0.9.0 est la première version publique, pour Windows et Linux (x64). macOS et l'AAX arrivent avec la 1.0.0.

### Windows 10 ou 11 (x64)

Lancez `STYM2149-0.9.0-windows-x64-setup.exe`. Il installe pour tous les utilisateurs et demande des droits d'administrateur :

| Composant | Dossier |
|---|---|
| VST3 | `C:\Program Files\Common Files\VST3\STYM2149.vst3` |
| CLAP | `C:\Program Files\Common Files\CLAP\STYM2149.clap` |
| Application autonome | `C:\Program Files\PRISM\STYM2149` (modifiable pendant l'installation) |

Ce sont les dossiers de plugins standards, que les DAW parcourent par défaut ; si STYM2149 n'apparaît pas, relancez la recherche des plugins.

L'installateur n'est pas signé numériquement. Windows SmartScreen peut afficher « Windows a protégé votre ordinateur » : cliquez sur **Informations complémentaires**, puis sur **Exécuter quand même**. Désinstallez depuis Paramètres > Applications.

### Linux (x86_64)

```sh
tar xzf STYM2149-0.9.0-linux-x86_64.tar.gz
cd STYM2149-0.9.0-linux-x86_64
./install.sh               # copie dans ~/.vst3, ~/.lv2, ~/.clap et ~/.local/bin
./install.sh --uninstall   # les supprime
```

## Construire et tester

Prérequis : CMake 3.22 ou ultérieur, un compilateur C++20 (GCC 11 ou ultérieur, Clang 16, MSVC 2022 ou Xcode 15), Git. Ninja pour le preset `linux`. Cargo seulement pour l'oracle ym2149-rs.

```sh
tools/fetch_refdata.sh                    # morceaux de référence (ym2149-rs, commit épinglé) dans refdata/
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release [-DSTYM_ORACLE_CRATE=ON]
cmake --build build --parallel
ctest --test-dir build --output-on-failure   # sous Windows : ajouter -C Release
```

Avec les presets : `cmake --preset linux` (Ninja, Release, dossier `build-linux`) ou `cmake --preset windows` (Visual Studio 2022 x64, dossier `build-windows`), puis `cmake --build --preset linux` ou `windows`.

| Test CTest | Contenu |
|---|---|
| `unit` | Puce, chaînes de sortie, timers du MFP, moteur de voix et séquenceur, motifs, Song et annulation, parité avec le prototype, cœur du plugin, tous les formats de fichier (YM, LHA, JSON, MIDI, WAV, ZIP, YM3, YM6, SNDH, XM) |
| `ui` | Interface GEM : texte et polices, alertes, menus, infobulles, fenêtres flottantes, toutes les fenêtres de l'éditeur ; non-régression visuelle des scènes du prototype (moins de 1 % de pixels différents par scène) et des scènes de STYM2149 (aucun pixel différent) |
| `realtime` | Aucune allocation pendant le rendu (`-DSTYM_RTSAN=ON` avec Clang 20 : RealtimeSanitizer) |
| `oracle` | Puce et effets de timer contre AtariAudio 1.26, LHA contre le décompresseur d'AtariAudio |
| `oracle_s9` | Chaîne HQ contre le lecteur YM d'AtariAudio |
| `oracle_crate` | Contre le crate ym2149-rs 0.9.1 (`-DSTYM_ORACLE_CRATE=ON`) |
| `plugin` | Processeur et éditeur JUCE, traductions des infobulles, test de charge (`-DSTYM_BUILD_PLUGIN=ON`) |

Sans `refdata/`, les tests sur des morceaux sont ignorés, sauf si `STYM_REQUIRE_REFDATA=1` est défini (comme en CI).

### Plugin

```sh
cmake -S . -B build-plugin -DCMAKE_BUILD_TYPE=Release -DSTYM_BUILD_PLUGIN=ON
cmake --build build-plugin --parallel --target STYM2149_All STYM2149_CLAP stym_plugin_tests
ctest --test-dir build-plugin --output-on-failure -R '^plugin$'   # Linux sans écran : xvfb-run -a ctest ...
```

CMake télécharge JUCE 8.0.15 et clap-juce-extensions à des commits vérifiés. Formats : VST3, LV2, CLAP et Standalone sous Linux ; VST3, CLAP et Standalone sous Windows (l'AAX est compilé mais pas encore livré) ; AU, VST3, CLAP et Standalone sous macOS. Fichiers produits dans `build-plugin/src/plugin/STYM2149_artefacts/Release/`. Sous Linux, JUCE demande les paquets de développement d'ALSA, JACK, FreeType, Fontconfig et X11 (liste dans `.github/workflows/ci.yml`) ; sans écran, les tests de l'éditeur demandent Xvfb.

Paquets : `packaging/linux/make_tarball.sh` construit l'archive Linux ; `packaging/windows/STYM2149.iss` est le script Inno Setup 6 de l'installateur Windows.

### Outils

- `ym2wav` : fait le rendu d'un fichier YM (compressé LHA ou non) en WAV.
- `ymbench` : charge CPU du moteur de voix (`--seq --notes 6 --noise` pour le pire cas, `--hash` pour une empreinte de la sortie).
- `gemshot` : fait le rendu de l'interface en PNG, sans hôte ni JUCE (tests visuels).
- `tools/proto_capture`, `tools/proto_golden`, `tools/gemfont` : captures de référence, données de parité et polices GEM tirées du prototype et d'EmuTOS.

## Documentation

- [Manuel d'utilisation](docs/MANUAL.fr.md) ([English](docs/MANUAL.md))
- [Guide d'architecture](docs/ARCHITECTURE.md)
- [Journal des modifications](CHANGELOG.md)

## Licence

GNU Affero General Public License v3 (voir [LICENSE](LICENSE)). L'émulation s'appuie sur AtariAudio de Leonard/Oxygene (Arnaud Carré) et sur ym2149-rs de slippyex, tous deux sous licence MIT ; les fichiers SNDH sont joués par la machine Atari d'AtariAudio avec l'émulateur 68000 Musashi de Karl Stenerud (MIT) ; la décompression LHA est portée de pylzh de Hirofumi Ukawa (Apache-2.0) ; les polices GEM et les icônes du bureau viennent d'EmuTOS (GPL) ; le plugin utilise JUCE 8 sous AGPLv3. Chaque composant et sa licence : [THIRD_PARTY.md](THIRD_PARTY.md) ; textes des licences dans [third_party/licenses](third_party/licenses).
