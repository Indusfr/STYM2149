# Manuel de STYM2149

Version 0.9.0. Programming: Pascal "Indus" Nowak (with Claude's help) — Copyright © 2026 Indus / PRISM. Licence : AGPLv3.

STYM2149 est un instrument logiciel qui émule la puce sonore YM2149F de l'Atari ST (et le son DMA de l'Atari STE), avec une interface dessinée dans le style du bureau GEM de l'Atari. C'est avant tout un plugin : vous le chargez sur une piste d'instrument de votre DAW, vous en jouez en MIDI et vous pilotez son séquenceur pas à pas intégré depuis le transport du DAW. Une application autonome est aussi fournie.

La version 0.9.0 est livrée pour Windows (VST3, CLAP, Standalone) et Linux (VST3, CLAP, LV2, Standalone).

Les remarques de ce manuel marquées **À CONFIRMER** décrivent des points qui n'ont pas encore été vérifiés.

## Sommaire

1. [Installation](#1-installation)
2. [Premiers pas](#2-premiers-pas)
3. [La fenêtre principale](#3-la-fenêtre-principale)
4. [Fenêtres flottantes](#4-fenêtres-flottantes)
5. [Le séquenceur](#5-le-séquenceur)
6. [Instruments, Patches, Drum Kit, Digidrums et DMA](#6-instruments-patches-drum-kit-digidrums-et-dma)
7. [MIDI](#7-midi)
8. [Fichiers](#8-fichiers)
9. [Préférences](#9-préférences)
10. [Raccourcis clavier et gestes de la souris](#10-raccourcis-clavier-et-gestes-de-la-souris)
11. [Confidentialité](#11-confidentialité)
12. [Dépannage](#12-dépannage)
13. [Licences et crédits](#13-licences-et-crédits)

---

## 1. Installation

### 1.1 Windows 10 ou 11 (x64)

Lancez `STYM2149-0.9.0-windows-x64-setup.exe`. L'installateur installe pour tous les utilisateurs et demande des droits d'administrateur. Il parle anglais, français, allemand, espagnol, portugais (Brésil) et italien, affiche la licence (AGPLv3) et vous laisse choisir les composants :

| Composant | Installé dans |
|---|---|
| Plugin VST3 | `C:\Program Files\Common Files\VST3\STYM2149.vst3` |
| Plugin CLAP | `C:\Program Files\Common Files\CLAP\STYM2149.clap` |
| Application autonome | `C:\Program Files\PRISM\STYM2149\STYM2149.exe` (le dossier peut être changé pendant l'installation ; une mise à jour supprime l'ancien `C:\Program Files\Indus`) |

Le dossier de l'application reçoit aussi `LICENSE.txt`, `README.md`, `README.fr.md`, `THIRD_PARTY.md`, ce manuel en anglais et en français (dossier `docs`) et un dossier `licenses`. L'installateur ajoute un groupe au menu Démarrer (STYM2149 et son programme de désinstallation) ; une icône sur le bureau est facultative. Installer une nouvelle version remplace en entier le bundle VST3 précédent, et retire ce que l'installateur par utilisateur des versions de test précédentes avait laissé : ses copies du plugin dans `%LOCALAPPDATA%\Programs\Common`, son application dans `%LOCALAPPDATA%\Programs\STYM2149`, son groupe du menu Démarrer et son icône sur le bureau.

**Quel build est installé.** Chaque build a un numéro, celui du passage de la CI qui l'a compilé : Fuji > Information l'affiche après la version, avec son commit (par exemple `0.9.0 build 412 (fe71c0d)`), et le nom de l'installateur le porte (`STYM2149-0.9.0-build412-windows-x64-setup.exe`). Un build compilé sur votre propre ordinateur affiche `build dev`.

**Pour que votre DAW trouve le plugin.** `C:\Program Files\Common Files\VST3` et `…\CLAP` sont les dossiers de plugins standards des spécifications VST3 et CLAP, que les DAW parcourent par défaut. Si STYM2149 n'apparaît pas, relancez la recherche des plugins ; s'il n'apparaît toujours pas, vérifiez que les réglages des dossiers de plugins de votre DAW comprennent ces dossiers.

| DAW (Windows) | Format à utiliser | Remarques |
|---|---|---|
| FL Studio | VST3 (CLAP : **À CONFIRMER**) | |
| Ableton Live 11 | VST3 | Live 11 ne prend pas en charge CLAP |
| Cubase | VST3 | |
| ACID Pro | VST3 (**À CONFIRMER**) | |
| Maschine | VST3 (**À CONFIRMER**) | STYM2149 n'a pas de version VST2 |
| Reason | VST3 (**À CONFIRMER**) | Pas de version Rack Extension |

Dans la liste des plugins, STYM2149 apparaît comme un instrument du fabricant **PRISM**.

**Windows SmartScreen.** L'installateur n'est pas signé numériquement. Windows peut afficher « Windows a protégé votre ordinateur ». Cliquez sur **Informations complémentaires**, puis sur **Exécuter quand même**. Cet avertissement est normal pour un logiciel open source non signé.

**Désinstallez** depuis Paramètres > Applications (STYM2149), ou avec « Désinstaller STYM2149 » dans le menu Démarrer. Le programme de désinstallation supprime les fichiers installés ; vos réglages et vos bibliothèques (section 9) ne font pas partie de l'installation et restent en place.

### 1.2 Linux (x86_64)

```sh
tar xzf STYM2149-0.9.0-linux-x86_64.tar.gz
cd STYM2149-0.9.0-linux-x86_64
./install.sh
```

`install.sh` ne demande pas d'accès root et ne touche à rien en dehors de votre dossier personnel :

| Composant | Installé dans |
|---|---|
| VST3 | `~/.vst3/STYM2149.vst3` |
| LV2 | `~/.lv2/STYM2149.lv2` |
| CLAP | `~/.clap/STYM2149.clap` |
| Standalone | `~/.local/bin/STYM2149` |
| Entrée de menu et icône | `~/.local/share/applications/stym2149.desktop`, `~/.local/share/icons/hicolor/*/apps/stym2149.png` |

Extrayez d'abord toute l'archive : le script s'arrête s'il manque un fichier à côté de lui. Si `~/.local/bin` n'est pas dans votre `PATH`, le script le signale ; lancez l'application avec son chemin complet. Laissez ensuite votre hôte relancer la recherche de ses plugins.

`./install.sh --uninstall` supprime ce que `install.sh` a copié. Votre fichier de réglages reste en place.

L'archive contient aussi `README.md`, `README.fr.md`, `LICENSE`, `THIRD_PARTY.md`, ce manuel en anglais et en français (dossier `docs`) et un dossier `licenses`.

Bibliothèques d'exécution : les plugins et l'application utilisent ALSA, FreeType, Fontconfig et les bibliothèques d'exécution C et C++, et ont besoin d'un affichage X11 pour la fenêtre. La distribution et la version de glibc minimales sont **À CONFIRMER**.

### 1.3 macOS et AAX

macOS (AU, VST3, CLAP, Standalone) et l'AAX ne font pas partie de la version 0.9.0. Ils sont prévus pour la version 1.0.0.

---

## 2. Premiers pas

### 2.1 Premier son

1. Insérez STYM2149 comme instrument sur une piste MIDI ou une piste d'instrument.
2. Ouvrez sa fenêtre. Une nouvelle instance démarre en mode **16 ch**, avec la piste 1 activée et les autres désactivées, générateur Chip Voice, onde carrée, volume 13.
3. Jouez des notes depuis votre clavier MIDI ou le piano roll du DAW. Par défaut (routage MIDI **Omni**), les notes reçues jouent la piste courante, c'est-à-dire la piste 1.
4. Vous pouvez aussi cliquer sur le clavier à l'écran, en bas de la fenêtre.

Les notes hors de la plage de la puce (B0 à C9) ne jouent pas sur les pistes mélodiques.

### 2.2 Le motif de démonstration

File > Demo pattern écrit un court motif dans le motif courant : une basse arpégée sur la piste 1, un lead SID sur la piste 2, un rythme Chipdrum sur la piste 3 (voix C en Authentic 3ch) et des stabs Sync Buzzer sur la piste 4 (16 ch seulement). Si les pistes 1 à 4 contiennent déjà des données, STYM2149 demande confirmation avant de les écraser.

Dans un DAW, le séquenceur suit le transport de l'hôte : lancez la lecture dans votre DAW pour entendre le motif (section 5.9). Dans l'application autonome, appuyez sur Space ou sur le bouton Play du Step Sequencer.

### 2.3 Modes de voix

Le sélecteur **Voices** du cadre SYSTEM (aussi un paramètre d'hôte) choisit comment les pistes sont réparties sur les puces :

| Mode | Pistes | Usage typique |
|---|---|---|
| **16 ch** | 16 pistes, chacune avec son propre panoramique et sa propre polyphonie (jusqu'à 6 voix par piste) | Production ; son YM sans la limite de 3 voix |
| **Authentic 3ch** | Une puce YM : voix A, B, C plus une piste Digidrum (pistes 1 à 4) ; sortie mono | Musique qu'un vrai Atari ST peut jouer ; obligatoire pour les exports de registres (VBL, YM3, YM6, SNDH) |
| **Dual chip 2x3ch** | Deux puces : A1, B1, C1, Digi1, A2, B2, C2, Digi2 (pistes 1 à 8) | Deux puces YM |

Le réglage **Computer** (ST ou STE) ajoute le son DMA du STE : avec STE, Authentic 3ch gagne huit pistes DMA (pistes 5 à 12), Dual chip gagne huit pistes DMA (9 à 16), et en 16 ch toute piste peut utiliser le générateur DMA.

Changer le mode de voix ou l'ordinateur arrête toutes les notes.

### 2.4 L'application autonome

L'application autonome fonctionne comme le plugin. Différences :

- Options > Audio/MIDI settings ouvre les réglages des périphériques audio et MIDI.
- L'indicateur MIDI de la barre de menus affiche « no inputs », le nom de l'entrée ou « n inputs ».
- Le séquenceur tourne sur sa propre horloge (Free run) : Space, ou le bouton Play, le lance et l'arrête, et le curseur BPM règle sa vitesse.
- File > Quit (Ctrl+Q) ferme l'application après une confirmation ; la case de fermeture de la fenêtre du système (et Alt+F4 sous Windows) pose la même question. Quitter depuis le menu de l'application sous macOS, ou à la fin de la session, enregistre et quitte sans demander.

### 2.5 Enregistrer votre travail

- **Dans un DAW**, tout ce qui fait le son est enregistré dans le projet du DAW : paramètres, motifs, morceau, instruments, séquences, Drum Kit, échantillons Digidrum et DMA, noms des pistes, dernière prise de l'enregistreur.
- **Non enregistrés dans le projet** : positions des fenêtres, outil actif, historique d'annulation et presse-papiers. Ils durent aussi longtemps que l'instance du plugin (fermer et rouvrir la fenêtre du plugin les conserve). Les préférences (taille de la fenêtre, infobulles, table MIDI learn, etc.) sont propres à l'utilisateur et partagées par toutes les instances (section 9).
- **Nouveau** : File > New (Ctrl+N) repart d'un projet neuf : tous les paramètres, motifs, instruments, échantillons et la prise de l'enregistreur reviennent à leurs valeurs par défaut. Si quelque chose est déjà défini, STYM2149 demande confirmation. Les préférences et les positions des fenêtres restent.
- **Fichiers** : le menu File enregistre et charge des presets, des motifs, des morceaux et des kits de batterie sous forme de fichiers JSON, et importe et exporte d'autres formats (section 8).

---

## 3. La fenêtre principale

### 3.1 Le bureau GEM

La fenêtre est un bureau GEM de 1 344 × 984 pixels logiques, dessiné à la résolution de votre écran : à 100 % il mesure 840 × 615 pixels (les 5/8 du bureau), à 160 % 1 344 × 984, à 200 % 1 680 × 1 230, multipliés par l'échelle d'affichage du système. Quand un pixel du bureau tombe sur un nombre entier de pixels de l'écran, le dessin est exact au pixel près. Sinon le réglage Pixels (Options > Settings, section 3.12) décide : **Sharp** place chaque pixel des lettres et des icônes sur des pixels entiers de l'écran, net mais certains un peu plus larges que d'autres ; **Smooth** (par défaut) les dessine avec anticrénelage, comme un navigateur montre le prototype. Si les cadres et les traits sont flous eux aussi, pas seulement les lettres, c'est l'hôte qui étire la fenêtre du plugin : cherchez dans ses réglages la mise à l'échelle de l'affichage des plugins. Pour changer la taille, redimensionnez la fenêtre du plugin dans l'hôte (ou la fenêtre de l'application autonome), de 50 à 200 % ; les proportions restent fixes et la taille atteinte est mémorisée. Il n'y a pas de menu de zoom. Si vous n'avez jamais redimensionné la fenêtre, STYM2149 choisit une taille d'après votre écran principal (50 %, 75 %, 100 % ou 125 %).

La barre de menus court en haut du bureau, comme sur un bureau GEM (section 3.2) ; la fenêtre principale commence sous elle, et aucune fenêtre ne passe au-dessus. Les icônes des disquettes A et B se trouvent côte à côte en haut à gauche du bureau, celle de la corbeille en bas à droite, nommées dans la langue des infobulles et avec les images du bureau d'EmuTOS : une disquette 3,5 pouces portant la lettre du lecteur sur son étiquette, une poubelle. Un clic sur la corbeille est File > New, avec sa confirmation ; A et B sont seulement décoratives, un clic ou un double-clic sur elles ne fait rien. Maintenez le bouton sur une icône pour faire glisser son contour pointillé ailleurs : comme sur un bureau GEM, il saute de case en case d'une grille invisible de la taille des icônes, et une icône ne se pose pas sur la case d'une autre (sa place est une préférence, section 9).

Les fenêtres flottantes s'ouvrent par-dessus la fenêtre principale. Faites glisser une fenêtre par sa barre de titre (la fenêtre principale aussi, qui garde sa place tant que l'instance du plugin vit) ; aucune fenêtre ne dépasse les bords du bureau, et celle qui grandit près de son bord droit ou bas, ou s'ouvre à une place gardée sur un bureau plus grand, revient à l'intérieur ; fermez-la avec la case de sa barre de titre ou avec Esc (Esc ferme la fenêtre au premier plan, sauf les fenêtres Computer Keyboard et Slide Curve). La plupart des fenêtres affichent le nom de la piste courante dans leur titre et suivent la piste courante. **Tab** passe la fenêtre du fond au premier plan, de sorte que les fenêtres ouvertes viennent devant tour à tour (la fenêtre principale prend son tour) ; Shift+Tab va dans l'autre sens. Un menu ouvert depuis un bouton se pose sur le filet du bas du bouton, sans espace, comme les menus GEM.

Les infobulles apparaissent quand vous laissez le pointeur sur une commande. Les réglages Tooltips et Language d'Options > Settings les activent ou les désactivent et choisissent leur langue (sections 3.12 et 9). Les menus et les libellés sont toujours en anglais.

Les curseurs se font glisser à la souris. Vous pouvez cliquer dans la valeur à côté de chaque curseur et y taper une valeur (des soulignés marquent ses positions libres) ; appuyez sur Enter pour l'appliquer, sur Esc pour la laisser telle quelle. Une valeur hors limites est ramenée au bout du curseur. Une note (note de base, note fixe des séquences) se tape par son nom, dans l'une ou l'autre notation (`C#4`, `Sol3`), ou par son numéro de note MIDI. Un curseur horizontal (sauf les panoramiques du Mixer) a une flèche à chaque bout : un clic change la valeur d'un pas, et recommence tant que vous maintenez le bouton.

Les boîtes de dialogue gardent le clavier tant qu'elles sont ouvertes. Une confirmation avant une action irréversible (Clear, Demo pattern, New, Quit, suppression d'une prise ou d'une banque) affiche d'abord **Cancel**, encadré comme choix par défaut : Enter et Esc annulent tous les deux, quel que soit le bouton qui a le focus, et seul un clic sur l'action confirme. Dans une boîte où vous tapez un nom ou un nombre, Enter vaut OK et Esc vaut Cancel. Tab et Shift+Tab passent d'un bouton à l'autre (et au champ de texte).

### 3.2 Barre de menus

| Menu | Entrées |
|---|---|
| Menu Atari (Fuji) | **Information** (icône, version, numéro de build et crédits ; Entrée la ferme) ; **Shortcuts** (^?) : la fenêtre SHORTCUTS, une liste déroulante de tous les raccourcis |
| **File** | New (^N) ; Load, Save, Save as (chacun : Preset, Pattern, Song, Drum kit) ; Import (YM dump, MIDI file, XM module) ; Export pattern (MIDI, WAV, Stems (ZIP)) ; Export song (VBL dump, YM3, YM6, SNDH, MIDI, WAV, Stems (ZIP)) ; Recorder ; Demo pattern ; Quit (^Q, application autonome seulement) |
| **Edit** | Step Sequencer (^E), Arpeggiator (^B), Mixer (^M), Song (^G), Modulation (^R), Effects (^F), Patches (^P), Drum kit (^K) ; Cut (^X), Copy (^C), Paste (^V) ; Undo (^Z), Redo (^Y) ; Instruments (^T), Samples (Digidrums, DMA (STE)) |
| **Options** | Audio/MIDI settings (application autonome seulement) ; MIDI learn / Stop MIDI learn ; Clear MIDI map ; Oscilloscope (On, Off) ; Register monitor (On, Off) ; Note monitor (On, Off) ; Settings... (la fenêtre SETTINGS, section 3.12) |

Les entrées grisées expliquent pourquoi dans leur infobulle. Arpeggiator et Modulation sont grisés sur les pistes de percussion (Digidrum, Chipdrum). Drum kit (et Ctrl+K) ne sert qu'aux pistes Chipdrum. Samples > DMA (STE) est grisé avec Computer ST. L'onde SID et les pas du Sync Buzzer se règlent dans le cadre GENERATOR (section 3.8). Les exports de registres (VBL dump, YM3, YM6, SNDH) sont grisés sauf si le mode de voix est Authentic 3ch. Edit > Cut et Copy agissent sur la sélection du séquenceur, ou sur la note sous le curseur du séquenceur.

Deux indicateurs se trouvent au bout droit de la barre de menus :

- **CHIP: OK** ou **CHIP: n** — conformité à la puce du motif courant. Cliquez dessus pour lister ce que les exports VBL, YM3, YM6 ou SNDH ne reproduiraient pas tel que vous l'entendez (section 8.4).
- **MIDI: host** dans un plugin (l'hôte envoie le MIDI) ; dans l'application autonome, l'état des entrées MIDI.

### 3.3 Onglets des pistes

Seize onglets sélectionnent la **piste courante**, celle qu'éditent les panneaux du dessous et que joue le clavier à l'écran. Les panneaux forment quatre colonnes : SOURCE et VIBRATO ; ENVELOPE, ADSR et ADSR ENVELOPE ; GENERATOR ; SYSTEM.

- Cliquez sur un onglet pour rendre sa piste courante. Cliquez à nouveau sur l'onglet de la piste courante pour activer ou désactiver la piste. Une piste désactivée ne sonne pas.
- **All on** et **All off** activent ou désactivent toutes les pistes.
- Clic droit sur un onglet : **Copy settings**, **Paste settings** (le son, l'arpège et le niveau de la piste ; pas On, Pan, Mute ni Solo), **Rename...** (8 caractères ; un nom vide affiche le numéro de la piste). En Authentic 3ch et en Dual chip, les voix de la puce sont nommées d'après la puce (A, B, C, Digi…) et ne peuvent pas être renommées.
- En Authentic 3ch, les onglets qui suivent les quatre pistes de la puce sont inutilisés (ou sont les pistes DMA, DMA1 à DMA8, avec Computer STE).
- ← et → rendent courante la piste précédente ou suivante (section 10).

### 3.4 SOURCE (per channel)

| Commande | Valeurs | Effet |
|---|---|---|
| Source | Tone, Noise, Tone+Noise, Env, Tone+Env, Noise+Env, Tone+Noise+Env | Onde carrée, bruit, les deux ou aucun, avec le volume 4 bits ou l'enveloppe matérielle : une entrée avec « Env » règle aussi Mode sur Envelope, les autres sur Vol 4-bit. **Env** seul joue la forme d'onde de l'enveloppe elle-même, à sa fréquence (cadre ENVELOPE), sans suivre la note ; pour un buzzer qui suit la note, prenez le générateur Sync Buzzer. En Authentic 3ch, ce sont les réglages mêmes de la puce (R7 et bit 4 de R8 à R10) ; la puce n'a qu'une enveloppe, donc les pistes en Env partagent sa forme et sa période |
| Voicing | Polyphonic, Monophonic | Polyphonic : accords jusqu'à 6 voix ; une note poursuit son release pendant que les suivantes jouent, au clavier et à l'arpégiateur (4.2) comme dans le Step Sequencer (6 voix par piste, le release le plus ancien cédant la place). Monophonic : une note à la fois ; une nouvelle touche tenue glisse vers la nouvelle note au lieu de la redéclencher. Indisponible en Authentic 3ch (une voix par canal) |
| Noise period | 1 à 31 | Une valeur basse est brillante, une valeur haute gronde. En Authentic 3ch, la puce n'a qu'un générateur de bruit : la valeur s'applique à toutes les pistes |
| Volume | 0 à 15 | Le volume 4 bits de la puce |
| Mode | Vol 4-bit, Envelope | Volume 4 bits fixe, ou volume piloté par l'enveloppe matérielle ; Source suit (Tone devient Tone+Env, etc.). Vol 4-bit sur Env seul rallume aussi l'onde carrée |
| Level | 0 à 100 | Niveau du canal dans le mixage (en Authentic 3ch, le niveau vient du volume 4 bits) |

Sur une piste DMA, Digidrum ou Chipdrum, Source, Noise period, Volume et Mode sont grisés, ainsi que tout le cadre ENVELOPE : ces pistes jouent leur propre son (un sample, ou les trames du Drum Kit), sans l'onde carrée, le bruit, le volume fixe ni l'enveloppe matérielle de ce cadre. Voicing et Level agissent toujours.

### 3.5 ENVELOPE (per channel ; « shared » en Authentic 3ch)

Le YM2149 n'a qu'une enveloppe matérielle. En Authentic 3ch, elle est partagée par les trois voix.

| Commande | Effet |
|---|---|
| Env freq | Fréquence de répétition de l'enveloppe. Le curseur est géométrique et occupe toute sa ligne ; sa valeur, en Hz, est au bout de la ligne de EP = |
| EP = | Le registre de période d'enveloppe (R11–R12), 1 à 65 535. Tapez un nombre décimal ou un nombre hexadécimal précédé de `$` (par défaut 12 800, `$3200`) |
| Shape R13 | Les formes `$8` à `$F` du registre R13 (`$0` à `$3` sonnent comme `$9`, `$4` à `$7` comme `$F` ; une valeur plus basse venue d'un import YM ou d'un CC MIDI est gardée, et le menu coche la forme jouée). Utilisé seulement quand Mode vaut Envelope, ou avec le générateur Sync Buzzer |

Quand **Tuning** est activé dans le cadre GENERATOR d'une piste Sync Buzzer, le cadre affiche « tuned xN/D » et la période d'enveloppe suit la note ; Env freq, EP = et leurs valeurs sont alors grisés (section 3.8). L'accord n'agit que sur un Sync Buzzer : sur une piste Chip Voice, l'enveloppe joue EP.

### 3.6 VIBRATO (per channel)

**Vibrato** On ou Off (On par défaut ; paramètre de l'hôte, automatisable) : Off coupe le vibrato de la piste et grise Depth et Rate. La ligne VIB du séquenceur (`Vxx`) le coupe et le rétablit à partir d'une ligne du motif (section 5.5). Depth de 0 à 200 cents ; Rate de 1 à 20 Hz (par pas de 0,5 Hz, 6 Hz par défaut). La molette de modulation MIDI ajoute aussi de la profondeur de vibrato, même avec Vibrato sur Off (section 7.2). Les pistes de percussion (Chipdrum, Digidrum) ne jouent pas de vibrato : le cadre est grisé.

### 3.7 ADSR (per channel)

| Commande | Mode Smooth | Mode Chip |
|---|---|---|
| A (Attack, attaque) | 0 à 2 000 ms | Ticks VBL (0 à 100) |
| D (Decay, déclin) | 0 à 2 000 ms | Ticks VBL (0 à 100) |
| S (Sustain, maintien) | 0 à 100 % | Niveau 4 bits (0 à 15) |
| R (Release, relâchement) | 0 à 2 000 ms | Ticks VBL (0 à 100) |

À 50 Hz, les 100 ticks de A, D et R en mode Chip sont les 2 000 ms de Smooth.

Les curseurs affichent les initiales ; leurs infobulles nomment chaque réglage (dans une infobulle traduite, suivi de son nom anglais entre parenthèses : « Attaque (Attack) »).

**Mode** : Smooth donne une courbe continue (un gain audio) ; Chip écrit des paliers de volume 4 bits au rythme VBL, comme le font les players de l'Atari. En Authentic 3ch, Chip est imposé (sauf sur les pistes DMA). Avec le générateur Sync Buzzer en mode Chip, le niveau vient de l'enveloppe matérielle, et les pistes de percussion jouent chaque coup jusqu'au bout : l'ADSR n'y a aucun effet, et les cadres ADSR et ADSR ENVELOPE sont grisés. Le cadre **ADSR ENVELOPE** dessine la courbe et montre un point pour chaque note qui sonne sur la piste : en Polyphonic, une note encore dans son release garde son point pendant que les suivantes ont le leur. En Monophonic, et sur toutes les pistes des modes 3 canaux, il n'y a qu'un point, celui de la note la plus récente. Une note relâchée avant son sustain descend du niveau atteint : son point continue sur la ligne du decay jusqu'au niveau du sustain, puis suit la ligne du release. Avec un sustain à 0 %, il descend la ligne du decay jusqu'au bout, où la note s'arrête.

### 3.8 GENERATOR (per channel)

**Type** choisit le générateur de son de la piste :

| Générateur | Touche | Ce qu'il fait |
|---|---|---|
| Chip Voice | Ctrl+V | La voix YM simple : carré, bruit, enveloppe |
| SID Voice | Ctrl+S | Effet de timer qui fait basculer le volume entre le volume de la note et un volume bas (son « SID » de l'Atari). **Timer ratio** 0,25 à 8,00 × (1 joue la note, 2 une octave au-dessus) ; **SID min vol** 0 à 15. En dessous, la **SID wave**, sa forme choisie dans le menu après son nom : **Base**, l'onde carrée simple, sans réglage : ses barres et Steps sont cachés, le menu seul dans leur cadre ; **Square**, un carré de Steps pas, le volume de la note sur les **Width** premiers (1 à Steps - 1, moitié-moitié par défaut), SID min vol sur le reste ; **Sine**, une sinusoïde de **Period** barres (2 à 16, 8 par défaut) répétée le long des 16 barres, aucune à 0 (une sinusoïde des volumes, tels que les barres les montrent ; le niveau de la puce, logarithmique, la rend plus brillante qu'une sinusoïde pure) ; **Bounce**, la même avec Period, mais une sinusoïde du niveau de la puce, qui sonne comme une sinusoïde pure et que les barres montrent comme un rebond, plat en haut et pointu en bas ; **Custom**, les barres dessinées à la souris. Les barres de Square montrent le carré joué et ne répondent pas à la souris. Chaque forme garde les réglages et les barres des autres. **Steps** (2 à 16, 8 par défaut), sous les barres, son libellé devant son curseur, règle le nombre de barres jouées, une par interruption du timer, mises à l'échelle par le volume de la note : le timer tourne Steps/2 fois plus vite, donc la hauteur ne change pas ; les barres au-delà restent grisées et ne sont pas jouées. Les Steps jouent une période de la note, donc la hauteur suit les cycles qu'ils contiennent : Period 8 avec 16 Steps sonne une octave au-dessus, 8 Steps à la hauteur de la note. Juste sous Steps, à la même place, Period avec Sine ou Bounce et Width avec Square, les trois curseurs alignés. Sine ou Bounce depuis Base règle autant de Steps que leur Period, un cycle qui joue la note ; Custom depuis Base ou Square, ses barres jamais dessinées, part du carré joué. Avec Sine ou Bounce, une barre déplacée décale la sinusoïde le long des barres jusqu'à la place qui amène cette barre au plus près du volume demandé, sans en changer les volumes (un cran de molette va au volume suivant que la sinusoïde peut donner à la barre) ; Custom libère les barres telles quelles. Faites glisser ou utilisez la molette de la souris sur les barres ; un clic droit ouvre le curseur de la barre (un clic droit sur le curseur la remet à 0) |
| Sync Buzzer | Ctrl+U | Effet de timer qui relance l'enveloppe matérielle à la fréquence de la note. Le choisir règle EP à 32 si EP était au-dessus de 3 200. **Tuning** On ou Off : la période d'enveloppe suit la note, et l'enveloppe joue la note × **N** / **D** (1 à 16 chacun) plus **Fine** (−100 à +100 cents). **YM Pad** : le son tourne à la période 0 sous l'enveloppe, environ −6 dB. **Presets** règlent la forme, l'accord et YM Pad en un clic, la piste restant un Sync Buzzer : Saw, Triangle, Third, Fifth, Seventh, Tenth, Tri fifth, Soft saw. En dessous, les **Sync Buzzer steps** : 0 à 8 formes d'enveloppe (R13) écrites tour à tour à chaque relance (**Steps** à 0 : la forme de la piste à chaque fois). Le timer tourne autant de fois plus vite qu'il y a de pas, pour qu'ils tiennent tous dans une période de la note et que la piste garde la hauteur de la note. Chaque bouton montre sa forme, ou « Trk » pour la forme de la piste : cliquez-le pour choisir une forme, faites un clic droit pour le remettre à la forme de la piste. Ces réglages n'agissent que tant que la piste joue un Sync Buzzer |
| Chipdrum | Ctrl+C | Percussions synthétisées : les huit instruments du Drum Kit (bouton **Drum Kit**, nom du kit affiché) |
| Digidrum | Ctrl+I | Échantillons 4 bits joués par un timer à travers une voix de la puce. **Slot** 1 à 8 ; **ST channel** A, B ou C (Authentic 3ch : la voix qu'emprunte la piste Digidrum) ; **Digidrum samples**, sous les emplacements 5 à 8, ouvre la fenêtre Digidrums pour charger et modifier les échantillons ; la forme d'onde de l'emplacement en dessous |
| DMA | Ctrl+D | Le son DMA du STE (Computer STE seulement). **Sample** 1 à 8 ; **DMA samples**, sous les échantillons 5 à 8, ouvre la fenêtre DMA ; la forme d'onde de l'échantillon en dessous |

En Authentic 3ch et en Dual chip, les rôles des pistes sont fixes : la quatrième piste de chaque puce ne joue que des Digidrums, les pistes DMA ne jouent que du DMA, et les voix de la puce ne peuvent pas être des pistes Digidrum. Les touches de générateur ne passent qu'à un type que la piste a le droit d'utiliser.

### 3.9 SYSTEM

| Commande | Effet |
|---|---|
| Voices | 16 ch, Authentic 3ch, Dual chip 2x3ch. Le changer arrête toutes les notes |
| Computer | Un menu : **ST** (le YM seul) ou **STE** (le YM plus le son DMA) |
| Clock | Horloge des arpégiateurs et du Step Sequencer : **Internal** (vitesse d'arpège en Hz ; le séquenceur suit l'hôte dans un DAW, son propre tempo dans l'application autonome) ou **MIDI ext** (les deux suivent l'horloge MIDI : pas d'arpège à la division choisie, séquenceur lancé par MIDI Start, voir 5.9). Avec MIDI ext, un état affiche « waiting for clock... », puis SYNC ou STOP |
| Master | Niveau de sortie, 0 à 100 (70 par défaut) |
| Voices (LED) | Une LED par voix, allumée tant qu'elle sonne |
| Note | Période de son et fréquence de la dernière note jouée (« TP=n -> f Hz »), tant qu'une voix sonne ; « - » dès que toutes les notes sont finies, release comprise, comme au lancement. Options > Note monitor > Off masque la ligne (une préférence) ; rien ne bouge |

### 3.10 Boutons de fenêtres, oscilloscope et moniteur de registres

Les boutons **Mixer** (Ctrl+M), **Arpeggiator** (Ctrl+B), **Modulation** (Ctrl+R), **Effects** (Ctrl+F), **Step Sequencer** (Ctrl+E) et **Patches** (Ctrl+P) ouvrent leurs fenêtres. La rangée de boutons est centrée dans la fenêtre.

L'**oscilloscope** affiche la forme d'onde de sortie ; Options > Oscilloscope > Off le masque (une préférence), et ce qui se trouve dessous remonte, la fenêtre raccourcissant de 100 pixels. Le **moniteur de registres** affiche les registres YM R0 à R13 en hexadécimal et, à droite, la piste courante et son générateur (CHIP VOICE, SID VOICE, SYNC BUZZER, DIGIDRUM, CHIPDRUM ou DMA ; ou « INS nn » quand un Program Change a sélectionné un instrument), avec « 3CH AUTH (shared env+noise) » ou « 16CH ». Pendant que le YM Player joue, il affiche « YM PLAYER ». Options > Register monitor > Off le masque (une préférence), comme l'oscilloscope : le clavier ou les pads et leurs boutons remontent, la fenêtre raccourcissant de 50 pixels.

### 3.11 Clavier à l'écran et pads

- Le piano joue la piste courante. Cliquez pour jouer ; faites glisser en gardant le bouton enfoncé pour passer à d'autres touches. Faites un clic droit pour choisir la piste que joue le clavier.
- Sur une piste de percussion (Chipdrum, Digidrum), le piano est remplacé par **8 pads**. Sur une piste Digidrum, un pad sélectionne aussi son emplacement.
- **Latch** : les touches restent tenues jusqu'à ce que vous appuyiez à nouveau dessus.
- **Layer** : joue toutes les pistes activées au lieu de la piste courante (règle le routage MIDI sur Layer ; le désactiver revient à Omni).
- **Oct -** / **Oct +** : octave du piano (0 à 6) ; les touches Z et X font de même.
- **Computer keyboard** : ouvre la fenêtre Computer Keyboard (section 4.6). Le clavier de votre ordinateur ne joue des notes (ou les pads, sur une piste de percussion) que tant que cette fenêtre est ouverte.
- La ligne d'aide sous le clavier rappelle les touches principales : Space = Play/Stop, 1-8 = Pattern, Shift+1-4 = Bank, ←→ = Channel, M = Metro, Ctrl+Z/Y = Undo/Redo, Esc = Panic.

### 3.12 Fenêtre Settings

Options > **Settings...** ouvre la fenêtre SETTINGS. Chaque réglage est un libellé et un bouton qui affiche sa valeur : cliquez sur le bouton pour la liste des valeurs, la valeur courante cochée. Une valeur qui ne s'applique pas est grisée, et son infobulle dit pourquoi. La fenêtre garde sa place, et elle est de nouveau ouverte à la prochaine ouverture de l'éditeur si vous l'avez laissée ouverte.

Un changement s'applique tout de suite, pour que vous l'entendiez. **OK** garde les changements et ferme la fenêtre. **Cancel**, le bouton par défaut (contour gras, pressé aussi par Entrée), remet chaque réglage changé depuis l'ouverture de la fenêtre à la valeur qu'il avait avant, puis ferme la fenêtre ; la case de fermeture et Échap font de même. Quand un réglage diffère de sa valeur à l'ouverture, une boîte demande d'abord « Discard the changes? » : son Cancel, le choix par défaut (Entrée, Échap), garde la fenêtre ouverte avec les changements, OK les annule et la ferme. Un réglage changé puis remis à sa valeur ne demande rien. Fermer l'éditeur garde les changements.

**SOUND ENGINE** (cadre de gauche). Ces réglages sont enregistrés avec le projet ; tous sauf Quantize to VBL sont aussi des paramètres d'hôte (la fréquence de l'horloge Custom et Tuning A4 sont des paramètres que l'hôte ne peut pas automatiser).

| Réglage | Valeurs | Remarques |
|---|---|---|
| Computer | ST, STE | Comme dans le cadre SYSTEM |
| Chip clock | Custom (par défaut, 2.0000 MHz), Atari ST 2.0053 MHz (PAL), ZX Spectrum 1.7734 MHz, Amstrad CPC 1 MHz | Le bouton affiche l'horloge utilisée. La première entrée, **Custom** avec sa fréquence, ouvre une boîte où vous tapez la fréquence en MHz, de 0.1000 à 8.0000 (quatre décimales au plus ; une virgule est lue comme un point). Elle ne s'applique qu'en 16 ch : en Authentic 3ch et Dual chip, l'entrée est grisée et la puce tourne à 2 MHz. ZX Spectrum et Amstrad CPC sont grisés en Authentic 3ch. Ne change que la fréquence de la puce ; le ZX Spectrum et l'Amstrad CPC ne sont pas émulés. La valeur Atari ST (machine PAL) vient de sources d'émulateurs : **À CONFIRMER** sur le matériel |
| Tuning A4 | 350.00 à 480.00 Hz (440.00 Hz par défaut) | La fréquence du La 4 (A4, note MIDI 69), d'où toutes les notes sont accordées : Chip Voice, SID Voice, Sync Buzzer, les glissandos, et les samples Digidrum et DMA ; les sons Chipdrum gardent leur hauteur. Cliquez sur la valeur et tapez-la en Hz, deux décimales au plus (une virgule est lue comme un point) ; une valeur hors limites est ramenée à 350 ou 480 Hz. La puce ne joue que des périodes entières, donc une note sonne à la période la plus proche : à 2 MHz, le La 4 à 440 Hz reçoit la période 284, qui joue 440,14 Hz |
| Output chain | Raw (par défaut), ST, STE | Raw : la puce seule. ST : le filtre analogique de la carte de l'Atari ST. STE : la sortie du STE avec réglages de graves et d'aigus ; grisée avec Computer ST. Valeurs des composants des filtres : **À CONFIRMER** sur le matériel |
| Resampling | Sinc (par défaut), Box (AtariAudio), Retro | La façon dont la fréquence interne de la puce est ramenée à la fréquence de l'hôte. Box et Retro se rabattent sur Sinc quand la fréquence de l'hôte dépasse clk/8 (par exemple 176,4 ou 192 kHz avec l'horloge de l'Amstrad CPC) |
| STE bass, STE treble | −12 à +12 dB, par pas de 2 dB | Chaîne de sortie STE seulement ; grisés avec Computer ST |
| Soft clip | On (par défaut), Off | Une saturation tanh sur la sortie |
| Timer | MFP (par défaut), Free | Effets de timer quantifiés par le MFP 68901 de l'Atari, ou continus. Free est grisé en Authentic 3ch |
| VBL frequency | 50 Hz (PAL, par défaut), 60 Hz (NTSC), 70 Hz (Monochrome), 100 Hz, 200 Hz | Le tick de replay : il cadence l'ADSR Chip, les tables Modulation, le cadencement Ticks du séquenceur et les exports de registres. Il était avant dans le menu Options |
| Quantize to VBL | Off (par défaut), On | Les notes, les événements MIDI, les pas du séquenceur et les arpèges ne prennent effet qu'au début de chaque trame de replay (fréquence VBL), comme les jouerait une routine de replay Atari ; le tempo ne dérive pas |

**MIDI** (cadre du milieu). Enregistrés avec le projet, et paramètres d'hôte (non automatisables).

| Réglage | Valeurs | Remarques |
|---|---|---|
| MIDI routing | Omni, Layer, Per channel | Section 7.1 |
| Velocity | Accent, Vol 4-bit, Off | Section 7.2 |
| Bend | Pitch (par défaut), Volume, Envelope | Ce que pilote le pitch bend, section 7.2 |
| Bend range | 0 à 24 demi-tons (2 par défaut) | |
| Wheel | Off, Vibrato (par défaut), Volume, Envelope, LFO | Ce que pilote la molette de modulation, section 7.2 |
| Wheel depth | 0 à 200 cents (menu par pas de 25 ; 50 par défaut) | Le vibrato de la molette, de l'aftertouch et du slide MPE à leur maximum |
| Aftertouch | Off, Vibrato (par défaut), Volume, Envelope, LFO | Ce que pilote la pression des touches, section 7.2 |
| MPE | Off (par défaut), On | 16 ch seulement, section 7.2 |
| MPE bend range | 0 à 96 demi-tons (menu : 1, 2, 3, 4, 5, 7, 12, puis par 12 ; 48 par défaut) | Grisé tant que MPE est sur Off |
| MPE slide (CC74) | Volume (par défaut), Envelope, Vibrato | Grisé tant que MPE est sur Off |

**INTERFACE** (cadre de droite). Vos préférences, communes à toutes les instances de STYM2149 (section 9) : Pixels (section 3.1), Renderer (Windows seulement, grisé ailleurs), Note names, Keyboard layout (section 4.6), Tooltips (On, Off), Language, et le bouton **Languages folder...**.

---

## 4. Fenêtres flottantes

| Fenêtre | S'ouvre avec | Section |
|---|---|---|
| Step Sequencer | Bouton Step Sequencer, menu Edit, Ctrl+E | 5 |
| Song | Bouton Song du Step Sequencer, menu Edit, Ctrl+G | 5.11 |
| Mixer | Bouton Mixer, menu Edit, Ctrl+M | 4.1 |
| Arpeggiator | Bouton Arpeggiator, menu Edit, Ctrl+B | 4.2 |
| Modulation | Bouton Modulation, menu Edit, Ctrl+R | 4.3 |
| Recorder | Menu File | 4.4 |
| YM Player | File > Import > YM dump | 4.5 |
| Computer Keyboard | Bouton Computer keyboard | 4.6 |
| Slide Curve | Entrée Custom de la ligne CRV | 4.7 |
| Effects | Bouton Effects, menu Edit, Ctrl+F | 4.8 |
| Scope | Clic sur le mini-oscilloscope d'une piste dans le Mixer | 4.1 |
| Instruments, Patches, Drum Kit, Digidrums, DMA | Menu Edit, Ctrl+T, Ctrl+P, Ctrl+K | 6 |

### 4.1 Mixer

Une tranche par piste (en Authentic 3ch : A, B, C et Digi, huit tranches avec deux puces, plus les pistes DMA avec Computer STE). Chaque tranche comporte :

- le nom de la piste (cliquez dessus pour rendre cette piste courante) ;
- un mini-oscilloscope (cliquez dessus pour ouvrir une fenêtre **Scope** plus grande pour cette piste) ;
- un fader vertical, 0 à 100 ; pour les voix de la puce en Authentic 3ch et Dual chip, dont le niveau est le volume 4 bits, le fader est ce **Volume** du cadre SOURCE, de 0 à 15, et bouge avec lui (grisé sur un Sync Buzzer, qui n'a pas de volume fixe) ;
- un indicateur de niveau. Une piste DMA qui joue un sample stéréo a deux oscilloscopes dans le cadre, gauche puis droite (un clic sur l'un ou l'autre ouvre le Scope), et deux indicateurs de niveau, gauche et droite ;
- un curseur de panoramique, −100 à +100 (un double-clic le centre ; absent pour les voix de la puce en Authentic 3ch, puisque le ST est mono) ;
- en 16 voies, un curseur **Send**, 0 à 100 % : la part de la piste, après son fader et son panoramique, envoyée au delay (section 4.8) ; comme le panoramique, Paste settings ne le copie pas. Les modes 3 voies n'ont pas de rangée Send, le delay étant un effet 16 voies ;
- **M** (muet) et **S** (solo).

Une piste DMA qui joue un sample stéréo en a deux de chaque : deux faders, chacun avec son indicateur et sa valeur, celui du côté gauche puis celui du côté droit, un **M** et un **S** sous chacun, et un bouton **Link** sur une rangée en dessous (la fenêtre grandit de cette rangée). Lié (par défaut), chaque fader règle le Volume de la piste et les deux bougent ensemble ; délié, le fader de droite est le niveau propre du côté droit (paramètre d'hôte Level R), qui part du Volume. Les M et S de chaque côté sont indépendants, liés ou non : coupez le côté gauche seul, ou isolez le côté droit seul (paramètres d'hôte Mute R, Solo R et Link). Ces réglages appartiennent au Mixer, comme Send : un patch, Paste settings et les fichiers de preset les laissent de côté. Sur un sample mono, ils ne font rien et ne s'affichent pas.

Après un trait vertical, le fader **Master** est le niveau principal, le même que Master dans le cadre SYSTEM. Quand la sortie est stéréo (16 voies, et Authentic 3ch avec le STE), Master a deux faders, **L** et **R**, un curseur de balance sur la rangée des panoramiques (un double clic le centre) et un bouton **Link** sur la rangée de M. Lié (par défaut), chaque fader règle les deux côtés ; délié, chaque fader règle son côté (paramètres d'hôte Master et Master 2). La balance baisse seulement le côté opposé. En Dual chip 2x3ch, les deux faders sont **1** et **2**, un par puce, avec Link et sans balance : délié, Master règle la puce 1 et Master 2 la puce 2 ; les pistes DMA du STE suivent Master. En Authentic 3ch avec le ST, la sortie est mono et Master n'a qu'un fader.

Sous chaque fader, panoramique, Send et Master, sa valeur s'affiche en petits chiffres, nommée sous un panoramique ou un Send (« Pan +25 », « Send 50 » ; panoramiques et balance signés, + vers la droite). Un clic droit sur l'un de ces curseurs ouvre un menu : **Value...** pour taper un nombre dans sa plage, **Reset** pour revenir à sa valeur par défaut. Un clic sur la valeur elle-même la fait taper sur place : taper le nombre, Enter valide, Escape annule (sous un panoramique ou un Send, seul le nombre se tape).

Indicateurs de sortie : Out L et Out R, ou un seul Out en Authentic 3ch sans le STE. Chaque déplacement d'un fader, d'un panoramique ou d'un Send, chaque valeur tapée ou remise par défaut, et chaque clic sur M, S ou le Link d'une piste, constitue une étape d'annulation (les faders Master, la balance et Link, comme le Master du cadre SYSTEM, n'en ont pas).

### 4.2 Arpeggiator

L'arpège de la piste courante (indisponible sur les pistes de percussion).

| Commande | Effet |
|---|---|
| Off / On | Active l'arpégiateur pour ce canal (50 Hz = rythme VBL) |
| Mode | Up, Down, UpDn, Order, Random, Table |
| Sync | Allumé : l'arpège suit le tempo, un pas par Division : dans un DAW, le tempo de l'hôte, calé sur sa grille pendant la lecture ; en autonome ou en Free run, le tempo du séquenceur. Éteint : Rate. Paramètre d'hôte Arp Sync, automatisable. Avec Clock MIDI ext, allumé et grisé : l'horloge MIDI |
| Rate | 4 à 200 Hz (Clock Internal, Sync éteint) |
| Track | « Slide follows » : pendant un glissando, la vitesse de l'arpège monte avec la hauteur |
| Land | Au dernier tick d'un glissando, revient à la fondamentale pour que la note visée enchaîne sans saut |
| Division | Bouton menu : 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32, 1/32T, 1/64, et Tick (une impulsion d'horloge MIDI) avec Clock MIDI ext seulement (Sync allumé ou Clock MIDI ext, remplace Rate ; sur la ligne de Mode, après Sync) |
| Octaves | 1, 2 ou 3 (pas en mode Table) |
| Steps | Mode Table : 1 à 16 pas |
| Chord... | Mode Table : remplit la table avec l'un des 39 accords |
| Grid | Mode Table : 16 pas × décalages de +0 à +24 demi-tons, appliqués à la dernière note jouée |

Avec la fenêtre au premier plan et l'arpégiateur activé, les touches fléchées déplacent le curseur de la table et Enter écrit le pas.

Les touches tenues sont arpégées comme une note par touche enfoncée (ou par note MIDI), pas une par pas : l'enveloppe de cette note continue à travers les pas de l'arpège, et son point sur la courbe de l'ADSR ENVELOPE avec elle ; chaque touche enfoncée lance une nouvelle note depuis l'attaque. En 16 voies avec un **Voicing** Polyphonic, la note précédente passe alors dans son release pendant que la nouvelle joue : avec un Release long, elles se chevauchent, chacune avec son point sur la courbe. Avec Monophonic, et sur toutes les pistes des modes 3 canaux, la note précédente est coupée. Touches relâchées, la dernière note continue de parcourir l'arpège pendant son release, les précédentes finissant le leur à leur hauteur. Dans le Step Sequencer, chaque note du motif est de même une seule note de l'arpège (ligne ARP, 5.5). La table de Modulation, le vibrato et le LFO continuent d'un pas de l'arpège au suivant.

### 4.3 Modulation

Tables par trame VBL de la piste courante (l'« ornement » des trackers), et les lignes de séquence jouées avec elles. Indisponibles sur les pistes de percussion.

| Commande | Effet |
|---|---|
| Off / On | Active la table et ses lignes de séquence pour ce canal |
| Len | 1 à 64 pas |
| Loop / Hold | Boucle la table, ou tient son dernier pas |
| 1/frame … 4/frame | Vitesse de la table : ticks VBL par pas |
| Clear | Efface la table et ses lignes de séquence (une seule annulation) |
| Lignes de hauteur | +12 à −12 demi-tons par pas |
| Ligne NSE | Ajoute du bruit sur ce tick |
| Ligne VOL | Niveau 4 bits sur ce tick (clic droit : curseur précis) |

Cliquez ou faites glisser pour dessiner. Avec la fenêtre au premier plan et la table activée : les flèches déplacent le curseur, Enter écrit la hauteur, Delete réinitialise le pas, N active ou désactive le bruit.

Quand la table est activée, les lignes de séquence placées dedans, entre les lignes de hauteur et NSE / VOL, jouent avec elle, tick par tick (mêmes longueur, boucle et vitesse) :

| Ligne | Contenu |
|---|---|
| PER | Décalage de période de son, −127 à +127 (+ est plus grave). Faites glisser pour dessiner, molette de la souris pour ajuster (Shift : par 10), clic droit pour un curseur précis |
| NP | Période de bruit, 0 à 31. Mêmes gestes que PER |
| MIX | Mode du canal 0 à 7 (Off, Tone, Noise, Tone+Noise, Env, Tone+Env, Noise+Env, Tone+Noise+Env), ou le mixage de la piste. Cliquez pour choisir |
| GEN | Générateur de la note, Chip Voice, SID Voice ou Sync Buzzer. Cliquez pour choisir |
| FIX | Le pas joue la note fixe. Cliquez pour activer ou désactiver |
| OUT | Lecture seule : la hauteur que joue un C4 à chaque pas |

Un clic droit efface une cellule MIX, GEN ou FIX. Le curseur qu'ouvre un clic droit sur VOL, PER ou NP applique chaque mouvement aussitôt (une seule annulation pour tout le curseur) ; un clic droit sur le curseur remet la valeur par défaut (15 sur VOL, 0 sur PER et NP), et un clic sur la valeur en dessous permet d'en taper une. Les notes en haut de la fenêtre décrivent toutes les lignes. Les réglages sous la table sont toujours affichés, car Detune, Analog et la matrice de hauteur s'appliquent aussi table désactivée :

- **Fix** : la note fixe (0 à 127). **Detune** : −100 à +100 cents. **Analog** : décalage de hauteur aléatoire tiré à chaque note, jusqu'à 50 cents.
- **Hard sync** : relance le carré (16 ch) et le timer aussi sur les notes liées et glissées.
- **Link** : l'instrument qui prend le relais quand une table Hold se termine.
- **Pitch matrix** : quelles cibles (Sq : le carré, Tm : le timer de SID et de Sync Buzzer et la hauteur d'un sample DMA, Bz : la période d'enveloppe des buzzers) sont modifiées par chaque source (Table pitch, Slides, Vibrato).
- **Vibrato table** : décalages de période Min et Max, Cycles (1 à 15), Ramp (0 à 64 pas) ; **Write PER** l'écrit dans la ligne PER.

Sur une piste DMA, la hauteur de table, PER, FIX, les glissés et le vibrato modifient le sample par la colonne Tm, et VOL règle son niveau. Ce qui ne modifie rien d'un sample est grisé, son infobulle disant pourquoi : les lignes NSE, NP, MIX et GEN, Hard sync et les colonnes Sq et Bz de la matrice de hauteur. Leurs valeurs sont gardées pour un autre générateur.

**LFO** (sous les réglages) : une onde lente qui fait varier un réglage du son de la piste, relancée à chaque note ou libre (Retrig) ; elle continue sur les notes d'un arpège et dans un instrument chaîné. Il ne change que les valeurs écrites dans les registres du YM, il joue donc aussi en Authentic 3ch et dans les exports de registres. Indisponible sur les pistes de percussion.

| Contrôle | Effet |
|---|---|
| Off / On | Active le LFO de la piste ; tant qu'il est sur Off, seuls le libellé LFO et ce bouton sont affichés |
| Shape | Triangle, Sine, Square, Saw up, Saw down, Random (une nouvelle valeur tenue à chaque cycle), dessinées sur le bouton et dans son menu |
| To | Volume (pas de 0 à 15), Noise period (0 à 31), Env period, SID ratio et Pitch (en cents, jusqu'à 2400), Pan (pourcentage, 16 voies seulement). Une cible que la piste ne peut pas faire varier est grisée dans le menu, son infobulle dit pourquoi : Volume pour un Sync Buzzer ou une source sur l'enveloppe (le volume 4 bits est ignoré), Noise period sans bruit dans la source (ou pour un Sync Buzzer ou une piste DMA), Env period sans l'enveloppe (Vol 4-bit, SID Voice, DMA), SID ratio hors SID Voice, Pan dans les modes 3 voies. Si la cible choisie plus tôt ne s'applique plus (le générateur ou la source ont changé depuis), les autres réglages sont grisés et To dit pourquoi |
| Retrig | Allumé (par défaut) : le LFO repart à chaque note. Éteint : il tourne librement et chaque note le prend où il en est ; synchronisé au tempo pendant que l'hôte joue, il suit la grille des temps de l'hôte. Les LFO libres repartent au lancement du séquenceur |
| Sync | Éteint : Rate en Hz, de 0,1 à 20. Allumé : un cycle par division du tempo, de 4/1 à 1/64 (4/1, 2/1, 1/1, 1/2, 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32, 1/32T, 1/64), au tempo de l'hôte dans un DAW, du séquenceur sinon. Avec une fréquence VBL de 50 Hz, un cycle plus rapide que 25 Hz (1/64 dès 94 BPM) se replie sur un plus lent : choisissez 100 ou 200 Hz pour les divisions les plus rapides |
| Rate | Sync éteint : un curseur en Hz ; cliquez sur sa valeur pour taper `7.5`, ou une division comme `1/8T`, qui allume Sync. Sync allumé : un bouton menu des divisions |
| Depth | De combien le réglage s'écarte de part et d'autre de sa valeur, dans l'unité de la cible ; changer de cible garde sa part de l'étendue |

La cible Pitch suit la ligne Vibrato de la matrice de hauteur (Sq, Tm, Bz). Chaque réglage est une étape d'annulation ; le LFO est enregistré avec le projet, les patches et les instruments.

### 4.4 Recorder

Enregistre les registres YM d'une puce pendant qu'elle joue : une trame de R0 à R13 par tick de replay (fréquence VBL).

- **Chip** 1 ou 2 (2 en Dual chip seulement). **Rec** arme une prise : elle démarre et s'arrête avec la lecture de l'hôte (application autonome : avec le séquenceur). Cliquez à nouveau pour arrêter.
- **Take** affiche les trames, la durée, la fréquence et la puce de la dernière prise. La dernière prise est enregistrée avec le projet. **Clear** la supprime après une confirmation.
- **Export** : YM6, SNDH ou VBL dump de la prise (section 8.3). L'enregistreur ne conserve aucun son DMA.
- 16 ch n'a pas de registres YM : passez en Authentic 3ch ou en Dual chip. Une prise contient au plus 131 072 trames (43 min 41 s à 50 Hz).

### 4.5 YM Player

File > Import > YM dump ouvre un morceau (YM2 à YM6, compressé LHA ou non, ou SNDH) dans la fenêtre YM Player. Celle-ci joue le morceau à la place de la sortie de l'instrument (le moniteur de registres et l'oscilloscope le suivent).

- **Play** / **Stop**, **Rewind**, position « trame / total (secondes) ».
- **Subsong** − / + pour les fichiers SNDH à plusieurs sous-morceaux (relance le morceau).
- Fermer la fenêtre arrête la lecture et oublie le morceau ; il n'est pas enregistré dans le projet.

### 4.6 Computer Keyboard

Joue la piste courante depuis le clavier de votre ordinateur. **La frappe ne joue des notes que tant que cette fenêtre est ouverte.**

Sur une piste de percussion (Chipdrum, Digidrum), les touches de la rangée du milieu, A S D F G H J K par position, jouent les 8 pads comme les pads de la fenêtre principale (un pad Digidrum sélectionne aussi son emplacement). Le dessin nomme chaque pad (Kick à Tom 2, ou Slot 1 à Slot 8) ; les autres touches, les boutons d'octave et Held notes n'y font rien.

- Touches, par position sur un clavier QWERTY : A W S E D F T G Y H U J K O L P ; ' jouent de C jusqu'à F de l'octave suivante. Choisissez votre clavier avec Keyboard layout dans Options > Settings (Auto, QWERTY, AZERTY, QWERTZ ; Auto suit la disposition de votre système, et vaut QWERTY pour toute autre disposition) pour que STYM2149 fasse correspondre à ces positions les caractères que tapent vos touches.
- **Oct -** / **Oct +** (touches Z et X) : octave 0 à 9. Les chiffres 0 à 9 du pavé numérique choisissent directement l'octave.
- **Held notes** : **Keep** (une note tenue garde sa hauteur quand vous changez d'octave) ou **Follow** (elle saute à la nouvelle octave).
- Le dessin montre quelles touches jouent, lesquelles changent d'octave, lesquelles sont des raccourcis, et la plage jouée. Vous pouvez aussi cliquer sur les touches dessinées.
- Esc ne ferme pas cette fenêtre. La fermer fait taire les notes tenues depuis l'écran.

### 4.7 Slide Curve

La courbe **Custom** de la ligne CRV (section 5.6), commune à toutes les pistes : faites glisser pour dessiner (gauche = début du glissé, droite = arrivée). **Linear** la remet en ligne droite, **Smooth** la lisse, **Invert** la retourne de haut en bas, toute entière : le glissé part alors de la note visée et revient au départ. Un avertissement apparaît quand la courbe n'atteint pas la note visée. Esc ne ferme pas cette fenêtre, et ses modifications ne peuvent pas être annulées.

### 4.8 Effects

Deux effets dans une fenêtre, EFFECTS (le bouton Effects, Edit > Effects ou Ctrl+F).

**Echo**, dans tous les modes de voix : chaque note du son de la piste courante est rejouée, de plus en plus doucement. Les répétitions sont des notes de la puce : Authentic 3ch les joue, le Recorder et les exports de registres les gardent. L'écho appartient au son : il est enregistré avec le projet, les patches et les instruments, et chaque réglage est une étape d'annulation de la piste.

| Commande | Rôle |
|---|---|
| Echo Off / On | Active l'écho de la piste ; tant qu'il est à Off, seule cette rangée s'affiche |
| Division | Temps entre deux répétitions, une division du tempo (1/8 par défaut) |
| To | Piste qui joue les répétitions : la même (par défaut) ou une autre, avec le son de cette piste (ou l'instrument qu'a joué la note) |
| Repeats | 1 à 8 (3 par défaut) |
| Drop | Pas du volume 4 bits perdus à chaque répétition, 0 à 15 (3 par défaut) ; les répétitions s'arrêtent au volume 0 |

Chaque répétition dure autant que la note a été tenue. Un Sync Buzzer n'a pas d'écho en Authentic 3ch et Dual chip : il joue l'unique enveloppe matérielle de la puce, qu'une répétition relancerait à plein niveau ; les réglages de l'écho y sont grisés (en 16 voies chaque voix a sa propre enveloppe, et l'écho joue). En Authentic 3ch et Dual chip, un canal ne joue une répétition que s'il ne joue aucune note à lui : une répétition de la même piste attend que la note soit relâchée et prend son canal ; une répétition pour une autre piste prend le canal de cette piste quand il est libre (un Digidrum prend le canal digidrum) ; sinon la répétition est sautée. En 16 voies, une répétition prend une voix libre, ou la plus ancienne répétition ; une nouvelle note prend la voix d'une répétition avant celle d'une note plus ancienne.

**Delay**, en 16 voies seulement : un delay audio alimenté par le **Send** de chaque piste dans le Mixer (0 par défaut : rien ne s'entend tant qu'aucune piste ne lui envoie de son). Aucun ST ne pourrait le faire : en Authentic 3ch et Dual chip sa rangée est grisée et le son ne change pas. Ses réglages sont des paramètres d'hôte.

| Commande | Rôle |
|---|---|
| Delay Off / On | Active le delay ; tant qu'il est à Off, seule cette rangée s'affiche |
| Ping-pong | Les échos vont d'un côté à l'autre (affiché quand le delay est allumé) |
| Sync / Division | Synchronisé (par défaut) : un écho par Division du tempo (1/8D par défaut), au tempo de l'hôte dans un DAW, à celui du séquenceur sinon |
| Sync éteint / Time | Temps en millisecondes, 10 à 2000 (375 par défaut) |
| Feedback | Part de chaque écho qui revient dans le suivant, 0 à 90 % (35 %) |
| Hi cut | Chaque écho perd les aigus au-dessus de cette fréquence, 500 à 20 000 Hz (6000 Hz) |
| Level | Niveau des échos dans le mix, 0 à 100 % (100 %) |

Les échos s'ajoutent avant le Master ; les sorties de pistes ne les portent pas.

---

## 5. Le séquenceur

### 5.1 Motifs, banques et pistes

- 32 motifs en 4 banques (A à D) de 8. Chaque motif a de 1 à 256 pas (curseur **Steps**) et contient les 16 pistes.
- Cliquez sur une banque, puis sur un motif (1 à 8). Les touches 1 à 8 choisissent un motif de la banque courante ; Shift+1 à Shift+4 choisissent la banque A à D. Pendant la lecture, un nouveau motif démarre à la fin de la boucle en cours.
- Clic droit sur un bouton de motif : Copy, Cut, Paste, +1 semitone, −1 semitone, +1 octave, −1 octave.
- La grille édite la **piste courante** (onglets de la fenêtre principale).

### 5.2 La fenêtre Step Sequencer

Le titre affiche « STEP SEQUENCER - nom de la piste [générateur] ». La fenêtre s'ouvre près du bord gauche du bureau ; quand ses lignes la rendent plus haute que le bureau, elle remonte pour que son bas reste visible, jamais au-dessus de la barre de menus. La fenêtre contient :

- **Outils** : Move, Draw (par défaut), Erase, Split, Join, Select ; **Copy**, **Cut**, **Paste**.
- **Step** (saisie pas à pas : une note que vous jouez est écrite au curseur, qui avance ensuite), **Clear** (efface la piste courante après une confirmation), **Euclid** (générateur euclidien, 5.7), **<** et **>** (font tourner la piste), **-1** / **+1** (transposent d'un demi-ton), **Oct-** / **Oct+** (transposent d'une octave), **Follow** (la vue suit la tête de lecture), **Len =** (longueur de la piste, 5.7), **Song** (ouvre la fenêtre Song).
- **Steps** (1 à 256), **BPM** (60 à 240, Free run seulement ; une commande `Txx` change le tempo pendant la lecture, 5.5), **Gate** (5 à 95 %) et **All** (un même gate pour toutes les pistes), **Swing** (50 à 75 % : 50 est droit, 66 un shuffle de triolets).
- **Scale** : tonique, gamme (Off et 25 gammes, des modes Major et Minor jusqu'à Blues, Whole tone et Chromatic) et **Snap** (les notes sont forcées dans la gamme : une note ou une voix d'accord posée sur une ligne grisée va au degré le plus proche). Les lignes hors de la gamme sont grisées.
- **Signature** (2/4 à 7/4, 5/8 à 12/8 : les accents et le métronome la suivent), **Resolution** (valeur d'un pas, et le cadencement Ticks, 5.8).
- **Note length** (figure écrite quand vous placez une note, dessinée sur son bouton : Step, long d'un pas, dessiné comme la note de la Resolution ; 1/1 à 1/64, valeurs pointées et triolets de 1/4T à 1/32T ; les valeurs qui ne font pas un nombre entier de pas de la Resolution sont grisées, avec leur longueur non arrondie, et une valeur choisie avant qui ne tombe plus juste écrit un pas tant que la Resolution ne lui convient pas), **Chord** (Off ou l'un des 39 accords, ajouté au-dessus de la note que vous placez), **Voicing** (Polyphonic ou Monophonic).
- **Ghost** (affiche les autres canaux derrière la grille ; activé par défaut) et **Overview** (une bande avec une ligne par canal ; dessous, seule la ligne « Whole pattern » remplace la légende de la grille).
- La **grille** : 13 lignes de notes (une octave et une note) et 64 pas visibles, séparés par des lignes de 2 pixels, avec une barre de défilement au bord droit de la fenêtre, aussi haute que toute la grille, pour faire défiler les notes de B0 à C9 (une flèche : un demi-ton ; un clic sur la barre à côté du curseur : une octave ; la molette de la souris au-dessus de la grille : un demi-ton) et une barre de défilement pour les motifs de plus de 64 pas. Sous les lignes de notes se trouvent les lignes de contrôle (5.4).
- Le **transport** : Play/Stop, Rec, Loop, Play from start, entrée de morceau précédente et suivante, Metronome (un clic sur chaque temps, une cloche sur le premier temps de la mesure ; il s'entend, mais l'oscilloscope et les indicateurs de sortie du Mixer ne le dessinent pas ; un clic droit ouvre son volume, de 0 à 100 %, gardé avec le projet) ; puis les banques et les motifs.

Sur les pistes de percussion, les lignes de notes deviennent les huit instruments (Chipdrum : Kick, Snr, Clap, Hat, OHat, CHat, Tom1, Tom2 ; Digidrum : Sl 1 à Sl 8) et les coups durent un pas.

### 5.3 Écrire des notes

Avec l'outil Draw :

- **Cliquez** sur une cellule vide pour écrire une note (avec la Note length et le Chord choisis). Faites glisser vers la droite sur la même ligne pour l'allonger ; faites glisser ailleurs pour écrire d'autres notes.
- **Cliquez sur une note** pour la supprimer (si sa longueur est la Note length courante) ou pour lui donner la Note length courante.
- **Faites glisser une note** pour la déplacer ; une note d'une sélection (cadre ou Shift+clic) déplace toute la sélection. Tant que le bouton reste appuyé sur une note qui peut bouger, le pointeur est une main ; il redevient le crayon quand vous le relâchez. Faites glisser son **bord gauche ou droit** (la partie extérieure de son premier ou de son dernier pas, où le pointeur devient une double flèche) pour la redimensionner.
- Sur une piste polyphonique, cliquez sur une autre ligne d'un pas qui a déjà une note pour ajouter une voix d'accord (jusqu'à 6 voix par pas) ; cliquez sur une voix d'accord pour la supprimer.
- **Alt+clic** sur une note la scinde à ce pas.
- **Shift+clic** sur une note l'ajoute à une sélection multiple (ou l'en retire) ; **Shift+glisser** sur des cellules vides trace un cadre de sélection. Sur les lignes de contrôle (et sur les pistes de percussion), Shift+clic sélectionne un bloc de pas.
- **Clic droit** sur une note : Copy, Cut, Paste here, Longer ], Shorter [, Split here `\`, Join with next J, Length, Delete. Clic droit sur une cellule vide quand une note est dans le presse-papiers : Paste note here.
- **Glisser avec le bouton droit** trace un cadre de sélection ; un clic droit dessus donne Copy, Cut, Paste, Clear, Deselect. Un clic droit sur un bloc de pas sélectionné donne Copy block, Cut block, Paste block, Clear block, Deselect, Note length, Channel, Rename channel....

Les autres outils : **Move** déplace des notes ou une sélection, par leur bord aussi, car seul Draw redimensionne une note (un clic sans glisser place seulement le curseur) ; **Erase** supprime les notes sur lesquelles vous cliquez ou passez en glissant ; **Split** scinde en deux la note cliquée à ce pas ; **Join** fusionne la note cliquée avec la suivante sur la même ligne ; **Select** trace un cadre de sélection.

Quand le Step Sequencer est ouvert, le clavier édite aussi : les flèches déplacent le curseur (la vue défile quand vous sortez par le haut ou par le bas), Enter écrit ou supprime une note au curseur, Delete efface le pas. Avec la fenêtre au premier plan et une note sous le curseur : `]` l'allonge, `[` la raccourcit, `\` la scinde au curseur, J la joint à la note suivante, Ctrl+C / Ctrl+X la copient ou la coupent, Ctrl+V colle au curseur. Avec un cadre de sélection : les flèches le déplacent (Shift+↑/↓ d'une octave), Delete l'efface, Esc le désélectionne, Ctrl+C / X / V.

Chaque geste constitue une étape d'annulation (Ctrl+Z, Ctrl+Y).

### 5.4 Lignes de contrôle

Sous les lignes de notes, des lignes de contrôle règlent des options par pas, dans l'ordre de ce qu'elles font à la note : sa hauteur, son son (GEN décide de ce que peuvent SRC, ENV et SMP), ses modulations, son glissé, son placement dans le pas, les autres commandes, sa place dans le mixage. Les lignes affichées dépendent de la piste :

| Ligne | Affichée sur | Valeurs | Effet |
|---|---|---|---|
| OCT | Percussion | −3 à +3 | Accorde la percussion par octaves |
| TON | Percussion | −12 à +12 | Accorde la percussion par demi-tons |
| TUN | Mélodique | −99 à +99 | Accord de la piste en cents à partir de ce pas (`Fxx`, 5.5) |
| INS | Mélodique, quand le projet a des instruments | Channel, $01 à $20 | Joue la note avec un instrument du projet (6.1) |
| GEN | Mélodique | Channel, Chip Voice, SID Voice, Sync Buzzer | Un autre générateur sur ce pas seulement |
| SRC | Mélodique | Channel, Tone, Noise, Tone+Noise, Env, Tone+Env, Noise+Env, Tone+Noise+Env | Onde carrée, bruit ou les deux sur ce pas. Tone, Noise et Tone+Noise gardent le mode de volume de la piste ; les entrées Env mettent le volume sur l'enveloppe (Env seul : ni carré ni bruit) |
| ENV | Mélodique, quand l'enveloppe joue : source Env, générateur Sync Buzzer, une entrée Env dans SRC ou Sync Buzzer dans GEN | No change, formes 8 à F ; Restart : None, At once, At tick | Forme de l'enveloppe matérielle de la piste à partir de ce pas (`Exx`), et relance de l'enveloppe à un tick de la ligne (`Rxx`) ; la cellule affiche la forme et R (5.5) |
| SMP | Pistes DMA | 0 à 255 | Le sample DMA de ce pas démarre d'autant de pas de 256 trames après son Start (`Oxx`, 5.5) |
| ARP | Mélodique, quand l'arpégiateur de la piste est activé | No change, On, Off | Allume ou éteint l'arpégiateur de la piste à partir de ce pas (`Jxx`, 5.5) |
| VIB | Mélodique, avec Vibrato sur On dans le cadre VIBRATO | No change, On, Off | Allume ou éteint le vibrato de la piste à partir de ce pas (`Vxx`, 5.5) |
| LFO | Mélodique, avec le LFO de la piste allumé (fenêtre Modulation) | No change, On, Off | Allume ou éteint le LFO de la piste à partir de ce pas (`Lxx`, 5.5) |
| MOD | Mélodique, avec la table de modulation de la piste allumée (fenêtre Modulation) | No change, On, Off | Allume ou éteint la table de modulation de la piste à partir de ce pas (`Mxx`, 5.5) |
| SLD | Mélodique | on/off | Glisse de cette note jusqu'à la suivante |
| CRV | Mélodique, tant qu'une case SLD est allumée (ou qu'une courbe est écrite) | Linear, Exponential, Logarithmic, S-curve, Stepped, Sawtooth, Wobble, Chromatic, Scale-locked, Overshoot, Bounce, Random walk, Custom | Forme du glissando ; réglez-la là où commence un glissé, ou en cours de glissé pour la changer. Custom ouvre la fenêtre Slide Curve |
| CND | Toutes | Always, 1:2, 2:2, 1:4, 4:4, 25%, 50%, 75% | Ne joue le pas qu'au 1er ou au 2e de chaque groupe de 2 passages, au 1er ou au 4e de chaque groupe de 4, ou avec cette probabilité |
| DLY | Toutes | 1 à 255 | Les notes de ce pas partent à ce tick de la ligne (`Dxx`, 5.5) |
| RTG | Toutes | 1 à 4 | Répète le coup 2, 3 ou 4 fois à l'intérieur du pas |
| GAT | Mélodique | Channel, ou 0 à 100 % | Longueur de la note sur ce pas, en proportion du pas |
| HLD | Chipdrum | on/off | Ne redéclenche pas la percussion tant qu'elle résonne encore |
| CUT | Toutes | 0 à 255 | Les notes de ce pas s'arrêtent à ce tick de la ligne (`Cxx`, 5.5) |
| CMD | Affichée par défaut (menu Resolution > Step command rows la masque), et dès qu'une commande de sa boîte est écrite | TIK, SYN, VSL, BSL, NSL ; MUL pour plusieurs | Commandes de pas (5.5) |
| PAN | 16 ch, et pistes DMA | Center, −10 à +10 | Position stéréo de ce pas |
| VOL | Toutes | 0 à 15 | Accent (niveau 4 bits). Faites glisser pour dessiner ; clic droit pour un curseur (clic droit dessus : retour à 15 ; clic sur sa valeur pour en taper une) et son bouton **Slide** : le niveau glisse vers le VOL du pas suivant (`Gxx`, 5.5), dessiné en rampe |
| BPM | Application autonome, sous VOL après un séparateur | 32 à 255 | Tempo de toutes les pistes à partir de ce pas : une seule ligne pour tout le motif, la même sur chaque piste (`Txx`, 5.5) |

ENV, VIB, LFO, MOD, TUN, DLY, CUT, SMP et BPM sont affichées avec la ligne CMD (Resolution > Step command rows), chacune aussi tant qu'une de ses valeurs est écrite ; ARP est affichée dès que l'arpégiateur de la piste est allumé. ARP, VIB, LFO et MOD disparaissent tant que l'arpégiateur, le vibrato, le LFO ou la table de modulation de la piste est éteint, et reviennent avec leurs valeurs quand il est rallumé.

Une valeur écrite là où elle ne peut pas agir est grisée et barrée d'une croix, et son infobulle dit pourquoi : le LFO de la piste sans profondeur (LFO), une piste de percussion (TUN), aucune enveloppe ne joue (ENV), pas une piste DMA ou aucune note ne part sur ce pas (SMP, DLY), aucun glissé ne joue (CRV), cadencement Ticks ou horloge externe (BPM). Elle reste modifiable et effaçable.

Une case où rien ne pourrait agir à cause du son de son pas est barrée même vide, et un clic n'y ouvre rien ; son infobulle dit pourquoi. Ce son est le générateur de la note (GEN, sinon son instrument, sinon la piste) : ENV sous une SID Voice, qui écrit ses propres volumes ; SRC sous un Sync Buzzer ; SRC et ENV sous le DMA ou un son de percussion ; SMP hors des notes DMA ; GEN sur les pistes DMA d'Authentic 3ch et de Dual chip, et sur une note DMA ou de percussion tenue ; INS et CND sur une note tenue, INS sur la note qu'atteint un glissé (il garde l'instrument de la note d'où il part) ; GAT et RTG pendant un glissé ; GAT sur un pas après lequel la note continue (seul le GAT de son dernier pas agit). Sur un pas où aucune note ne sonne, une case vide reste ouverte, puisqu'une note peut y être écrite plus tard ; une valeur déjà écrite y est barrée.

Une note dessinée sur plusieurs pas est tenue : SRC, GEN, CRV, PAN, VOL, RTG et ARP agissent sur chacun de ses pas. VOL règle son niveau pas par pas ; RTG la refrappe depuis le début de son pas ; ARP y lance ou reprend l'arpège (RTG attend tant que l'arpège joue, et pendant un glissé seul VOL agit). GAT agit sur le dernier pas de la note, où il la relâche ; OCT, TON, INS, CND et SLD appartiennent à son premier pas.

Cliquez sur une ligne on/off pour l'activer ou la désactiver (faites glisser pour peindre). Cliquez sur une ligne de valeurs pour ouvrir son menu ; GAT, PAN, BPM, TUN, DLY, CUT et SMP ouvrent une petite boîte (PAN affiche 0 au centre) ; CMD ouvre la boîte des commandes du pas. Faites un clic droit sur une cellule de contrôle pour la réinitialiser (faites glisser pour en réinitialiser davantage), sauf une case GEN, SRC ou ENV qui porte une valeur : son clic droit ouvre une fenêtre avec les réglages de la piste courante que la case met en jeu, qui agissent comme les mêmes commandes de la fenêtre principale. GEN : ceux de SID Voice (Timer ratio, SID min vol, la SID wave, le menu de ses formes, ses Steps, Period avec Sine ou Bounce, Width avec Square, disposés comme dans la fenêtre principale) ou de Sync Buzzer (Tuning, N, D, Fine, YM Pad, Presets, les pas) ; Chip Voice n'en a pas. SRC : Noise period et Volume, grisés quand la source de la case ne les utilise pas, et Level. ENV : Env freq et EP. Une seule fenêtre s'ouvre à la fois, celle de la dernière case cliquée. Faites un clic droit sur le libellé d'une ligne pour **Copy row** / **Paste row**, qui copie une ligne entière d'une piste à une autre (sauf BPM, que toutes les pistes partagent).

### 5.5 Commandes de pas (CMD et les lignes de commandes)

La ligne CMD et les lignes propres des commandes (ENV, VIB, LFO, MOD, TUN, DLY, CUT, SMP, BPM) sont affichées par défaut ; Resolution > Step command rows > Off les masque (une préférence) tant qu'aucune de leurs valeurs n'est écrite. ARP est affichée avec l'arpégiateur. Un pas porte jusqu'à quinze commandes par piste, une de chaque sorte, qui jouent ensemble, et le tempo du motif. La cellule CMD affiche le nom court de la commande de sa boîte (TIK, SYN, VSL, BSL, NSL), ou MUL quand il y en a plusieurs ; BPM, TUN, DLY, CUT et SMP affichent leur nombre, ENV la forme (8 à F) et R pour une relance, ARP, VIB, LFO et MOD On ou Off.

Cliquez sur une cellule CMD pour ouvrir la boîte **Commands** : une ligne par commande, dans l'ordre Ticks per Line, Sync Mark, Volume Slide, Buzzer Slide, Noise Slide, chacune avec sa valeur et un bouton **On**/**Off**. Un glissé a une vitesse de 0 à 15 (0 l'arrête) et un bouton pour son sens, **Up** ou **Down** (écrit `x0` ou `0y`). En 16 voies, le bouton des Buzzer et Noise Slides ouvre un menu : Up, Down et un sous-menu **Curve** avec les 13 formes de la ligne CRV ; le bouton affiche le sens et le nom court de la forme (Down EXP). Une forme autre que Linear (`Kxx`, `Qxx`) fait suivre cette forme au glissé, de son pas jusqu'à la commande suivante de son type (jusqu'à la fin de la piste s'il n'y en a pas), en couvrant ce que sa vitesse couvre en ligne droite ; Linear garde la vitesse par trame. Les autres commandes ont un curseur et un champ tapé en décimal. Un glissé n'est dans la boîte que s'il peut faire bouger une note entre son pas et la commande suivante de son type, la piste bouclant : Buzzer Slide une note dont on entend l'enveloppe (source ou entrée SRC Env, Sync Buzzer) ; Noise Slide une note dont on entend le bruit (Source ou SRC avec bruit, Chip Voice ou SID Voice) ; le Volume Slide, que le Slide d'une case VOL remplace, seulement là où un projet plus ancien en a écrit un. Chaque note a le générateur de sa case GEN, sinon de son instrument, sinon de la piste, et la source de sa case SRC, sinon de son instrument ou de la piste ; sa table de modulation compte aussi, et un pas vide compte comme la note qu'on pourrait y écrire. Aucun glissé sur une piste de percussion. Un glissé écrit sur le pas reste dans la boîte, pour pouvoir l'éteindre. Choisir une valeur met sa ligne sur On. **OK** écrit toutes les lignes dans le pas ; **Cancel**, le bouton par défaut (Entrée, Échap), ferme sans le changer. Une commande absente du pas propose une valeur de départ : les ticks par ligne du morceau, marque 1, volume descendant d'un quart de pas par tick (`01`), les autres glissés montant d'une unité (`10`). Un clic droit sur une cellule CMD efface les commandes de sa boîte.

Les autres commandes ont leur propre ligne. **ARP**, **VIB**, **LFO** et **MOD** : un menu, No change, On (`J01`, `V01`, `L01`, `M01`) ou Off (`J00`, `V00`, `L00`, `M00`). **BPM** (application autonome seulement, puisqu'en plugin le DAW règle le tempo ; un tempo écrit reste dans le projet) : une boîte avec un curseur de 32 à 255, qui part du champ BPM ; la ligne appartient au motif, donc un tempo écrit sur une piste apparaît sur toutes. **ENV** (quand l'enveloppe joue) : un menu des formes 8 à F avec leur dessin, et un sous-menu **Restart** : None, At once (tick 0) ou At tick..., qui ouvre une boîte sur les ticks d'une ligne. **DLY** et **CUT** : une boîte avec un curseur sur les ticks d'une ligne au tempo courant, à partir de 1 pour DLY et de 0 pour CUT ; un tick plus grand, jusqu'à 255, peut être tapé et reste ignoré tant que la ligne est plus courte. **TUN** : une boîte en cents, de −99 à +99. **SMP** (pistes DMA) : une boîte de 0 à 255, par pas de 256 trames. **None** dans une boîte, et No change dans un menu, retirent la commande du pas ; un clic droit sur la cellule aussi (sur ENV, la forme et la relance).

| Commande (ligne de la boîte, ou sa propre ligne) | Effet |
|---|---|
| `Sxx` (Ticks per Line) | Ticks par ligne à partir de cette ligne (cadencement Ticks seulement ; 1 à 31). Ignoré en cadencement BPM et en Host sync |
| `Txx` (ligne BPM) | Tempo de toutes les pistes à partir de cette ligne, de 32 à 255 BPM (une valeur plus petite tapée devient 32), une seule ligne pour le motif. Cadencement BPM avec l'horloge propre du séquenceur (Free run) et dans les exports ; ignoré en cadencement Ticks, en Host sync et en MIDI ext. Un projet ou un fichier d'une version précédente qui avait un tempo sur plusieurs pistes d'une même ligne garde le dernier. Le tempo tient jusqu'au `Txx` suivant : Play repart du champ BPM, une entrée de Song qui a son propre tempo repart de celui-ci à chaque passage, et toucher au champ BPM reprend la main |
| `Vxx` (ligne VIB) | Vibrato de la piste à partir de cette ligne : Off (`V00`) le suspend, On (`V01`) le rétablit (le vibrato du cadre VIBRATO ; la molette de modulation ajoute toujours le sien). Tient jusqu'au `Vxx` suivant ; Play et Stop rendent à la piste son vibrato. Avec Vibrato sur Off dans le cadre, `Vxx` ne rétablit rien |
| `Jxx` (ligne ARP) | Arpégiateur de la piste à partir de cette ligne : On (`J01`) le fait jouer sur les notes du motif, Off (`J00`) l'arrête, aussi sur un pas d'une note tenue, qui continue sans arpège. Tient jusqu'au `Jxx` suivant. Chaque motif commence avec l'arpégiateur éteint, comme Play ; lancée au milieu d'un motif, la piste prend le dernier `Jxx` avant ce pas. Avec l'arpégiateur sur Off dans sa fenêtre, `J01` ne fait rien. Les pas ARP d'une version précédente deviennent des `J01` et `J00` et jouent pareil |
| `Lxx` (ligne LFO) | LFO de la piste (fenêtre Modulation) à partir de cette ligne : Off (`L00`) le suspend, On (`L01`) le rétablit. Tient jusqu'au `Lxx` suivant ; chaque motif commence avec le LFO tel que réglé dans la fenêtre |
| `Mxx` (ligne MOD) | Table de modulation de la piste (fenêtre Modulation) à partir de cette ligne : Off (`M00`) la suspend, On (`M01`) la rétablit. Tient jusqu'au `Mxx` suivant ; chaque motif commence avec la table telle que réglée dans la fenêtre |
| `Rxx` (ligne ENV, Restart) | Relance l'enveloppe matérielle (réécrit R13) au tick xx de la ligne (0 à 255), sans redéclencher la note. En Authentic 3ch, seulement si la piste pilote l'enveloppe partagée |
| `Zxx` (Sync Mark) | Synchro de démo ; ne joue rien. Dans un export SNDH, la routine écrit xx (0 à 255) dans son octet de synchro à la trame de la ligne (plusieurs pistes sur une même ligne : la dernière l'emporte). Une démo trouve cet octet à l'adresse que la routine d'init SNDH renvoie dans le registre A0 ; il vaut 0 après l'init. L'endroit où maxYMiser garde son propre octet de synchro est **À CONFIRMER** |
| `Gxx` (ligne VOL, Slide) | Le niveau du pas glisse trame après trame, en ligne droite, de son VOL au VOL du pas suivant : l'accent en 16 voies et sur les pistes DMA, le volume 4 bits dans les modes 3 voies (dans les registres exportés). Pas sur les pistes de percussion |
| `Axy` (Volume Slide, projets plus anciens) | Le volume des notes monte de x ou descend de y quarts de pas (sur les 16 niveaux) par tick de replay, à partir du deuxième tick d'une note ; si les deux sont réglés, il bouge de x − y. Tient jusqu'au `Axy` suivant : `A00` l'arrête. Chaque nouvelle note repart de son propre volume. Sans effet sur un volume piloté par l'enveloppe. Le Slide d'une case VOL le remplace ; la boîte le montre là où un projet en a un |
| `Dxx` (ligne DLY) | Les notes de ce pas partent au tick xx de la ligne ; le reste de la ligne fait leur durée. Un tick au-delà de la ligne est ignoré (les notes partent à l'heure) |
| `Cxx` (ligne CUT) | Les notes de la piste s'arrêtent au tick xx de la ligne (`C00` : tout de suite, la note de ce pas ne sonne pas). Un tick au-delà de la ligne est ignoré. Cette ligne seulement |
| `Bxy` (Buzzer Slide) | La période de l'enveloppe matérielle (R11-R12) glisse : montée x (période plus courte, plus aigu) ou descente y en seizièmes de demi-ton par tick de replay. Tient jusqu'à `B00` ; chaque nouvelle note repart de sa propre période. Fait plier les basses Sync Buzzer et d'enveloppe |
| `Kxx` (Buzzer Slide, Curve) | 16 voies : la forme du `Bxy` du pas (1 à 12 : les formes de la ligne CRV après Linear). Le glissé la suit jusqu'au `Bxy` suivant de la piste ; les modes 3 voies l'ignorent |
| `Nxy` (Noise Slide) | La période de bruit (R6) glisse : montée x (période plus courte, bruit plus aigu) ou descente y en quarts de pas par tick de replay, entre 1 et 31. Tient jusqu'à `N00` ; chaque nouvelle note repart de son propre bruit |
| `Qxx` (Noise Slide, Curve) | 16 voies : la forme du `Nxy` du pas, comme `Kxx` |
| `Exx` (ligne ENV) | Forme R13 de l'enveloppe matérielle de la piste à partir de cette ligne, de 8 à 15 (les formes du menu Shape R13) ; une note tenue la prend tout de suite, ce qui relance l'enveloppe. Tient jusqu'au `Exx` suivant ; Play repart de la forme du son. Les autres valeurs sont ignorées |
| `Oxx` (ligne SMP) | Les samples DMA de ce pas démarrent xx × 256 trames après le Start du slot (machine STE). Un départ à End ou au-delà donne le silence ; un sample en boucle reboucle sur ce départ, comme le STE recharge son adresse de début de trame. Cette ligne seulement. La granularité exacte de l'adresse sur le STE est **À CONFIRMER** |
| `Fxx` (ligne TUN) | Accord de la piste à partir de cette ligne, xx − 100 cents (de −99 à +99 dans la boîte ; ±100 d'un fichier précédent s'affiche ±99 et joue tel qu'écrit) ; une note tenue suit. Tient jusqu'au `Fxx` suivant ; Play repart de 0 |

Les commandes appartiennent à la ligne, pas à la note : supprimer ou déplacer une note les laisse en place. L'arpège `Xyz` et le glissé de hauteur `Pxy` sont retirés, les lignes ARP et SLD faisant leur travail ; un fichier qui en contient se charge sans eux.

### 5.6 Arpèges, glissés et gate

- **Arpèges** : l'arpégiateur de la piste (4.2) joue sur les notes du motif à partir d'un pas dont la ligne ARP est sur On jusqu'à un pas dont la ligne ARP est sur Off (chaque motif commence avec lui éteint), et sur les notes que vous jouez en direct. Sur un pas d'une note tenue, ARP On lance ou reprend l'arpège. Comme au clavier (4.2), chaque note du motif est une seule note de l'arpège : son enveloppe continue à travers les pas de l'arpège, la note suivante du motif la relance depuis l'attaque ; à la fin de la note, elle continue de parcourir l'arpège pendant son release.
- **Glissés** : SLD sur une note glisse vers la note suivante ; CRV règle la forme (Custom utilise la fenêtre Slide Curve). Les options Track et Land de l'Arpeggiator ajustent les arpèges pendant les glissandos. La matrice de hauteur de la fenêtre Modulation décide ce que les glissés font bouger (carré, timer, enveloppe).
- **Gate** : le curseur Gate règle la longueur des notes en proportion du pas pour la piste courante ; avec **All**, un même gate s'applique à toutes les pistes. La ligne GAT le remplace pas par pas.

### 5.7 Longueur de piste, Euclid, rotation et transposition

- **Len =** donne à la piste courante sa propre longueur (1 à 64 pas) : elle boucle seule pendant que les autres continuent (polymètres). Sa boîte part de la longueur que joue la piste. **Global** suit à nouveau la longueur du motif.
- **Euclid** ouvre le générateur euclidien : **Steps** (2 à 32), **Pulses** (coups répartis aussi régulièrement que possible), **Rotate**, **Accents** (une seconde figure augmente le volume de ces coups), la note, l'emplacement ou la percussion à écrire, **Merge** (garder ce qui est déjà écrit), **Rhythms** (un menu de 23 rythmes nommés, de E(2,3) Rumba à E(13,24) Aka pygmies, qui règle Steps et Pulses ; un rythme plus long que la piste est grisé) et **Apply**. Quand la figure remplacerait ou retirerait des notes déjà écrites, Apply demande d'abord ; le générateur reste ouvert.
- **<** / **>** font tourner la piste ; **-1** / **+1** et **Oct-** / **Oct+** la transposent.

### 5.8 Cadencement

- **Cadencement BPM** (par défaut) : le menu Resolution règle la valeur d'un pas : 1/4, 1/8, 1/16 (par défaut), 1/32, 1/64, 1/4T, 1/8T, 1/16T, 1/32T. Swing décale un pas sur deux. En 1/32T, un pas dure deux impulsions de l'horloge MIDI. En 1/64, un pas dure une impulsion et demie de l'horloge MIDI : avec Clock MIDI ext, un pas sur deux commence entre deux impulsions, à la moitié de la durée mesurée d'une impulsion ; avec Quantize to VBL, les pas tombent sur les trames de replay (à 120 BPM et 50 Hz, un pas de 1/64 dure 31 ms, une ou deux trames).
- **Cadencement Ticks** (Resolution > Ticks: Speed ticks per line) : chaque ligne dure **Speed** ticks de replay (1 à 31, à la fréquence VBL) plus le **Groove** (jusqu'à 16 valeurs, 0 à 15 ticks ajoutés aux lignes successives), comme dans les trackers. En Free run, le champ BPM affiche « ticks » et le tempo qui en résulte ; Swing est remplacé par le groove. En Host sync, les lignes suivent les temps de l'hôte selon la résolution.

Choisir une autre résolution alors que les motifs contiennent des notes ouvre une boîte qui demande si les notes la suivent. **Fit** place chaque note sur le pas de la nouvelle grille le plus proche de son instant (un demi-pas s'arrondit au pas suivant) et met à l'échelle sa longueur, les valeurs de pas écrites sur elle, les accords, la longueur des motifs, la ligne BPM, les longueurs propres des pistes et les silences du Song ; quand deux notes d'une ligne tombent sur le même pas, la première reste. Fit s'annule en une étape. **Keep** laisse chaque note sur son pas, qui dure désormais la nouvelle valeur. **Cancel**, par défaut (Entrée et Échap), garde la résolution. Sans notes, ou avec la même résolution, aucune boîte ne s'ouvre.

### 5.9 Horloges et synchronisation sur l'hôte

| Horloge | Où | Comportement |
|---|---|---|
| **Host sync** | Plugin (VST3, CLAP, LV2) | Le transport de l'hôte démarre et arrête le séquenceur et règle sa position, son tempo et sa signature rythmique. Le bouton Play et Space ne font rien ; le champ BPM affiche « host » et le tempo de l'hôte |
| **Free run** | Application autonome | L'horloge propre du séquenceur : Play/Stop (Space), BPM 60 à 240, commandes `Txx` |

En Host sync, le pas k du motif commence à la noire k / (pas par noire) à partir du début de la timeline de l'hôte ; le motif se répète à chaque longueur de motif ; le morceau enchaîne ses entrées depuis le début. Quand vous lancez la lecture ou faites un saut (boucle, repositionnement), les notes tenues du séquenceur sont relâchées et le séquenceur est replacé. **Avec Loop désactivé, le motif (ou le morceau) est joué une fois à partir du début de la timeline, puis reste silencieux** jusqu'à ce que l'hôte revienne en arrière. Le tempo de l'hôte remplace le tempo propre à chaque entrée du morceau. Les arpèges réglés sur Clock MIDI ext suivent le temps de l'hôte (24 impulsions par noire).

MIDI Start, Stop, Continue, Song Position Pointer et l'horloge MIDI ne pilotent pas le séquenceur en Host sync. Pour que le séquenceur suive l'horloge MIDI, réglez **Clock** (fenêtre Arpeggiator) sur **MIDI ext**, comme dans le prototype : le curseur BPM est grisé, Play arme le séquenceur, MIDI Start le lance depuis le début, Continue et Song Position Pointer reprennent à l'endroit demandé, MIDI Stop l'arrête, et le champ BPM affiche « ext » et le tempo mesuré. Changer Clock pendant la lecture arrête celle-ci. Les exports hors ligne (WAV, stems, exports de registres) n'ont pas d'horloge MIDI à suivre : ils jouent au tempo propre du séquenceur.

### 5.10 Enregistrement

- **Rec** (transport) enregistre dans le motif les notes que vous jouez sur la piste courante, au pas en cours de lecture. Dans l'application autonome, armer Rec lance aussi la lecture ; l'arrêt le désarme.
- **Step** (saisie pas à pas) écrit chaque note que vous jouez au curseur, qui avance ensuite. Il a priorité sur Rec.

Les notes de votre clavier MIDI et du clavier à l'écran sont enregistrées. Avec le routage MIDI Per channel, seules comptent les notes reçues sur le canal de la piste courante.

### 5.11 Song

Ouvrir la fenêtre Song (bouton Song, Edit > Song, Ctrl+G) active le **mode Song** : le séquenceur joue le morceau au lieu du motif courant. Fermer la fenêtre désactive le mode Song.

- **Add** ajoute le motif courant à la fin (à nouveau : une répétition de plus ; Shift+Add : une nouvelle entrée).
- Cliquez sur une entrée pour la sélectionner (Shift : plusieurs) ; faites-la glisser pour la déplacer.
- Les entrées se lisent comme `A1x2` (motif A1, deux répétitions) ; `|:` marque le point de reprise ; `♪n` un tempo propre à l'entrée.
- Clic droit sur une entrée : Repeat x1, x2, x4, x8, x16 ; Duplicate ; Cut ; Copy ; Paste ; Move left ; Move right ; Remove ; Tempo... (20 à 300 BPM, vide = tempo du morceau) ; Insert rest ; Insert note-off ; Insert loop ; Length... (entrées silence et note-off, 1 à 256 pas) ; Loop back... (combien d'entrées une entrée de boucle rejoue, 1 à 255 ; Repeat règle combien de fois elle saute) ; Restart here / Restart from start (l'endroit où le morceau revient quand Loop est activé).
- **Rest** joue un silence ; **Note-off** relâche toutes les notes du séquenceur, puis un silence ; **Loop** (affiché `BackNxR`) revient N entrées en arrière, R fois.
- Touches (fenêtre Song au premier plan) : ←/→ déplacent, Home/End sautent au début ou à la fin, Ins ou A ajoute le motif courant après le curseur, Del supprime, D duplique, + et − changent les répétitions, X / C / V coupent, copient et collent, Enter fait sauter la lecture à cette entrée.
- Les boutons du transport sont les mêmes que dans le Step Sequencer ; Previous et Next passent d'une entrée du morceau à l'autre (pas en Host sync).

---

## 6. Instruments, Patches, Drum Kit, Digidrums et DMA

### 6.1 Instruments (Ctrl+T)

32 instruments ($01 à $20) stockés dans le projet. Un instrument est un son de piste : source, volume, enveloppe, générateur, vibrato, ADSR, table Modulation et séquences, arpège. Le niveau, le panoramique et le gate restent ceux de la piste qui le joue.

- Cliquez sur un emplacement utilisé pour le charger dans la piste courante ; cliquez sur un emplacement vide pour y stocker la piste courante (un nom vous est demandé, 16 caractères au plus).
- Clic droit : Store channel, Load into channel, Rename, Save to Patches, Load from Patches, Clear.
- Dans le Step Sequencer, la ligne **INS** joue une note avec un instrument (son menu se termine par Instruments..., qui ouvre cette fenêtre). La ligne apparaît dès que le projet a un instrument.
- **Program Change** n (1 à 32) sélectionne l'instrument n pour les notes jouées en direct (section 7.4).

### 6.2 Patches (Ctrl+P)

Une bibliothèque de sons de piste nommés, partagée par tous vos projets (le fichier `Patches.json`, section 9).

- **Save current** stocke le son de la piste courante sous un nom.
- Cliquez sur un patch pour le charger dans la piste courante (une étape d'annulation). Clic droit : Load, Rename, Overwrite, Delete.
- **Import** ajoute les patches d'un fichier de bibliothèque à la fin de la vôtre ; **Export** écrit votre bibliothèque dans un fichier.
- Si le fichier de bibliothèque ne peut pas être lu, STYM2149 le laisse inchangé et vous le signale.

### 6.3 Drum Kit (Ctrl+K)

Les huit instruments du générateur **Chipdrum**, communs à toutes les pistes et enregistrés dans le projet. Sur une piste Chipdrum, chaque instrument répond à une note :

| Instrument | Note | Réglages |
|---|---|---|
| Kick | C | Pitch (25–90), Sweep (20–300), Decay (4–20), Noise (0–100), Vol (0–100) |
| Snare | C# | Body (80–400), Tone (0–70), Decay (6–30), Vol |
| Clap | D | Space (6–20), Color (15–80), Tail (10–60), Vol |
| Hat | D# | Metal (0–100), Tune (60–160), Decay (15–120), Vol |
| Open Hat | E | Metal, Tune, Decay (3–30), Vol |
| Closed Hat | F | Metal, Tune, Decay (30–250), Vol |
| Tom 1 | F# | Pitch (60–200), Sweep (20–150), Decay (4–20), Noise, Vol |
| Tom 2 | G | Pitch (80–260), Sweep (20–150), Decay (4–20), Noise, Vol |

Les notes G# à B jouent à nouveau Kick à Hat. **Audition** fait entendre la percussion sélectionnée, **Reset** rétablit ses valeurs d'usine, **Kits** charge l'un des 13 kits d'usine (Classic, TR-606-ish, TR-909-ish, Hard EBM, Industrial, Lo-Fi 4bit, Chiptune, TR-808-ish, House, Techno, Hip-Hop, Trap, Drum'n'Bass). Le nom du kit affiche « Custom » dès que vous le modifiez. Le kit se reconstruit en direct pendant la lecture. File > Load / Save > Drum kit lit et écrit des fichiers de kit.

### 6.4 Digidrums (Edit > Samples > Digidrums)

Huit emplacements d'échantillons 4 bits joués par le timer d'une piste Digidrum, à leur fréquence à partir de leur note de référence. Dans le séquenceur, la ligne sur laquelle vous écrivez choisit l'emplacement ; en MIDI, c'est le réglage Slot de la piste.

- **Slot** 1 à 8 (clic : le jouer et le sélectionner). **Name** (16 caractères, pris du fichier chargé ; cliquez pour le taper). Le nom ouvre la ligne de texte au-dessus de la forme d'onde, ici et dans le cadre GENERATOR, et suit le numéro de l'emplacement dans les infobulles.
- **Rate** 2 000 à 20 000 Hz à la note de référence (préréglages 7800, 8000, 11025). La changer conserve les valeurs, donc la hauteur change ; un fichier se charge à cette fréquence.
- **Root** (la note qui joue l'emplacement à sa fréquence) et **Fine** (−100 à +100 cents).
- **Load...** (ou clic sur la forme d'onde) : un fichier audio (WAV, AIFF, FLAC, Ogg Vorbis), premier canal seulement, rééchantillonné à la fréquence, 3 secondes au plus. **Play** joue l'emplacement une fois, de Start à End, à travers la table de volume de la puce ; avec Loop, il répète cette partie jusqu'à un nouveau clic sur Play ou Loop ou la fermeture de la fenêtre, Play restant allumé pendant ce temps. **Clear** vide l'emplacement.
- **Start** et **End** : la partie de l'échantillon jouée (End est la valeur qui suit la dernière jouée), avec ou sans Loop ; charger un fichier les règle sur l'échantillon entier. La forme d'onde grise ce qui est en dehors. Les exports YM6 et SNDH écrivent cette partie.
- **Loop** : répète Start à End tant que la note est tenue, puis joue jusqu'à End.
- **4-bit** : **Linear** ou **YM levels** (valeurs calées sur les niveaux de sortie mesurés de la puce, pour que l'échantillon garde sa forme). La source 8 bits est conservée, vous pouvez donc refaire la conversion.
- **Tools** sur la source 8 bits : Normalize, Reverse, Fade out, **Sign** (inverse le bit de poids fort de chaque octet : signé ↔ non signé), −3 dB, +3 dB. Reverse retourne aussi Start et End, pour que la partie jouée soit le même son lu à l'envers. −3 dB juste après un +3 dB qui a écrêté rend les crêtes écrêtées : les pas qui se suivent sont calculés à partir des valeurs d'avant le premier.
- **Banks** : enregistrer les 8 emplacements en banque nommée, charger ou supprimer une banque. La bibliothèque de banques (`Digidrums.json`) est partagée par tous les projets.

Charger un fichier ou une banque ne peut pas être annulé ; les autres réglages le peuvent.

### 6.5 DMA (Edit > Samples > DMA (STE))

Le son DMA du STE joue huit échantillons 8 bits signés de 32 KB chacun. Il demande Computer STE : avec Computer ST l'entrée du menu est grisée, et une fenêtre restée ouverte l'indique et propose **Use STE**. Avec STE, les pistes DMA (Authentic 3ch, Dual chip) ou les pistes de type DMA (16 ch) jouent ces échantillons, chaque piste choisissant son échantillon dans le cadre GENERATOR.

La fenêtre est organisée comme celle des Digidrums : l'emplacement à gauche, son échantillon à droite.

- **Slot** 1 à 8. **Name** (16 caractères, pris du fichier chargé ou, après un import XM, du sample ou de son instrument ; cliquez pour le taper). Le nom ouvre la ligne de texte au-dessus de la forme d'onde, ici et dans le cadre GENERATOR, et suit le numéro du sample dans les infobulles et dans le texte du cadre GENERATOR.
- **Rate** : 6.3k, 12.5k, 25k, 50k (6 258, 12 517, 25 033, 50 066 Hz). La changer conserve les octets, donc la hauteur et la durée changent, comme sur le STE. Avec **Resample** allumé (sous Rate ; une préférence, éteinte par défaut), une nouvelle fréquence rééchantillonne le sample à la place : il garde sa durée et sa hauteur, Start et End restent aux mêmes instants, 32 KB au plus ; une étape d'annulation.
- **Root** : la note qui joue l'échantillon à sa fréquence, et **Fine** (−100 à +100 cents), qui accorde le sample à partir d'elle, aperçu et export SNDH compris.
- **Load...** (ou clic sur la forme d'onde) : un fichier audio (rééchantillonné à la fréquence) ou des octets 8 bits bruts (`.raw`, `.spl`, `.sam`). **Play** joue le sample une fois de Start à End ; avec Loop, il répète cette partie jusqu'à un nouveau clic sur Play ou Loop ou la fermeture de la fenêtre, Play restant allumé pendant ce temps. **Clear** vide l'emplacement : sample, nom, Start et End.
- **Start** et **End** (en trames) : la partie du sample jouée, avec ou sans Loop (End est la trame qui suit la dernière jouée) ; charger un fichier les règle sur le sample entier. La forme d'onde grise ce qui est en dehors. L'export SNDH écrit le sample jusqu'à End.
- **Loop** : répète Start à End tant que la note est tenue ; sinon la partie joue une fois, jusqu'à End.
- **Format** : **Mono** (un octet par trame ; un fichier stéréo est mixé en mono) ou **Stereo** (octets entrelacés gauche, droite ; 16 KB par côté au plus). La forme d'onde d'un sample stéréo montre ses deux canaux, marqués **L** et **R** en haut à droite.
- **Tools** : Normalize, Reverse, Fade out, **Sign** (inverse le bit de poids fort de chaque octet : signé ↔ non signé), −3 dB, +3 dB, comme dans la fenêtre Digidrums. Avec un sample stéréo, **L** et **R** choisissent les canaux traités (les deux allumés par défaut, au moins un) ; Normalize sur les deux canaux les monte d'un même gain, en gardant leur balance.
- **Banks** (sous Fine) : enregistrer les 8 samples (réglages, octets et noms) en banque nommée, charger ou supprimer une banque. La bibliothèque de banques (`DMA.json`) est partagée par tous les projets.

Charger un fichier ou une banque ne peut pas être annulé.

---

## 7. MIDI

### 7.1 Routage

MIDI routing, dans le cadre MIDI d'Options > Settings :

| Routage | Les notes reçues jouent |
|---|---|
| **Omni** (par défaut) | La piste courante (paramètre d'hôte Focus Track, 1 à 16) |
| **Layer** | Toutes les pistes activées à la fois (aussi le bouton Layer) |
| **Per channel** | Le canal MIDI n joue la piste n |

Le clavier à l'écran joue toujours la piste courante. Le canal d'une piste DMA ne va qu'à cette piste, quel que soit le routage.

### 7.2 Vélocité, pitch bend, molette de modulation, aftertouch et MPE

Ces réglages sont dans le cadre MIDI d'Options > Settings (section 3.12). Les percussions (Chipdrum, Digidrum) ignorent la molette, l'aftertouch et le slide MPE.

- **Velocity** : **Accent** (par défaut : un gain audio en 16 ch, le volume 4 bits en Authentic 3ch), **Vol 4-bit** (le volume 4 bits dans tous les modes) ou **Off**.
- **Pitch bend** : ± Bend range (0 à 24 demi-tons, 2 par défaut). **Bend** choisit ce qu'il pilote : **Pitch** (par défaut), **Volume** (le centre laisse le volume 4 bits tel quel ; le haut le monte vers 15, le bas le descend vers 0) ou **Envelope** (la fréquence d'enveloppe bouge des demi-tons du bend, la note ne bouge pas : un balayage du timbre en Sync Buzzer et avec les sources Env).
- **Molette de modulation** (CC 1) : **Wheel** choisit ce qu'elle pilote, de rien en bas à tout en haut : **Vibrato** (par défaut, jusqu'à Wheel depth, 0 à 200 cents, 50 par défaut), **Volume** (le volume 4 bits monté vers 15), **Envelope** (la fréquence d'enveloppe, jusqu'à une octave plus haut), **LFO** (la profondeur du LFO de la piste : rien en bas, la profondeur réglée dans la fenêtre Modulation en haut) ou **Off**.
- **Aftertouch** : la pression des touches, de tout le canal (channel pressure) ou de chaque note (aftertouch polyphonique) ; une note prend la plus forte des deux. **Aftertouch** offre les mêmes choix que Wheel (Vibrato par défaut). Quand la molette et l'aftertouch pilotent la même chose, la plus forte des deux joue.
- **MPE** (16 ch seulement) : avec MPE sur On, le canal 1 joue comme d'habitude et ses contrôleurs agissent sur toutes les notes, tandis que les canaux 2 à 16 jouent chacun leur note, que leur pitch bend (± **MPE bend range**, 48 demi-tons par défaut), leur pression (le choix Aftertouch) et leur CC 74 (**MPE slide**) bougent seule. MPE slide pilote le **Volume** (par défaut : 64 laisse le volume 4 bits tel quel, 127 donne 15, 0 donne 0), l'**Envelope** (une octave dans chaque sens) ou le **Vibrato** (de 64 jusqu'à Wheel depth à 127). Les notes vont à la piste courante (ou à toutes les pistes en Layer) ; le routage Per channel ne s'applique pas.

### 7.3 Contrôleurs

Les contrôleurs agissent sur la piste courante (ou, en routage Per channel, sur la piste du canal) :

| CC | Effet |
|---|---|
| 1 | Molette de modulation (Wheel : vibrato par défaut) |
| 7 | Niveau Master |
| 70 | Tone on/off (≥ 64 = on) |
| 71 | Noise on/off |
| 72 | Période de bruit |
| 73 | Mode de volume (Vol 4-bit / Envelope) |
| 74 | Période d'enveloppe (avec MPE sur On, sur les canaux 2 à 16 : le slide MPE de leur note) |
| 75 | Forme d'enveloppe |
| 76 | Générateur (les cinq premiers types) |
| 77 | Timer ratio |
| 78 | SID min vol |
| 80 | Arpégiateur on/off |
| 81 | Vitesse d'arpège |
| 82 | Play du séquenceur (≥ 64) / Stop (sans effet en Host sync) |
| 83 | Tempo libre (60 à 240) |
| 84 | Piste courante |
| 120, 123 | All notes off |
| 121 | Remise à zéro du pitch bend, de la modulation et de la pression du canal (avec MPE sur On, sur les canaux 2 à 16 : leur bend, leur pression et leur slide) |

Un contrôleur appris avec MIDI learn (7.6) a priorité sur ce tableau. Les SysEx sont ignorés ; l'aftertouch est à la section 7.2.

### 7.4 Program Change

Program Change n sélectionne l'instrument n ($01 à $20, n = 1 à 32) pour les notes jouées ensuite, sur la piste du canal (Per channel), la piste courante (Omni) ou toutes les pistes (Layer). Le programme 0 rend à la piste son propre son ; les valeurs au-dessus de 32 sont ignorées. Les hôtes qui numérotent les programmes à partir de 1 affichent n + 1. Un pas du séquenceur dont la ligne INS nomme un instrument garde la priorité. Le choix n'est pas enregistré dans le projet (l'hôte renvoie son Program Change à la lecture). Les pistes de percussion n'ont pas d'instruments.

### 7.5 Horloge MIDI

- Les arpégiateurs en **Clock: MIDI ext** avancent sur l'horloge MIDI à la division choisie (dans un DAW, sur le temps de l'hôte).
- Le séquenceur suit le transport de l'hôte dans un DAW et sa propre horloge dans l'application autonome (5.9).

### 7.6 MIDI learn

1. Options > **MIDI learn**. Un bandeau en bas à gauche affiche « MIDI learn: click a slider or button, then move a control... ».
2. Cliquez sur un curseur ou un bouton de STYM2149 (le clic l'arme sans le modifier).
3. Tournez un potentiomètre, ou appuyez sur un pad ou une touche de votre contrôleur. Le bandeau affiche l'association, par exemple `CC74 -> [ef]`.
4. Cliquez sur une autre commande, ou quittez avec Options > **Stop MIDI learn**.

Un curseur appris suit le contrôleur sur toute sa plage ; un bouton appris est enfoncé quand la valeur monte au-delà de 64 (une note compte pour 127, son relâchement pour 0). Une note apprise ne joue plus de son. Les messages de mode de canal (CC 120 à 127) ne sont jamais appris. Le canal est ignoré.

La table MIDI est une préférence partagée par toutes les instances et tous les projets (section 9). **Clear MIDI map** la vide. Les commandes apprises ne répondent que tant que la fenêtre du plugin est ouverte ; un volume du Mixer appris fonctionne avec la fenêtre Mixer fermée.

### 7.7 Automation de l'hôte et paramètres

STYM2149 expose 554 paramètres d'hôte : 42 paramètres globaux (Master, Voice Mode, Chip Clock, Output Chain, Resampling, STE Bass, STE Treble, VBL Rate, Soft Clip, Timer, Focus Track, MIDI Routing, Velocity, Bend Range, Wheel Depth, Arp Clock, Arp Division, Free Tempo, Swing, Gate, Gate All, Computer, Chip Clock Hz, Arp Sync, Master 2, Master Pan, Master Link, Delay On, Delay Sync, Delay Division, Delay Time, Delay Feedback, Delay Hi Cut, Delay Level, Delay Ping-Pong, Tuning A4, Bend Target, Wheel Target, Aftertouch, MPE, MPE Bend Range, MPE Slide) et 32 par piste, nommés « T1 Level », « T2 Env Period », etc. (On, Level, Pan, Mute, Solo, Tone, Noise, Noise Period, Volume, Vol Mode, Env Period, Env Shape, Generator, Timer Ratio, SID Min Vol, Vibrato Depth, Vibrato Rate, Attack, Decay, Sustain, Release, ADSR Mode, Voicing, Arp On, Arp Rate, Gate, Vibrato On, Send, Level R, Mute R, Solo R, Link). Les douze réglages MIDI (MIDI Routing, Velocity, Bend Range, Wheel Depth, Arp Clock, Arp Division, Bend Target, Wheel Target, Aftertouch, MPE, MPE Bend Range, MPE Slide), Chip Clock Hz (la fréquence de l'horloge Custom) et Tuning A4 ne sont pas automatisables.

Les motifs, le morceau, les instruments, les séquences, le kit et les échantillons ne sont pas des paramètres : ils sont enregistrés dans l'état du projet.

### 7.8 Sorties

En plus de la sortie stéréo principale, le plugin propose 16 sorties mono de piste (« Track 1 » à « Track 16 ») et 2 sorties mono de puce (« Chip 1 », « Chip 2 »), désactivées par défaut. Activez-les dans votre hôte. Une sortie de piste transporte la piste seule après son mixeur (niveau, muet, solo), sans panoramique ; une sortie de puce transporte chaque puce dans les modes à 3 voix et reste silencieuse en 16 ch. Une sortie activée pendant que le plugin tourne reste silencieuse jusqu'à ce que l'hôte prépare à nouveau le plugin. Le plugin n'a pas d'entrée audio. Il annonce une queue de relâchement de 2 secondes et une petite latence avec le rééchantillonnage Sinc.

---

## 8. Fichiers

Les boîtes de dialogue de fichiers sont celles de votre système. Elles s'ouvrent dans le dernier dossier que vous avez utilisé.

### 8.1 Fichiers de projet (JSON)

| Fichier | Nom par défaut | Contenu |
|---|---|---|
| Preset | `stym2149-preset.json` | Le projet entier, y compris les instruments, les séquences, le morceau, les échantillons DMA et Digidrum. Le charger arrête toutes les notes et ne peut pas être annulé |
| Pattern | `stym2149-pattern.json` | Un motif (16 pistes) ; se charge dans le motif courant |
| Song | `stym2149-song.json` | Le morceau et les 32 motifs |
| Drum kit | `stym2149-kit.json` | Le kit Chipdrum |
| Bibliothèque de patches | `stym2149-patches.json` | Import / Export de la fenêtre Patches |

**Save** réécrit le dernier fichier utilisé pour ce type (et le demande la première fois) ; **Save as** demande toujours. Un fichier écrit par une version plus récente de STYM2149 est lu pour ce que cette version connaît, et un message le signale.

### 8.2 Import

| Import | Accepte | Résultat |
|---|---|---|
| YM dump | `.ym .ym2 .ym3 .ym5 .ym6 .snd .sndh` : YM2 à YM6 (compressés LHA ou non) et SNDH | Joué dans le YM Player (4.5) |
| MIDI file | Fichier MIDI standard, format 0 ou 1, 8 MB au plus | Motifs et morceau (ci-dessous) |
| XM module | Fasttracker II XM 1.04, 64 MB au plus | Motifs, morceau, instruments et échantillons DMA (ci-dessous) |

**Import MIDI.** Un pas vaut une double croche. Les motifs ont la longueur du motif courant, autant que nécessaire, 32 au plus (le reste est abandonné et signalé). Les canaux vont aux pistes du mode (les 16 en 16 ch ; pistes 1–3 en Authentic 3ch ; 1–3 et 5–7 en Dual chip), les canaux mélodiques d'abord, le canal 10 en dernier. Le canal 10 devient une piste Chipdrum (percussions General MIDI associées au kit) ; les autres canaux deviennent des pistes Chip Voice ; les notes longues sont liées. VOL reçoit la vélocité ramenée à 0–15, sur chaque pas de la note. Quand deux notes commencent ensemble sur un même canal, une seule est gardée (pas d'accords). Les motifs 1 à n sont remplacés et le morceau reçoit une entrée par motif ; le tempo du fichier est appliqué. L'import entier constitue une seule étape d'annulation.

**Import XM.** La table d'ordre du module devient le morceau (avec un point de reprise là où il boucle), ses motifs deviennent les motifs 1 à n (32 au plus). Computer passe à STE ; les canaux vont aux pistes qui jouent le son DMA (les 16 en 16 ch, réglées sur le générateur DMA ; pistes 5–12 en Authentic 3ch ; 9–16 en Dual chip). Les 8 instruments les plus joués qui ont des échantillons deviennent les instruments 1 à 8 et les échantillons DMA 1 à 8 (8 bits, 32 KB, fréquence STE la plus proche). Si le tempo du module est constant et correspond à 125, 150, 175 ou 250 BPM, l'import utilise le cadencement Ticks à 50, 60, 70 ou 100 Hz ; sinon le cadencement BPM, où chaque entrée du morceau reçoit le tempo de sa position et un changement de tempo au milieu d'un motif (`Fxx`) devient une commande `Txx` sur sa ligne. Les effets qui ont une commande en deviennent une sur leur ligne : `Axy` → `Axy` (jusqu'à 15 ; un glissé XM dure sa ligne, la ligne suivante reçoit donc `A00`) ; `9xx` → `Oxx` (mis à l'échelle de la fréquence DMA du sample) ; `EDx` → `Dxx` ; `ECx` → `Cxx` ; `E5x` → `Fxx` pour sa note. L'arpège `0xy` et les portamentos `1xx` et `2xx` sont laissés de côté. Les pas d'une note tenue portent son volume (ligne VOL). Un message liste ce qui n'a pas pu être importé.

### 8.3 Export

| Export | Motif | Morceau | Format |
|---|---|---|---|
| MIDI | ✓ | ✓ | SMF type 1, 96 ticks par noire : ce que joue le séquenceur (voix d'accord, redéclenchements et notes d'arpège compris). Percussions sur le canal 10 avec des notes General MIDI ; une piste MIDI par piste jouée |
| WAV | ✓ | ✓ | 16 bits, 24 bits ou 32 bits flottant ; 44,1, 48, 88,2, 96, 176,4, 192 kHz ou la fréquence de l'hôte (votre choix est mémorisé) |
| Stems (ZIP) | ✓ | ✓ | Un WAV par piste qui joue au moins une note, `stym2149-track-<track>.wav`, dans un ZIP |
| VBL dump | | ✓ | Trames de registres en source assembleur (`.s`, `.asm`) |
| YM3 | | ✓ | Dump de registres YM3 non compressé |
| YM6 | | ✓ | YM6 compressé LHA, avec effets de timer |
| SNDH | | ✓ | SNDH pour Atari ST ou STE : sous-morceau 1 = le morceau, sous-morceau 2 = le motif courant ; avec Computer STE, le son DMA est inclus |

- Les exports de morceau demandent des entrées dans le morceau (« Song is empty. Add entries first. »). Les exports tournent en arrière-plan ; un seul à la fois.
- Les exports audio sont calculés hors ligne : ils s'arrêtent exactement quand le séquenceur s'arrête, sans queue de relâchement, et sont coupés à une heure de musique (ou à la limite de 4 GB du WAV et du ZIP).
- Les **exports de registres** (VBL, YM3, YM6, SNDH) demandent **Authentic 3ch** (une puce, trois voix). Ils capturent ce que vous entendez, une trame par tick de replay. Après l'export, un message liste ce que le fichier ne reproduit pas, par exemple : effets de timer figés (VBL, YM3), players YM3 qui supposent 2 MHz et 50 Hz, périodes recalculées pour 2 MHz (SNDH), son DMA absent (formats qui ne l'ont pas). Une SID wave autre que Base (Square, Sine, Bounce, Custom) entre dans le fichier comme l'onde carrée simple, à la hauteur de la note.
- Le Recorder (4.4) exporte sa prise en YM6, SNDH ou VBL dump.

### 8.4 Conformité à la puce

L'indicateur **CHIP** de la barre de menus compte ce qu'un export de registres ne reproduirait pas dans le motif courant : plus de trois pistes avec des notes, Dual chip, accords sur une seule voix, notes hors de la plage de périodes de la puce, panoramique et niveaux du mixeur (le ST est mono et n'écrit que le volume 4 bits), un niveau Master différent de 70, et les limites des Digidrums ou du DMA. Cliquez dessus pour lire la liste. « CHIP: OK » signifie que les exports VBL, YM3, YM6 et SNDH correspondront à ce que vous entendez.

---

## 9. Préférences

Les préférences sont à vous, pas à un projet : toutes les instances de STYM2149 les partagent, et chacune se règle depuis l'interface et est enregistrée immédiatement.

| Préférence | Se règle avec |
|---|---|
| Taille de la fenêtre | En redimensionnant la fenêtre |
| Infobulles | Options > Settings > Tooltips (On, Off) |
| Langue des infobulles | Options > Settings > Language : English, intégré, puis les langues des fichiers PO par nom (Deutsch, Español, Français, Italiano, Português avec les installeurs). Par défaut, la langue de votre système si le menu l'a, sinon l'anglais. Les installeurs mettent les cinq langues dans un dossier de l'ordinateur (`C:\ProgramData\STYM2149\Languages` sous Windows, `~/.local/share/STYM2149/Languages` sous Linux, `/Library/Application Support/STYM2149/Languages` sous macOS, où on les copie à la main tant qu'il n'y a pas d'installeur macOS) ; un plugin copié sans elles n'affiche que l'anglais. Votre propre dossier Languages, à côté du fichier de préférences (`%APPDATA%\STYM2149\Languages`, `~/Library/Application Support/STYM2149/Languages`, `~/.config/STYM2149/Languages`), contient un fichier PO de gettext par langue (`nl.po`), fait avec un éditeur de texte ou un outil de traduction comme Poedit ; le menu relit les deux dossiers à chaque ouverture. **Languages folder...**, sous Language, écrit un modèle avec toutes les lignes anglaises et un README dans votre dossier, puis l'ouvre. Un de vos fichiers au nom d'une langue installée (`fr.po`) corrige ses lignes. Les traductions attendent encore une relecture par des locuteurs natifs (**À CONFIRMER**) |
| Disposition du clavier | Options > Settings > Keyboard layout : Auto (detect), QWERTY, AZERTY, QWERTZ. Auto suit la disposition de votre système (Windows, Linux ; macOS **À CONFIRMER**) |
| Noms des notes | Options > Settings > Note names : English (C D E) ou Latin (Do Re Mi) |
| Renderer | Options > Settings > Renderer (Windows seulement, grisé ailleurs) : Software (par défaut) ou Direct2D, la façon dont la fenêtre arrive à l'écran. Software ne recopie que ce qui change ; avec Direct2D, une copie de la fenêtre pouvait clignoter quelques secondes en haut à droite du bureau Windows |
| Pixels | Options > Settings > Pixels : Smooth (par défaut) ou Sharp (section 3.1) |
| Oscilloscope | Options > Oscilloscope : On (par défaut) ou Off (section 3.10) |
| Register monitor | Options > Register monitor : On (par défaut) ou Off (section 3.10) |
| Note monitor | Options > Note monitor : On (par défaut) ou Off, la ligne Note du cadre SYSTEM (section 3.9) |
| Table MIDI learn | Options > MIDI learn, Clear MIDI map |
| Lignes de commandes | Step Sequencer > Resolution > Step command rows : On (par défaut) ou Off (CMD, ENV, VIB, LFO, MOD, TUN, DLY, CUT, SMP, BPM) |
| Icônes du bureau | Leurs places sur le bureau, en les faisant glisser (section 3.1) |
| Format et fréquence WAV | La boîte de dialogue de l'export WAV |
| Dernier dossier | Mémorisé par les boîtes de dialogue de fichiers |

**Fichier de réglages** :

| Système | Fichier |
|---|---|
| Windows | `%APPDATA%\STYM2149\Preferences.settings` |
| Linux | `~/.config/STYM2149/Preferences.settings` |
| macOS (à partir de 1.0.0) | `~/Library/Application Support/STYM2149/Preferences.settings` |

La bibliothèque Patches (`Patches.json`), les banques Digidrum (`Digidrums.json`) et les banques DMA (`DMA.json`) sont stockées à côté. Les versions antérieures à 0.9.0 nommaient ce fichier `STYM2149.settings` ; le premier démarrage de la 0.9.0 en recopie vos préférences.

L'application autonome garde ses périphériques audio et MIDI et votre dernière session dans un fichier à part, `STYM2149.settings` : dans `%APPDATA%\STYM2149` sous Windows, `~/.config` sous Linux, `~/Library/Application Support` sous macOS. Quand vous quittez (File > Quit ou la case de fermeture de la fenêtre), elle y enregistre la session et la rouvre au démarrage suivant.

Pour revenir aux valeurs par défaut, fermez toutes les instances de STYM2149 et supprimez le fichier de réglages.

---

## 10. Raccourcis clavier et gestes de la souris

Les raccourcis fonctionnent tant que la fenêtre de STYM2149 a le focus clavier. Savoir si un hôte donné transmet toutes les touches (par exemple Space) au plugin est **À CONFIRMER** pour chaque DAW. ^X dans les menus signifie Ctrl+X.

### 10.1 Partout

| Touches | Action |
|---|---|
| Space | Play / Stop (Free run ; en Host sync, c'est le transport de l'hôte qui décide) |
| Esc | Fermer le menu ouvert ; sinon fermer la fenêtre au premier plan ; sinon arrêter toutes les notes (panique) |
| Tab / Shift+Tab | Passer la fenêtre ouverte suivante / précédente au premier plan (avec une seule fenêtre ouverte, la touche va à l'hôte) |
| 1 à 8 | Motif de la banque courante |
| Shift+1 à Shift+4 | Banque A à D |
| ← / → | Piste précédente / suivante (avec le Step Sequencer ouvert : déplacer le curseur de la grille ; Shift+← / → change toujours de piste) |
| M | Métronome on/off |
| Z / X | Octave du clavier vers le bas / vers le haut |
| Pavé numérique 0–9 | Octave du clavier (fenêtre Computer Keyboard ouverte) |
| A W S E D F T G Y H U J K O L P ; ' | Jouer des notes (fenêtre Computer Keyboard ouverte ; positions QWERTY) |
| Ctrl+Z | Annuler |
| Ctrl+Y, Ctrl+Shift+Z | Rétablir |
| Ctrl+E / M / G / P / T / K / B / R / F | Step Sequencer / Mixer / Song / Patches / Instruments / Drum Kit / Arpeggiator / Modulation / Effects |
| Ctrl+V / S / U / C / I / D | Générateur : Chip Voice / SID Voice / Sync Buzzer / Chipdrum / Digidrum / DMA (Computer STE) |
| Ctrl+/ ou Ctrl+? | Fenêtre SHORTCUTS : la liste des raccourcis du prototype, puis ceux qu'ajoute STYM2149 (aussi dans le menu Atari > Shortcuts) |
| Ctrl+N | Nouveau projet |
| Ctrl+Q | Quitter (application autonome seulement) |

Avec le Step Sequencer au premier plan, Ctrl+C, Ctrl+X et Ctrl+V agissent sur la note sous le curseur ou sur la sélection. Quand il n'y a rien à copier ou à coller à cet endroit, ils reviennent aux raccourcis de générateur ci-dessus (Ctrl+C : Chipdrum, Ctrl+V : Chip Voice).

La répétition des touches (touche maintenue) ne fonctionne que pour les flèches, Delete et Backspace, avec le Step Sequencer ouvert.

### 10.2 Step Sequencer ouvert

| Touches | Action |
|---|---|
| ← → ↑ ↓ | Déplacer le curseur de la grille |
| Enter | Écrire ou supprimer une note au curseur |
| Delete, Backspace | Effacer le pas au curseur |
| `]` / `[` | Allonger / raccourcir la note sous le curseur (fenêtre au premier plan) |
| `\` | Scinder au curseur la note sous le curseur |
| J | Joindre la note sous le curseur à la suivante |
| Ctrl+C / X / V | Copier / couper la note ou la sélection ; coller au curseur |
| Flèches, Shift+↑/↓ | Déplacer le cadre de sélection (d'un demi-ton, d'une octave) |
| Delete | Effacer le cadre de sélection |
| Esc | Désélectionner le cadre |

### 10.3 Autres fenêtres au premier plan

| Fenêtre | Touches |
|---|---|
| Song | ←/→ déplacer, Home/End, Ins ou A ajouter, Del ou Backspace supprimer, D dupliquer, + / − répétitions, X / C / V couper / copier / coller, Enter sauter à l'entrée |
| Arpeggiator (On, Table) | Les flèches déplacent le curseur, Enter écrit le pas |
| Modulation (On) | Les flèches déplacent le curseur, Enter écrit la hauteur, Delete réinitialise le pas, N active ou désactive le bruit |

### 10.4 Gestes de la souris

| Où | Geste | Action |
|---|---|---|
| Tout curseur | Glisser | Changer la valeur |
| Flèche d'un curseur horizontal | Clic, maintien | Un pas de moins ou de plus, répété tant que le bouton est maintenu |
| Champ de valeur | Clic, frappe, Enter | Taper une valeur |
| Fenêtre flottante | Glisser la barre de titre ; clic sur la case de fermeture | Déplacer ; fermer |
| Onglet de piste | Clic / nouveau clic / clic droit | Rendre courante / On-Off / Copy, Paste settings, Rename |
| Bouton de motif | Clic droit | Copy, Cut, Paste, transposition |
| Piano | Clic, glisser / clic droit | Jouer / choisir la piste jouée |
| Panoramique du Mixer | Double-clic | Centrer |
| Fader, panoramique, Master du Mixer | Clic droit | Value... (taper la valeur), Reset |
| Valeur du Mixer (sous un fader, un panoramique, un Send, un Master) | Clic, frappe, Enter | Taper la valeur sur place |
| Mini-oscilloscope du Mixer | Clic | Ouvrir le Scope de la piste |
| Grille du Step Sequencer | Voir 5.3 et 5.4 | Écrire, déplacer, redimensionner, sélectionner, menus |
| Grille du Step Sequencer | Molette de la souris | Faire défiler d'un demi-ton |
| Libellé d'une ligne de contrôle | Clic droit | Copy row / Paste row |
| Case GEN, SRC, ENV qui porte une valeur | Clic droit | Les réglages de la piste qu'elle met en jeu (5.4) |
| Case VOL | Clic droit | Curseur et Slide vers le pas suivant |
| Entrée du morceau | Clic, Shift+clic, glisser, clic droit | Sélectionner, sélection multiple, déplacer, menu de l'entrée |
| Grilles Arpeggiator, Modulation | Clic, glisser | Dessiner |
| Cellule VOL, PER, NP de Modulation | Clic droit | Curseur précis (clic droit dessus : retour à 15 ou 0) |
| PER, NP de Modulation ; barres de la SID wave (cadre GENERATOR) | Glisser, molette (Shift : ×10 sur PER) | Dessiner, ajuster |
| Barres de la SID wave (cadre GENERATOR) | Clic droit | Curseur précis (clic droit dessus : retour à 0) |
| Forme d'onde des fenêtres Digidrums et DMA | Clic | Charger un fichier |
| Toute commande pendant MIDI learn | Clic | L'armer pour l'apprentissage |

---

## 11. Confidentialité

STYM2149 n'ouvre aucune connexion réseau et n'envoie aucune télémétrie. Il n'échange de données qu'avec l'hôte (audio, MIDI, paramètres, état du projet), avec les fichiers que vous ouvrez ou enregistrez par le menu File et les fenêtres d'échantillons, et avec ses propres fichiers de réglages (section 9). Cela a été vérifié sur le code source et les bibliothèques, et à l'exécution sous Linux ; la vérification à l'exécution sous Windows est **À CONFIRMER**.

---

## 12. Dépannage

**Le DAW n'affiche pas STYM2149.**
- Vérifiez que les fichiers sont là où l'indique la section 1. Sous Windows, vérifiez que les dossiers de plugins de votre DAW comprennent `C:\Program Files\Common Files\VST3` (ou `…\CLAP`), puis relancez la recherche. Sous Linux, laissez l'hôte relancer la recherche.
- Utilisez un format que votre DAW prend en charge : il n'y a ni version VST2, ni AU, ni AAX dans la 0.9.0, et Ableton Live 11 n'a pas CLAP.
- STYM2149 est listé sous le fabricant « PRISM », comme instrument.

**Windows bloque l'installateur.** SmartScreen affiche « Windows a protégé votre ordinateur » parce que l'installateur n'est pas signé : cliquez sur **Informations complémentaires**, puis sur **Exécuter quand même**.

**Pas de son.**
- La piste courante doit être activée (son onglet), et les notes doivent être comprises entre B0 et C9.
- En routage Omni, le MIDI joue la piste courante ; en routage Per channel, le canal n joue la piste n.
- Vérifiez Mute et Solo dans le Mixer, ainsi que le niveau Master.
- Pendant que le YM Player joue un morceau, il remplace la sortie de l'instrument : fermez le YM Player.

**Le séquenceur ne joue pas dans mon DAW.** Lancez le transport du DAW (Play et Space dans le plugin ne font rien en Host sync). Assurez-vous que les pistes qui contiennent des notes sont activées. Si le motif joue une fois puis s'arrête, activez **Loop** dans le transport du Step Sequencer.

**Une sortie séparée est muette.** Les sorties activées pendant que le plugin tourne restent silencieuses jusqu'à ce que l'hôte prépare à nouveau le plugin (arrêt et redémarrage du moteur audio, ou rechargement du projet). Les sorties de puce sont silencieuses en 16 ch.

**Une entrée d'export est grisée.** VBL dump, YM3, YM6 et SNDH demandent Authentic 3ch. Les exports de morceau demandent des entrées dans le morceau.

**Les commandes MIDI apprises ne font rien.** Elles ne répondent que tant que la fenêtre du plugin est ouverte.

**CPU.** Le pire cas du moteur mesuré sur la machine de référence (96 voix avec du bruit sur chaque piste) est d'environ 2,5 % d'un cœur. Le dessin de l'éditeur est séparé de l'audio et coûte plus cher dans une grande fenêtre (une image de l'éditeur avec le motif de démonstration en lecture a été mesurée à 2,0 ms à 80 % sur un écran à 125 %, 6 ms à 160 %) : fermez la fenêtre du plugin ou réduisez-la si l'interface semble lente. Aux fréquences d'hôte au-dessus de clk/8, les rééchantillonnages Box et Retro se rabattent sur Sinc.

**Linux.** Les plugins et l'application demandent ALSA, FreeType, Fontconfig et un affichage X11. Si l'application ne démarre pas quand vous tapez son nom dans un terminal, `~/.local/bin` n'est probablement pas dans votre `PATH` : utilisez le chemin complet. La distribution minimale exacte est **À CONFIRMER**.

**Les notes s'arrêtent quand je change un réglage.** Changer le mode de voix, l'ordinateur (ST/STE) ou le générateur arrête les notes en cours ; c'est normal.

**Réinitialiser les préférences.** Fermez toutes les instances et supprimez le fichier de réglages (section 9).

---

## 13. Licences et crédits

STYM2149 est un logiciel libre sous **GNU Affero General Public License version 3** (AGPLv3) : vous pouvez le partager et le modifier selon ses termes (voir `LICENSE`).

Programming: Pascal "Indus" Nowak (with Claude's help) — Copyright © 2026 Indus / PRISM.

STYM2149 s'appuie sur :

| Composant | Auteur | Licence | Usage |
|---|---|---|---|
| AtariAudio | Leonard/Oxygene (Arnaud Carré) | MIT | Base de l'émulation de la puce ; machine Atari qui joue les fichiers SNDH |
| ym2149-rs | slippyex | MIT | Référence pour l'émulation |
| Musashi | Karl Stenerud | MIT | Émulateur 68000 utilisé pour jouer les fichiers SNDH |
| Décompresseur ICE 2.4 | Hans Wessels | Domaine public | Fichiers SNDH compressés |
| pylzh | Hirofumi Ukawa | Apache-2.0 | Décompression LHA des fichiers YM |
| Polices système et icônes du bureau d'EmuTOS | L'équipe EmuTOS | GPL (les polices : v2 ou ultérieure) | Polices GEM de l'interface ; icônes de disquette et de corbeille du bureau |
| JUCE 8 | Raw Material Software | AGPLv3 | Framework du plugin, audio, MIDI |
| zlib, libpng, libjpeg, HarfBuzz, SheenBidi, FLAC, Ogg Vorbis | (via JUCE) | Licences permissives | Compilés dans le plugin par JUCE |
| VST3 SDK, CLAP, clap-helpers, clap-juce-extensions, LV2, ASIO SDK | Steinberg, free-audio, projet LV2 | Voir leurs textes | Formats de plugin et pilotes audio Windows |

La fenêtre Information (menu Atari) affiche ces crédits. La liste complète des composants, des versions et des licences se trouve dans `THIRD_PARTY.md` ; les textes des licences sont dans le dossier `licenses` installé avec STYM2149.
