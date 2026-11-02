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
| FL Studio | VST3 ou CLAP | |
| Ableton Live | VST3 | Live ne prend pas en charge CLAP |
| Cubase | VST3 | |
| Studio One | VST3 ou CLAP | |
| ACID Pro | VST3 | |
| Maschine 3 | VST3 | STYM2149 n'a pas de version VST2 |
| Reason | VST3 | Pas de version Rack Extension |

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

Les notes hors de la plage de la puce (B0 à C9) ne jouent pas sur les pistes mélodiques, depuis un clavier MIDI comme depuis l'écran ; une piste de percussion les joue.

### 2.2 Le motif de démonstration

File > Demo pattern écrit un court motif dans le motif courant : une basse arpégée sur la piste 1, un lead SID sur la piste 2, un rythme Chipdrum sur la piste 3 (voix C en Authentic 3ch) et des stabs Sync Buzzer sur la piste 4 (16 et 32 ch seulement). Si les pistes 1 à 4 contiennent déjà des données, STYM2149 demande confirmation avant de les écraser.

Dans un DAW, le séquenceur suit le transport de l'hôte : lancez la lecture dans votre DAW pour entendre le motif (section 5.9). Dans l'application autonome, appuyez sur Space ou sur le bouton Play du Step Sequencer.

### 2.3 Modes de voix

Le sélecteur **Voices** du cadre SYSTEM (aussi un paramètre d'hôte) choisit comment les pistes sont réparties sur les puces. Son menu donne les quatre modes dans cet ordre ; 16 ch est le mode par défaut :

| Mode | Pistes | Usage typique |
|---|---|---|
| **Authentic 3ch** | Une puce YM : voix A, B, C plus une piste Digidrum (pistes 1 à 4) ; sortie mono | Musique qu'un vrai Atari ST peut jouer ; obligatoire pour les exports de registres (VBL, YM3, YM6, SNDH) |
| **Dual chip 2x3ch** | Deux puces : A1, B1, C1, Digi1, A2, B2, C2, Digi2 (pistes 1 à 8) | Deux puces YM |
| **16 ch** | 16 pistes, chacune avec son propre panoramique et sa propre polyphonie (jusqu'à 6 voix par piste) | Production ; son YM sans la limite de 3 voix |
| **32 ch** | 32 pistes, comme 16 ch | Arrangements plus larges, modules de plus de 16 canaux |

Le réglage **Computer** (ST ou STE) ajoute le son DMA du STE : avec STE, Authentic 3ch gagne huit pistes DMA (pistes 5 à 12), Dual chip gagne huit pistes DMA (9 à 16), et en 16 et 32 ch toute piste peut utiliser le générateur DMA.

Changer le mode de voix ou l'ordinateur arrête toutes les notes.

### 2.4 L'application autonome

L'application autonome fonctionne comme le plugin. Différences :

- Options > Audio/MIDI settings ouvre les réglages des périphériques audio et MIDI.
- L'indicateur MIDI de la barre de menus affiche « no inputs », le nom de l'entrée ou « n inputs ».
- Une entrée MIDI débranchée (ou désactivée dans les réglages) pendant que des touches sont tenues : ces notes sont relâchées, comme si les touches étaient lâchées ; leurs note off n'arriveraient jamais. Rebranchée, elle rejoue aussitôt. Dans un hôte, c'est l'hôte qui décide du sort des notes d'un contrôleur qui disparaît ; si l'une continue de sonner, Esc arrête toutes les notes (section 10).
- Le séquenceur tourne sur sa propre horloge (Free run) : Space, ou le bouton Play, le lance et l'arrête, et le curseur BPM règle sa vitesse.
- File > Quit (Ctrl+Q) ferme l'application après une confirmation ; la case de fermeture de la fenêtre du système (et Alt+F4 sous Windows), le menu de l'application sous macOS et la fin de la session posent la même question. Quitter n'enregistre rien : le travail non enregistré est perdu, comme le dit la boîte.
- File > Configuration (application autonome seulement) garde votre travail dans un fichier de configuration (`.stymcfg`) : **Load** ouvre une configuration (ou un projet `.stym`), **Save** réécrit la configuration enregistrée ou chargée en dernier (demande la première fois), **Save as** demande toujours. Une configuration contient ce qu'un projet de DAW garde pour STYM2149, dernière prise de l'enregistreur comprise. Au démarrage suivant, l'application rouvre la configuration enregistrée ou chargée en dernier, telle qu'elle était alors ; sans configuration, elle démarre avec un nouveau projet.

### 2.5 Enregistrer votre travail

- **Dans un DAW**, tout ce qui fait le son est enregistré dans le projet du DAW : paramètres, motifs, morceau, instruments, séquences, Drum Kit, échantillons Digidrum et DMA, noms des pistes, dernière prise de l'enregistreur.
- **Non enregistrés dans le projet** : positions des fenêtres, outil actif, historique d'annulation et presse-papiers. Ils durent aussi longtemps que l'instance du plugin (fermer et rouvrir la fenêtre du plugin les conserve). Les préférences (taille de la fenêtre, infobulles, table MIDI learn, etc.) sont propres à l'utilisateur et partagées par toutes les instances (section 9) ; chaque copie de l'application autonome garde sa propre table MIDI learn.
- **Nouveau** : File > New (Ctrl+N) repart d'un projet neuf : tous les paramètres, motifs, instruments, échantillons et la prise de l'enregistreur reviennent à leurs valeurs par défaut. Si quelque chose est déjà défini, STYM2149 demande confirmation. Les préférences et les positions des fenêtres restent.
- **Fichiers** : le menu File enregistre et charge des projets, des motifs, des morceaux et des kits de batterie sous forme de fichiers STYM2149, joue des fichiers sans toucher au projet, et importe et exporte d'autres formats (section 8).

---

## 3. La fenêtre principale

### 3.1 Le bureau GEM

La fenêtre est un bureau GEM de 1 344 × 984 pixels logiques, dessiné à la résolution de votre écran : à 100 % il mesure 840 × 615 pixels (les 5/8 du bureau), à 160 % 1 344 × 984, à 200 % 1 680 × 1 230, multipliés par l'échelle d'affichage du système. Quand un pixel du bureau tombe sur un nombre entier de pixels de l'écran, le dessin est exact au pixel près. Sinon le réglage Pixels (Options > Settings, section 3.12) décide : **Sharp** place chaque pixel des lettres et des icônes sur des pixels entiers de l'écran, net mais certains un peu plus larges que d'autres ; **Smooth** (par défaut) les dessine avec anticrénelage, comme un navigateur montre le prototype. Si les cadres et les traits sont flous eux aussi, pas seulement les lettres, c'est l'hôte qui étire la fenêtre du plugin : cherchez dans ses réglages la mise à l'échelle de l'affichage des plugins. Pour changer la taille, redimensionnez la fenêtre du plugin dans l'hôte (ou la fenêtre de l'application autonome), de 50 % jusqu'à la taille qui remplit votre plus grand écran (200 % au moins ; environ 700 % sur un écran 8K à l'échelle d'affichage 100 %) ; les proportions restent fixes et la taille atteinte est mémorisée. Une très grande fenêtre demande plus de temps processeur pour son dessin. Il n'y a pas de menu de zoom. Si vous n'avez jamais redimensionné la fenêtre, STYM2149 choisit une taille d'après votre écran principal (50 %, 75 %, 100 % ou 125 %).

La barre de menus court en haut du bureau, comme sur un bureau GEM (section 3.2) ; la fenêtre principale commence sous elle, et aucune fenêtre ne passe au-dessus. Les icônes des disquettes A et B se trouvent en haut à gauche du bureau, A au-dessus de B, celle de la corbeille en bas à droite, nommées dans la langue de l'interface et avec les images du bureau d'EmuTOS : une disquette 3,5 pouces portant la lettre du lecteur sur son étiquette, une poubelle. Elles sont dessinées deux fois plus grandes que sur l'Atari ST, leurs noms et les cases blanches sous elles aussi. Un clic sur la corbeille est File > New, avec sa confirmation ; A et B sont seulement décoratives, un clic ou un double-clic sur elles ne fait rien. Maintenez le bouton sur une icône pour faire glisser son contour pointillé ailleurs : comme sur un bureau GEM, il saute de case en case d'une grille invisible de la taille des icônes, et une icône ne se pose pas sur la case d'une autre (sa place est une préférence, section 9).

Les fenêtres flottantes s'ouvrent par-dessus la fenêtre principale. Faites glisser une fenêtre par sa barre de titre (la fenêtre principale aussi, qui garde sa place tant que l'instance du plugin vit) ; aucune fenêtre ne dépasse les bords du bureau, et celle qui grandit près de son bord droit ou bas, ou s'ouvre à une place gardée sur un bureau plus grand, revient à l'intérieur ; fermez-la avec la case de sa barre de titre ou avec Esc (Esc ferme la fenêtre au premier plan, sauf les fenêtres Computer Keyboard et Slide Curve). La plupart des fenêtres affichent le nom de la piste courante dans leur titre et suivent la piste courante. **Tab** passe la fenêtre du fond au premier plan, de sorte que les fenêtres ouvertes viennent devant tour à tour (la fenêtre principale prend son tour) ; Shift+Tab va dans l'autre sens. Un menu ouvert depuis un bouton se pose sur le filet du bas du bouton, sans espace, comme les menus GEM ; un bouton à ombre portée est un menu local : son menu s'ouvre avec le choix en cours par-dessus le bouton. Les éléments sont dessinés comme sur un Atari ST en haute résolution : menus à lignes de 16 pixels dans un cadre de 2 pixels, boutons au cadre de 2 pixels et de 3 pour le bouton par défaut (Entrée), éléments grisés tramés un pixel sur deux.

Les infobulles apparaissent quand vous laissez le pointeur sur une commande. Les réglages Tooltips et Tooltip language d'Options > Settings les activent ou les désactivent et choisissent leur langue ; Interface language choisit, à part, la langue des menus, boutons, libellés, titres de fenêtres et alertes (sections 3.12 et 9).

Les curseurs se font glisser à la souris. Vous pouvez cliquer dans la valeur à côté de chaque curseur et y taper une valeur (des soulignés marquent ses positions libres) ; appuyez sur Enter pour l'appliquer, sur Esc pour la laisser telle quelle. Une valeur hors limites est ramenée au bout du curseur. Une note (note de base, note fixe des séquences) se tape par son nom, dans l'une ou l'autre notation (`C#4`, `Sol3`), ou par son numéro de note MIDI. Un curseur horizontal (sauf les panoramiques du Mixer) a une flèche à chaque bout : un clic change la valeur d'un pas, et recommence tant que vous maintenez le bouton.

Les boîtes de dialogue gardent le clavier tant qu'elles sont ouvertes. Une confirmation avant une action irréversible (Clear, Demo pattern, New, Quit, suppression d'une prise ou d'une banque) affiche d'abord **Cancel**, encadré comme choix par défaut : Enter et Esc annulent tous les deux, quel que soit le bouton qui a le focus, et seul un clic sur l'action confirme. Dans une boîte où vous tapez un nom ou un nombre, Enter vaut OK et Esc vaut Cancel. Tab et Shift+Tab passent d'un bouton à l'autre (et au champ de texte).

### 3.2 Barre de menus

| Menu | Entrées |
|---|---|
| Menu Atari (Fuji) | **Information** (une démo d'après l'écran titre de ST BLIX, ou l'icône avec Options > Settings > Information demo > Off ; version, numéro de build et crédits ; Entrée la ferme) ; **Shortcuts** (^?) : la fenêtre SHORTCUTS, une liste déroulante de tous les raccourcis |
| **File** | New (^N) ; Load, Save, Save as (chacun : Preset, Pattern, Song, Drum kit) ; Play... ; Import (MIDI file, Tracker song, SNDH / YM file) ; Export pattern (MIDI, WAV, Stems (ZIP)) ; Export song (VBL dump, YM3, YM6, SNDH, maxYMiser, YM-Tracker, TTrak, MIDI, WAV, Stems (ZIP)) ; Recorder ; Demo pattern ; Configuration (Load, Save, Save as ; application autonome seulement) ; Quit (^Q, application autonome seulement) |
| **Edit** | Step Sequencer (^E), Arpeggiator (^B), Mixer (^M), Song (^G), Modulation (^R), Effects (^F), Patches (^P), Drum kit (^K) ; les générateurs de la piste courante (Chip Voice, SID Voice, Sync Buzzer, FM Voice, Chipdrum, Digidrum, DMA) avec leurs touches Shift+Ctrl, le générateur en cours coché ; Undo (^Z), Redo (^Y) ; Instruments (^T), Samples (Digidrums, DMA (STE)) |
| **Options** | Audio/MIDI settings (application autonome seulement) ; MIDI learn / Stop MIDI learn ; Clear MIDI map ; Oscilloscope, Register monitor, Note monitor et MIDI monitor, chacun coché quand il est affiché, un clic l'affichant ou le masquant ; Settings... (la fenêtre SETTINGS, section 3.12) |

Les entrées grisées expliquent pourquoi dans leur infobulle. Arpeggiator et Modulation sont grisés sur les pistes de percussion (Digidrum, Chipdrum). Drum kit (et Ctrl+K) ne sert qu'aux pistes Chipdrum. Samples > DMA (STE) est grisé avec Computer ST. L'onde SID et les pas du Sync Buzzer se règlent dans le cadre GENERATOR (section 3.8). Les exports de registres (VBL dump, YM3, YM6, SNDH) sont grisés sauf si le mode de voix est Authentic 3ch. ^X (couper), ^C (copier), ^V (coller) et ^A (tout sélectionner) agissent dans le Step Sequencer : sur sa sélection, ou sur la note sous son curseur.

Deux indicateurs se trouvent au bout droit de la barre de menus :

- **CHIP: OK** ou **CHIP: n** — conformité à la puce du motif courant. Cliquez dessus pour lister ce que les exports VBL, YM3, YM6 ou SNDH ne reproduiraient pas tel que vous l'entendez (section 8.4).
- **MIDI: host** dans un plugin (l'hôte envoie le MIDI) ; dans l'application autonome, l'état des entrées MIDI.

### 3.3 Onglets des pistes

Les onglets sélectionnent la **piste courante**, celle qu'éditent les panneaux du dessous et que joue le clavier à l'écran : 16 en 16 ch, Authentic 3ch et Dual chip, 32 en 32 ch. Les panneaux forment quatre colonnes : SOURCE et VIBRATO ; ENVELOPE, ADSR et ADSR ENVELOPE ; GENERATOR ; SYSTEM.

- Cliquez sur un onglet pour rendre sa piste courante. Cliquez à nouveau sur l'onglet de la piste courante pour activer ou désactiver la piste. Une piste désactivée est laissée de côté par le séquenceur et par Layer ; tant qu'elle est la piste courante, les touches (clavier à l'écran, clavier d'ordinateur, MIDI) la jouent quand même avec son propre son, pour que vous puissiez la régler avant de l'activer (sa LED s'allume tant qu'elle sonne).
- **All on** et **All off** activent ou désactivent toutes les pistes.
- Clic droit sur un onglet : **Copy settings**, **Paste settings** (le son, l'arpège et le niveau de la piste ; pas On, Pan, Mute ni Solo), **Rename...** (8 caractères ; un nom vide affiche le numéro de la piste). En Authentic 3ch et en Dual chip, les voix de la puce sont nommées d'après la puce (A, B, C, Digi…) et ne peuvent pas être renommées.
- En Authentic 3ch, les onglets qui suivent les quatre pistes de la puce sont inutilisés (ou sont les pistes DMA, DMA1 à DMA8, avec Computer STE).
- En 32 ch, les onglets qui ne tiennent pas restent au-delà de la rangée : **<** et **>**, avant All on, la font défiler d'un onglet (grisés à chaque bout), et l'onglet de la piste courante revient en vue chaque fois que la piste courante change.
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

**Vibrato** On ou Off (Off par défaut ; paramètre de l'hôte, automatisable) : Off coupe le vibrato de la piste et grise Depth et Rate. La ligne VIB du séquenceur (`Vxx`) le coupe et le rétablit à partir d'une ligne du motif (section 5.5). Depth de 0 à 200 cents ; Rate de 1 à 20 Hz (par pas de 0,5 Hz, 6 Hz par défaut). La molette de modulation MIDI ajoute aussi de la profondeur de vibrato, même avec Vibrato sur Off (section 7.2). Les pistes de percussion (Chipdrum, Digidrum) ne jouent pas de vibrato : le cadre est grisé.

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
| Chip Voice | Shift+Ctrl+V | La voix YM simple : carré, bruit, enveloppe |
| SID Voice | Shift+Ctrl+S | Effet de timer qui fait basculer le volume entre le volume de la note et un volume bas (son « SID » de l'Atari). **Timer ratio** 0,25 à 8,00 × (1 joue la note, 2 une octave au-dessus) ; **SID min vol** 0 à 15. En dessous, la **SID wave**, sa forme choisie dans le menu après son nom : **Base**, l'onde carrée simple, sans réglage : ses barres et Steps sont cachés, le menu seul dans leur cadre ; **Square**, un carré de Steps pas, le volume de la note sur les **Width** premiers (1 à Steps - 1, moitié-moitié par défaut), SID min vol sur le reste ; **Sine**, une sinusoïde de **Period** barres (2 à 16, 8 par défaut) répétée le long des 16 barres, aucune à 0 (une sinusoïde des volumes, tels que les barres les montrent ; le niveau de la puce, logarithmique, la rend plus brillante qu'une sinusoïde pure) ; **Bounce**, la même avec Period, mais une sinusoïde du niveau de la puce, qui sonne comme une sinusoïde pure et que les barres montrent comme un rebond, plat en haut et pointu en bas ; **Custom**, les barres dessinées à la souris. Les barres de Square montrent le carré joué et ne répondent pas à la souris. Chaque forme garde les réglages et les barres des autres. **Steps** (2 à 16, 8 par défaut), sous les barres, son libellé devant son curseur, règle le nombre de barres jouées, une par interruption du timer, mises à l'échelle par le volume de la note : le timer tourne Steps/2 fois plus vite, donc la hauteur ne change pas ; les barres au-delà restent grisées et ne sont pas jouées. Les Steps jouent une période de la note, donc la hauteur suit les cycles qu'ils contiennent : Period 8 avec 16 Steps sonne une octave au-dessus, 8 Steps à la hauteur de la note. Juste sous Steps, à la même place, Period avec Sine ou Bounce et Width avec Square, les trois curseurs alignés. Sine ou Bounce depuis Base règle autant de Steps que leur Period, un cycle qui joue la note ; Custom depuis Base ou Square, ses barres jamais dessinées, part du carré joué. Avec Sine ou Bounce, une barre déplacée décale la sinusoïde le long des barres jusqu'à la place qui amène cette barre au plus près du volume demandé, sans en changer les volumes (un cran de molette va au volume suivant que la sinusoïde peut donner à la barre) ; Custom libère les barres telles quelles. Faites glisser ou utilisez la molette de la souris sur les barres ; un clic droit ouvre le curseur de la barre (un clic droit sur le curseur la remet à 0) |
| Sync Buzzer | Shift+Ctrl+U | Effet de timer qui relance l'enveloppe matérielle à la fréquence de la note. Le choisir règle EP à 32 si EP était au-dessus de 3 200. **Tuning** On ou Off : la période d'enveloppe suit la note, et l'enveloppe joue la note × **N** / **D** (1 à 16 chacun) plus **Fine** (−100 à +100 cents). **YM Pad** : le son tourne à la période 0 sous l'enveloppe, environ −6 dB. **Presets** règlent la forme, l'accord et YM Pad en un clic, la piste restant un Sync Buzzer : Saw, Triangle, Third, Fifth, Seventh, Tenth, Tri fifth, Soft saw. En dessous, les **Sync Buzzer steps** : 0 à 8 formes d'enveloppe (R13) écrites tour à tour à chaque relance (**Steps** à 0 : la forme de la piste à chaque fois). Le timer tourne autant de fois plus vite qu'il y a de pas, pour qu'ils tiennent tous dans une période de la note et que la piste garde la hauteur de la note. Chaque bouton montre sa forme, ou « Trk » pour la forme de la piste : cliquez-le pour choisir une forme, faites un clic droit pour le remettre à la forme de la piste. Ces réglages n'agissent que tant que la piste joue un Sync Buzzer |
| FM Voice | Shift+Ctrl+F | Effet de timer qui réécrit la période du ton à chaque interruption, comme les timers FM de TTrak : la note saute entre des multiples de sa propre fréquence, un timbre bourdonnant, métallique ou de cloche. **Timer ratio** 0,25 à 8,00 × comme pour la SID Voice. En dessous, la **FM wave** et le menu de son échelle. En **Ratios** (la FM de TTrak), 16 barres, chacune le multiple joué à son pas, son chiffre dessous : 0 x0.5, 1 x1, 2 x1.5, 3 x2, 4 x3, 5 x4, 6 x5, 7 x6 (la table est écrite sous les barres). En **Semitones** (le square FM de maxYMiser), chaque barre vaut 0 à 127 demi-tons au-dessus de la note, un pixel par demi-ton (7 une quinte, 12 une octave, 19 une octave et une quinte, 24 deux octaves), sa valeur écrite dessous en hexadécimal comme les autres valeurs ($07 une quinte, $0C une octave, $18 deux octaves), sur deux rangées, les pas pairs puis les pas impairs, pour que les valeurs voisines ne se touchent pas ; le pas joue la période de la note raccourcie comme la calcule maxYMiser, et Timer ratio × Steps = 2 joue comme maxYMiser (deux pas par période de la note). Chaque échelle garde sa propre onde : en revenant à l'autre, on la retrouve telle qu'elle était. **Steps** (1 à 16, 2 par défaut) règle le nombre de barres jouées, une par interruption du timer ; le timer tourne Steps fois plus vite que la note au ratio 1, pour que les pas tiennent dans une période de la note ; les barres au-delà restent grisées. Une nouvelle piste joue la note puis son octave (1, 3). Glissez ou utilisez la molette sur les barres ; un clic droit ouvre le curseur de la barre (un clic droit sur le curseur la remet à x1). Seuls le SNDH, maxYMiser (8 pas au plus, en demi-tons) et TTrak (quatre pas de x1, x1.5, x2 ou x3) portent la FM wave ; les autres exports jouent la note (8.3) |
| Chipdrum | Shift+Ctrl+C | Percussions synthétisées : les huit instruments du Drum Kit (bouton **Drum Kit**, nom du kit affiché en police 8×8) |
| Digidrum | Shift+Ctrl+I | Échantillons 4 bits joués par un timer à travers une voix de la puce. **Pads** 1 à 8, chacun montrant le numéro du Digidrum qu'il joue (le pad n joue le Digidrum n tant que vous n'en choisissez pas un autre) : un clic choisit le pad que joue le clavier (un pad dont le Digidrum est vide est grisé et ne se choisit pas) ; un clic droit choisit son Digidrum parmi ceux qui sont chargés, ou n'importe lequel des 127 avec **Number...** (une étape d'annulation, gardée avec le son de la piste et son instrument). Les flèches **<** et **>** sous le libellé donnent aux huit pads les huit Digidrums d'avant ou d'après : 1 à 8, 9 à 16, et ainsi de suite, la dernière page 120 à 127 (une étape d'annulation). Dans le séquenceur, la ligne n joue le Digidrum du pad n. Un pad dont le Digidrum est muet faute de mémoire est barré (6.6). **ST channel** A, B ou C (Authentic 3ch : la voix qu'emprunte la piste Digidrum) ; **Digidrum samples**, sous les pads 5 à 8, ouvre la fenêtre Digidrums pour charger et modifier les échantillons ; la forme d'onde du Digidrum du pad choisi en dessous |
| DMA | Shift+Ctrl+D | Le son DMA du STE (Computer STE seulement). **Slot** : huit numéros de sample à la fois sur 127, les flèches **<** et **>** sous le libellé montrant les huit d'avant ou d'après ; cliquez-en un pour que la piste le joue. Un échantillon vide est grisé et ne peut être choisi avant d'être chargé ; un échantillon muet faute de mémoire est barré (6.6). **DMA samples**, sous les échantillons 5 à 8, ouvre la fenêtre DMA ; la forme d'onde de l'échantillon en dessous |

En Authentic 3ch et en Dual chip, les rôles des pistes sont fixes : la quatrième piste de chaque puce ne joue que des Digidrums, les pistes DMA ne jouent que du DMA, et les voix de la puce ne peuvent pas être des pistes Digidrum. Les touches de générateur ne passent qu'à un type que la piste a le droit d'utiliser.

En bas du cadre, sous un trait, quel que soit le générateur : le bouton **Instrument** ouvre la fenêtre Instruments (Ctrl+T, 6.1) ; puis le numéro de l'instrument de la piste, 1 à 127, le dernier chargé dans la piste ou rangé depuis elle (« -- » quand la piste joue son propre son), et son nom en police 8×8. Son menu liste les instruments utilisés, sur autant de colonnes que l'écran le demande, puis **Instruments...**, qui ouvre la fenêtre ; choisissez-en un pour le charger dans la piste (une seule annulation, comme Load into channel). Tapez un nom et appuyez sur Entrée pour renommer l'instrument ; sans instrument, le son de la piste est rangé dans le premier emplacement libre sous ce nom. Le numéro est sauvé avec le projet.

### 3.9 SYSTEM

| Commande | Effet |
|---|---|
| Voices | Authentic 3ch, Dual chip 2x3ch, 16 ch, 32 ch. Le changer arrête toutes les notes |
| Computer | Un menu : **ST** (le YM seul) ou **STE** (le YM plus le son DMA) |
| Master | Niveau de sortie, 0 à 100 (70 par défaut) |
| Voices (LED) | Une LED par piste, allumée tant qu'elle sonne : 16 sur deux rangées de 8 en Authentic 3ch, Dual chip et 16 ch (celles sans piste grisées) ; 32 plus petites sur deux rangées de 16 en 32 ch (pistes 1 à 16 en haut) |
| Note | Période de son et fréquence de la dernière note jouée (« TP=n -> f Hz »), tant qu'une voix sonne ; « - » dès que toutes les notes sont finies, release comprise, comme au lancement. Options > Note monitor, décoché, masque la ligne (une préférence) ; seul le moniteur MIDI bouge, il monte à sa place |
| MIDI | Moniteur MIDI : les derniers messages reçus de l'hôte ou de l'entrée MIDI, un par ligne, le plus récent en bas : le canal, puis Note on / Note off avec la note (dans les noms de notes choisis) et la vélocité, Poly AT, CC avec son numéro et sa valeur, Program, Pressure, Pitch bend depuis le centre (-8192 à +8191), et Start, Stop, Continue, Song position. Un SysEx tient sur deux lignes : « SysEx n bytes » (F0 et F7 compris), puis ses six premiers octets en hexadécimal, « .. » quand d'autres suivent (l'identifiant du fabricant en premier). Un message reçu de nouveau juste après lui-même ne prend pas de nouvelle ligne : la ligne en dessous le compte, « repeated x2 », « repeated x3 »... Les messages pris par le MIDI learn y figurent aussi ; l'horloge MIDI et l'active sensing non. Les lignes sont dans la police 8×8 du GEM ; le moniteur garde les 500 dernières (Options > Settings > MIDI monitor : 100 à 5000, ou Custom, de 1 à 100 000), et son ascenseur, ou la molette, remonte dans ces lignes ; remonté, les lignes affichées restent en place pendant que de nouveaux messages arrivent. **Clear**, à droite sur la ligne MIDI, le vide après une confirmation (grisé tant qu'il est vide). Options > MIDI monitor, décoché, le masque (une préférence) ; quand la ligne Note est masquée, il prend sa place |

### 3.10 Boutons de fenêtres, oscilloscope et moniteur de registres

Les boutons **Mixer** (Ctrl+M), **Arpeggiator** (Ctrl+B), **Modulation** (Ctrl+R), **Effects** (Ctrl+F), **Step Sequencer** (Ctrl+E) et **Patches** (Ctrl+P) ouvrent leurs fenêtres. La rangée de boutons est centrée dans la fenêtre.

L'**oscilloscope** affiche la forme d'onde de sortie, une voix seule au volume 15 remplissant presque toute sa hauteur. Deux boutons de part et d'autre, l'oscilloscope centré entre eux, choisissent ce qu'il dessine (des préférences) : à sa droite, **Main** / **Track**, la sortie principale (par défaut) ou la piste courante ; à sa gauche, **Merge**, une seule courbe pour la gauche et la droite. Merge éteint, une source stéréo (la sortie principale, sauf celle du ST en Authentic 3ch, ou une piste DMA qui joue un sample stéréo) est toujours dessinée en deux moitiés, gauche puis droite, marquées L et R, chacune dans son propre cadre, avec un espace entre les deux cadres, même quand les deux côtés jouent la même chose. Merge est grisé pour une source mono : la sortie principale du ST en Authentic 3ch, ou une piste qui ne joue pas de sample DMA stéréo. Options > Oscilloscope, décoché, le masque (une préférence), et ce qui se trouve dessous remonte, la fenêtre raccourcissant de 100 pixels. Le **moniteur de registres** affiche les registres YM R0 à R13 en hexadécimal et, à droite, la piste courante et son générateur (CHIP VOICE, SID VOICE, SYNC BUZZER, DIGIDRUM, CHIPDRUM ou DMA ; ou « INS nn » quand un Program Change a sélectionné un instrument), avec « 3CH AUTH (shared env+noise) » ou « 32CH ». Pendant que le YM/MOD Player joue, il affiche « CH A / YM/MOD PLAYER ». Options > Register monitor, décoché, le masque (une préférence), comme l'oscilloscope : le clavier ou les pads et leurs boutons remontent, la fenêtre raccourcissant de 50 pixels.

### 3.11 Clavier à l'écran et pads

- Le piano joue la piste courante. Cliquez pour jouer ; faites glisser en gardant le bouton enfoncé pour passer à d'autres touches. Faites un clic droit pour choisir la piste que joue le clavier.
- Sur une piste de percussion (Chipdrum, Digidrum), le piano est remplacé par **8 pads**. Sur une piste Digidrum, un pad sélectionne aussi son emplacement.
- **Latch** : les touches restent tenues jusqu'à ce que vous appuyiez à nouveau dessus.
- **Layer** : joue toutes les pistes activées en plus de la piste courante (règle le routage MIDI sur Layer ; le désactiver revient à Omni).
- **Oct -** / **Oct +** : octave du piano (0 à 6) ; les touches Z et X font de même.
- Le piano suit un clavier MIDI : une note jouée à gauche ou à droite de ses quatre octaves les déplace, juste assez pour la montrer (un C9 montre C6 à B9). Les notes à l'intérieur, les notes hors de la plage de la puce et les notes d'une piste de percussion ne déplacent rien.
- Les pads de percussion d'un contrôleur MIDI (section 7.1) allument le pad de leur son quand la piste courante le joue : les pads Chipdrum sur une piste Chipdrum, les pads Digidrum sur une piste Digidrum.
- **Computer keyboard** : ouvre la fenêtre Computer Keyboard (section 4.6). Le clavier de votre ordinateur ne joue des notes (ou les pads, sur une piste de percussion) que tant que cette fenêtre est ouverte.
- La ligne d'aide sous le clavier rappelle les touches principales : Space = Play/Stop, 1-8 = Pattern, Shift+1-4 = Bank, ←→ = Channel, M = Metro, Ctrl+Z/Y = Undo/Redo, Esc = Panic.

### 3.12 Fenêtre Settings

Options > **Settings...** ouvre la fenêtre SETTINGS. Chaque réglage est un libellé et un bouton qui affiche sa valeur : cliquez sur le bouton pour la liste des valeurs, la valeur courante cochée. Une valeur qui ne s'applique pas est grisée, et son infobulle dit pourquoi. La fenêtre garde sa place, et elle est de nouveau ouverte à la prochaine ouverture de l'éditeur si vous l'avez laissée ouverte.

Un changement s'applique tout de suite, pour que vous l'entendiez. **OK** garde les changements et ferme la fenêtre. **Cancel**, le bouton par défaut (contour gras, pressé aussi par Entrée), remet chaque réglage changé depuis l'ouverture de la fenêtre à la valeur qu'il avait avant, puis ferme la fenêtre ; la case de fermeture et Échap font de même. Quand un réglage diffère de sa valeur à l'ouverture, une boîte demande d'abord « Discard the changes? » : son Cancel, le choix par défaut (Entrée, Échap), garde la fenêtre ouverte avec les changements, OK les annule et la ferme. Un réglage changé puis remis à sa valeur ne demande rien. Fermer l'éditeur garde les changements.

**SOUND ENGINE** (cadre de gauche). Ces réglages sont enregistrés avec le projet ; tous sauf RAM et Quantize to VBL sont aussi des paramètres d'hôte (la fréquence de l'horloge Custom et Tuning A4 sont des paramètres que l'hôte ne peut pas automatiser).

| Réglage | Valeurs | Remarques |
|---|---|---|
| Computer | ST, STE | Comme dans le cadre SYSTEM |
| RAM | 512 Ko, 1 Mo, 2 Mo, 4 Mo (par défaut) | La mémoire de l'Atari pour lequel le projet est écrit, que se partagent ses Digidrums et ses échantillons DMA (6.6) ; Dual chip joue deux Atari, donc deux fois plus. Grisé en 16 et 32 ch, qui n'ont pas de limite. La changer ne s'annule pas ; Cancel la remet |
| Chip clock | Custom (par défaut, 2.0000 MHz), Atari ST 2.0053 MHz (PAL), ZX Spectrum 1.7734 MHz, Amstrad CPC 1 MHz | Le bouton affiche l'horloge utilisée. La première entrée, **Custom** avec sa fréquence, ouvre une boîte où vous tapez la fréquence en MHz, de 0.1000 à 8.0000 (quatre décimales au plus ; une virgule est lue comme un point). Elle ne s'applique qu'en 16 et 32 ch : en Authentic 3ch et Dual chip, l'entrée est grisée et la puce tourne à 2 MHz. ZX Spectrum et Amstrad CPC sont grisés en Authentic 3ch. Ne change que la fréquence de la puce ; le ZX Spectrum et l'Amstrad CPC ne sont pas émulés. La valeur Atari ST (machine PAL) vient de sources d'émulateurs : **À CONFIRMER** sur le matériel |
| Tuning A4 | 350.00 à 480.00 Hz (440.00 Hz par défaut) | La fréquence du La 4 (A4, note MIDI 69), d'où toutes les notes sont accordées : Chip Voice, SID Voice, Sync Buzzer, les glissandos, et les samples Digidrum et DMA ; les sons Chipdrum gardent leur hauteur. Son libellé nomme la note selon Note names : Tuning A4, ou Tuning La4 en notation latine. Cliquez sur la valeur et tapez-la en Hz, deux décimales au plus (une virgule est lue comme un point) ; une valeur hors limites est ramenée à 350 ou 480 Hz. La puce ne joue que des périodes entières, donc une note sonne à la période la plus proche : à 2 MHz, le La 4 à 440 Hz reçoit la période 284, qui joue 440,14 Hz |
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
| Clock | Internal (par défaut), MIDI ext | Horloge des arpégiateurs et du Step Sequencer : **Internal** (vitesse d'arpège en Hz ; le séquenceur suit l'hôte dans un DAW, son propre tempo dans l'application autonome) ou **MIDI ext** (les deux suivent l'horloge MIDI : pas d'arpège à la division choisie, séquenceur lancé par MIDI Start, voir 5.9). Avec MIDI ext, une ligne sous le bouton affiche « waiting for clock... », puis SYNC ou STOP. Il était avant dans le cadre SYSTEM |
| MIDI routing | Omni, Layer, Per channel | Section 7.1 |
| Velocity | Accent, Vol 4-bit, Off | Section 7.2 |
| Velocity curve | Linear (par défaut), Soft, Hard, S-curve | Comment la force d'une touche devient sa vélocité, section 7.2 |
| Bend | Pitch (par défaut), Volume, Envelope | Ce que pilote le pitch bend, section 7.2 |
| Bend range | 0 à 24 demi-tons (2 par défaut) | |
| Wheel | Off, Vibrato (par défaut), Volume, Envelope, LFO | Ce que pilote la molette de modulation, section 7.2 |
| Wheel depth | 0 à 200 cents (menu par pas de 25 ; 50 par défaut) | Le vibrato de la molette, de l'aftertouch et du slide MPE à leur maximum |
| Aftertouch | Off, Vibrato (par défaut), Volume, Envelope, LFO | Ce que pilote la pression des touches, section 7.2 |
| Aftertouch curve | Linear (par défaut), Soft, Hard, S-curve | Comment la pression des touches devient son effet, section 7.2 |
| MPE | Off (par défaut), On | 16 et 32 ch seulement, section 7.2. Le passage en Authentic 3ch ou Dual chip 2x3ch le remet sur Off ; il y reste grisé |
| MPE bend range | 0 à 96 demi-tons (menu : 1, 2, 3, 4, 5, 7, 12, puis par 12 ; 48 par défaut) | Grisé tant que MPE est sur Off |
| MPE slide (CC74) | Volume (par défaut), Envelope, Vibrato | Grisé tant que MPE est sur Off |
| Drum pads | Off, Channel 1 à Channel 16 (Channel 10 par défaut) | Le canal MIDI des pads de percussion d'un contrôleur, section 7.1. Grisé tant que MPE est sur On, son choix gardé |

**INTERFACE** (cadre de droite). Vos préférences, communes à toutes les instances de STYM2149 (section 9) : Pixels (section 3.1), Renderer (Windows seulement, grisé ailleurs), Note names, Keyboard layout (section 4.6), MIDI monitor (les lignes qu'il garde : 100, 500, 1000, 5000 ou Custom, un nombre tapé de 1 à 100 000 ; section 3.9), Information demo (On, Off : la démo de Fuji > Information, ou son icône), Interface language, Tooltips (On, Off), Tooltip language, et le bouton **Languages folder...**.

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
| YM/MOD Player | File > Play... | 4.5 |
| Computer Keyboard | Bouton Computer keyboard | 4.6 |
| Slide Curve | Entrée Custom de la ligne CRV | 4.7 |
| Effects | Bouton Effects, menu Edit, Ctrl+F | 4.8 |
| Scope | Clic sur le mini-oscilloscope d'une piste dans le Mixer | 4.1 |
| Instruments, Patches, Drum Kit, Digidrums, DMA | Menu Edit, Ctrl+T, Ctrl+P, Ctrl+K | 6 |

### 4.1 Mixer

Une tranche par piste (en Authentic 3ch : A, B, C et Digi, huit tranches avec deux puces, plus les pistes DMA avec Computer STE). En 32 ch la fenêtre montre seize tranches côte à côte, autant que 16 ch en a : une barre de défilement sous elles, deux fois moins épaisse que les autres, amène les autres en vue, et la tranche de la piste courante revient en vue chaque fois que la piste courante change. Chaque tranche comporte :

- le nom de la piste (cliquez dessus pour rendre cette piste courante) ;
- un mini-oscilloscope (cliquez dessus pour ouvrir une fenêtre **Scope** plus grande pour cette piste) ;
- un fader vertical, 0 à 100 ; pour les voix de la puce en Authentic 3ch et Dual chip, dont le niveau est le volume 4 bits, le fader est ce **Volume** du cadre SOURCE, de 0 à 15, et bouge avec lui (grisé sur un Sync Buzzer, qui n'a pas de volume fixe) ;
- un indicateur de niveau. Une piste stéréo (plus bas) a deux oscilloscopes dans le cadre, gauche puis droite (un clic sur l'un ou l'autre ouvre le Scope), et deux indicateurs de niveau, gauche et droite ;
- un curseur de panoramique, −100 à +100 (un double-clic le centre ; absent pour les voix de la puce en Authentic 3ch, puisque le ST est mono) ;
- en 16 et 32 ch, un curseur **Send**, 0 à 100 % : la part de la piste, après son fader et son panoramique, envoyée au delay (section 4.8) ; comme le panoramique, Paste settings ne le copie pas. Les modes 3 voies n'ont pas de rangée Send, le delay étant un effet de 16 et 32 ch ;
- **M** (muet) et **S** (solo).

Chaque piste DMA (16 et 32 ch, et Authentic 3ch et Dual chip avec le STE) est stéréo et en a deux de chaque : deux faders, chacun avec son indicateur et sa valeur, celui du côté gauche puis celui du côté droit, un **M** et un **S** sous chacun, et un bouton **Mono** avec un bouton **Link** dessous, sur des rangées en dessous (la fenêtre grandit de ces rangées tant qu'une piste DMA est affichée). Les autres pistes sont mono et n'en ont qu'un de chaque : les pistes de puce A, B, C et Digi des modes à 3 voies, et en 16 et 32 ch les pistes des autres générateurs (Chip Voice, SID Voice, Sync Buzzer, Digidrum, Chipdrum), dont le son est mono. Une telle piste a un fader, un indicateur et un oscilloscope (avant le panoramique), son panoramique et son Send, un M et un S, sans Mono ni Link ; son fader, son M et son S agissent sur les deux côtés de son panoramique. Lié (par défaut), chaque fader règle le Volume de la piste et les deux bougent ensemble, et M et S agissent sur les deux côtés ; délié, le fader de droite est le niveau propre du côté droit (paramètre d'hôte Level R), qui part du Volume, et chaque côté a ses propres M et S : coupez le côté gauche seul, ou isolez le côté droit seul (paramètres d'hôte Mute R, Solo R et Link). Les indicateurs et les oscilloscopes montrent chaque côté après le panoramique : une piste au centre montre ses deux côtés environ 3 dB sous son niveau, une piste tout à gauche ne montre rien à droite. **Mono** (paramètre d'hôte Mono, éteint par défaut) refait de la piste une seule bande : un fader, un M, un S, un oscilloscope et un indicateur, Link grisé ; un sample DMA stéréo est alors mixé (moyenne de gauche et droite) avant le panoramique. Ces réglages appartiennent au Mixer, comme Send : un patch, Paste settings et les fichiers de preset les laissent de côté.

Après un trait vertical, le fader **Master** est le niveau principal, le même que Master dans le cadre SYSTEM. Quand la sortie est stéréo (16 et 32 ch, et Authentic 3ch avec le STE), Master a deux faders, **L** et **R**, un curseur de balance sur la rangée des panoramiques (un double clic le centre) et un bouton **Link** sur la rangée de M. Lié (par défaut), chaque fader règle les deux côtés ; délié, chaque fader règle son côté (paramètres d'hôte Master et Master 2). La balance baisse seulement le côté opposé. Un bouton **Mono** sur la rangée de M, Link dessous (paramètre d'hôte Master Mono, éteint par défaut), mixe les deux sorties en mono : Master n'a plus qu'un fader, Link est grisé, et la balance règle toujours le niveau de chaque sortie. En Dual chip 2x3ch, les deux faders sont **1** et **2**, un par puce, avec Link et sans balance : délié, Master règle la puce 1 et Master 2 la puce 2 ; les pistes DMA du STE suivent Master. En Authentic 3ch avec le ST, la sortie est mono et Master n'a qu'un fader, sans Link ni Mono.

Sous chaque fader, panoramique, Send et Master, sa valeur s'affiche en petits chiffres, nommée sous un panoramique ou un Send (« Pan +25 », « Send 50 » ; panoramiques et balance signés, + vers la droite). Un clic droit sur l'un de ces curseurs ouvre un menu : **Value...** pour taper un nombre dans sa plage, **Reset** pour revenir à sa valeur par défaut. Un clic sur la valeur elle-même la fait taper sur place : taper le nombre, Enter valide, Escape annule (sous un panoramique ou un Send, seul le nombre se tape).

Indicateurs de sortie : Out L et Out R, ou un seul Out en Authentic 3ch sans le STE. Chaque déplacement d'un fader, d'un panoramique ou d'un Send, chaque valeur tapée ou remise par défaut, et chaque clic sur M, S, ou le Mono ou le Link d'une piste, constitue une étape d'annulation (les faders Master, la balance, Link et Mono, comme le Master du cadre SYSTEM, n'en ont pas).

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

Les touches tenues sont arpégées comme une note par touche enfoncée (ou par note MIDI), pas une par pas : l'enveloppe de cette note continue à travers les pas de l'arpège, et son point sur la courbe de l'ADSR ENVELOPE avec elle ; chaque touche enfoncée lance une nouvelle note depuis l'attaque. En 16 et 32 ch avec un **Voicing** Polyphonic, la note précédente passe alors dans son release pendant que la nouvelle joue : avec un Release long, elles se chevauchent, chacune avec son point sur la courbe. Avec Monophonic, et sur toutes les pistes des modes 3 canaux, la note précédente est coupée. Touches relâchées, la dernière note continue de parcourir l'arpège pendant son release, les précédentes finissant le leur à leur hauteur. Dans le Step Sequencer, chaque note du motif est de même une seule note de l'arpège (ligne ARP, 5.5). La table de Modulation, le vibrato et le LFO continuent d'un pas de l'arpège au suivant.

### 4.3 Modulation

Tables par trame VBL de la piste courante (l'« ornement » des trackers), et les lignes de séquence jouées avec elles. Indisponibles sur les pistes de percussion.

| Commande | Effet |
|---|---|
| Off / On | Active la table et ses lignes de séquence pour ce canal |
| Len | 1 à 64 pas |
| Loop / Hold | Boucle la table, ou tient son dernier pas |
| 1/frame … 4/frame | Vitesse de la table : ticks VBL par pas |
| Clear | Efface la table et ses lignes de séquence (une seule annulation) |
| Track | Au milieu de la fenêtre, sous les lignes de hauteur : la piste que la fenêtre édite ; son menu liste les pistes du mode avec leur générateur, comme le bouton Track du Step Sequencer, et en choisir une en fait la piste courante |
| Instrument | À gauche, sur la ligne de Track, avec le nom dessous : la ligne d'instrument du cadre GENERATOR (section 3.8) : le bouton ouvre la fenêtre Instruments, le numéro charge un instrument dans la piste, le nom le renomme ou range le son de la piste |
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
| GEN | Générateur de la note, Chip Voice, SID Voice, Sync Buzzer ou FM Voice. Cliquez pour choisir |
| FIX | Le pas joue la note fixe. Cliquez pour activer ou désactiver |
| OUT | Lecture seule : la hauteur que joue un C4 à chaque pas |

Un clic droit efface une cellule MIX, GEN ou FIX. Le curseur qu'ouvre un clic droit sur VOL, PER ou NP applique chaque mouvement aussitôt (une seule annulation pour tout le curseur) ; un clic droit sur le curseur remet la valeur par défaut (15 sur VOL, 0 sur PER et NP), et un clic sur la valeur en dessous permet d'en taper une. Les notes en haut de la fenêtre décrivent toutes les lignes. Les réglages sous la table sont toujours affichés, car Detune, Analog et la matrice de hauteur s'appliquent aussi table désactivée :

- **Fix** : la note fixe (0 à 127). **Detune** : −100 à +100 cents. **Analog** : décalage de hauteur aléatoire tiré à chaque note, jusqu'à 50 cents.
- **Hard sync** : relance le carré (16 et 32 ch) et le timer aussi sur les notes liées et glissées.
- **Link** : l'instrument qui prend le relais quand une table Hold se termine.
- **Pitch matrix** : quelles cibles (Sq : le carré, Tm : le timer de SID et de Sync Buzzer et la hauteur d'un sample DMA, Bz : la période d'enveloppe des buzzers) sont modifiées par chaque source (Table pitch, Slides, Vibrato).
- **Vibrato table** : décalages de période Min et Max, Cycles (1 à 15), Ramp (0 à 64 pas) ; **Write PER** l'écrit dans la ligne PER.

Sur une piste DMA, la hauteur de table, PER, FIX, les glissés et le vibrato modifient le sample par la colonne Tm, et VOL règle son niveau. Ce qui ne modifie rien d'un sample est grisé, son infobulle disant pourquoi : les lignes NSE, NP, MIX et GEN, Hard sync et les colonnes Sq et Bz de la matrice de hauteur. Leurs valeurs sont gardées pour un autre générateur.

**LFO** (sous les réglages) : une onde lente qui fait varier un réglage du son de la piste, relancée à chaque note ou libre (Retrig) ; elle continue sur les notes d'un arpège et dans un instrument chaîné. Il ne change que les valeurs écrites dans les registres du YM, il joue donc aussi en Authentic 3ch et dans les exports de registres. Indisponible sur les pistes de percussion.

| Contrôle | Effet |
|---|---|
| Off / On | Active le LFO de la piste ; tant qu'il est sur Off, seuls le libellé LFO et ce bouton sont affichés |
| Shape | Triangle, Sine, Square, Saw up, Saw down, Random (une nouvelle valeur tenue à chaque cycle), dessinées sur le bouton et dans son menu |
| To | Volume (pas de 0 à 15), Noise period (0 à 31), Env period, SID ratio et Pitch (en cents, jusqu'à 2400), Pan (pourcentage, 16 et 32 ch seulement). Une cible que la piste ne peut pas faire varier est grisée dans le menu, son infobulle dit pourquoi : Volume pour un Sync Buzzer ou une source sur l'enveloppe (le volume 4 bits est ignoré), Noise period sans bruit dans la source (ou pour un Sync Buzzer ou une piste DMA), Env period sans l'enveloppe (Vol 4-bit, SID Voice, DMA), SID ratio hors SID Voice, Pan dans les modes 3 voies. Si la cible choisie plus tôt ne s'applique plus (le générateur ou la source ont changé depuis), les autres réglages sont grisés et To dit pourquoi |
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
- 16 et 32 ch n'ont pas de registres YM : passez en Authentic 3ch ou en Dual chip. Une prise contient au plus 131 072 trames (43 min 41 s à 50 Hz).

### 4.5 YM/MOD Player

File > Play... ouvre un seul sélecteur qui montre tous les fichiers que STYM2149 sait jouer ; c'est le contenu du fichier qui décide comment il est joué, jamais son extension. La fenêtre YM/MOD Player le joue à la place de la sortie de l'instrument (le moniteur de registres et l'oscilloscope le suivent) ; le projet reste tel quel.

- Un morceau YM (YM2 à YM6, compressé LHA ou non) ou SNDH (`.sndh`, et un `.snd` de maXYmizer, joué par sa propre routine) est joué comme un morceau.
- Un fichier STYM2149 (projet, motif ou morceau), un morceau maxYMiser (`.mys` avec son `.myv`, comme pour l'import), un morceau YM-Tracker (`.ymt`), un module TTrak (`.tpm`), un module XM, MOD, S3M ou OKT est joué par une seconde copie du moteur, à part du vôtre : le fichier est converti comme File > Import le convertirait, dans une copie de votre projet, et joué depuis le début de son Song (ou de son motif s'il n'a pas de Song). Un motif ou un morceau joue avec les sons de votre projet ; un projet avec les siens. La lecture s'arrête à la fin, et Play la relance depuis le début.
- Un fichier qu'aucun d'eux ne lit donne une alerte (« Unsupported file »), de même qu'un kit de batterie ou une bibliothèque de patches (« Nothing to play in this file. »).

- **Play** / **Stop**, **Rewind**, position « trame / total (secondes) » (trames de 1/50 s ; le total est « ? » quand il n'est pas connu).
- **Load**, à droite de la même rangée, ouvre le sélecteur de File > Play... pour jouer un autre fichier à sa place.
- **Subsong** − / + pour les fichiers SNDH à plusieurs sous-morceaux (relance le morceau).
- Fermer la fenêtre arrête la lecture et oublie le fichier ; il n'est pas enregistré dans le projet.

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

Chaque répétition dure autant que la note a été tenue. Un Sync Buzzer n'a pas d'écho en Authentic 3ch et Dual chip : il joue l'unique enveloppe matérielle de la puce, qu'une répétition relancerait à plein niveau ; les réglages de l'écho y sont grisés (en 16 et 32 ch chaque voix a sa propre enveloppe, et l'écho joue). En Authentic 3ch et Dual chip, un canal ne joue une répétition que s'il ne joue aucune note à lui : une répétition de la même piste attend que la note soit relâchée et prend son canal ; une répétition pour une autre piste prend le canal de cette piste quand il est libre (un Digidrum prend le canal digidrum) ; sinon la répétition est sautée. En 16 et 32 ch, une répétition prend une voix libre, ou la plus ancienne répétition ; une nouvelle note prend la voix d'une répétition avant celle d'une note plus ancienne.

**Delay**, en 16 et 32 ch seulement : un delay audio alimenté par le **Send** de chaque piste dans le Mixer (0 par défaut : rien ne s'entend tant qu'aucune piste ne lui envoie de son). Aucun ST ne pourrait le faire : en Authentic 3ch et Dual chip sa rangée est grisée et le son ne change pas. Ses réglages sont des paramètres d'hôte.

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

- 32 motifs en 4 banques (A à D) de 8. Chaque motif a de 1 à 256 pas (curseur **Steps**) et contient les 32 pistes.
- Cliquez sur une banque, puis sur un motif (1 à 8). Les touches 1 à 8 choisissent un motif de la banque courante ; Shift+1 à Shift+4 choisissent la banque A à D. Pendant la lecture, un nouveau motif démarre à la fin de la boucle en cours. Quand le morceau passe à un motif d'une autre banque, la banque affichée le suit, sauf si vous aviez ouvert une autre banque entre-temps.
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
- Le **transport** : Play/Stop, Rec, Loop, Play from start, entrée de morceau précédente et suivante, Metronome (un clic sur chaque temps, une cloche sur le premier temps de la mesure ; il s'entend, mais l'oscilloscope et les indicateurs de sortie du Mixer ne le dessinent pas ; un clic droit ouvre son volume, de 0 à 100 %, gardé avec le projet), **Launch** (allumé, les touches et pads MIDI Do2 à Sol2 choisissent les motifs 1 à 8 de la banque affichée et Sol#2 à Si2 les banques A à D, sur tous les canaux, au lieu de jouer ; éteint au démarrage du plugin ; voir 7.6 pour l'apprendre ou déplacer ces touches) ; puis **Track**, la piste que la grille édite, dont le menu liste les pistes du mode avec leur générateur pour en choisir une autre (comme les onglets de la fenêtre principale) ; puis l'instrument de la piste, comme dans le cadre GENERATOR (3.8) : **Instrument** ouvre la fenêtre Instruments, et le numéro de l'instrument de la piste (« -- » quand la piste joue son propre son) en charge un autre dans la piste depuis son menu, qui montre leurs noms et ouvre aussi la fenêtre Instruments. En mode Song, quand les flèches Song sont affichées, le numéro reste seul ; puis les banques et les motifs.

Sur les pistes de percussion, les lignes de notes deviennent les huit instruments (Chipdrum : Kick, Snr, Clap, Hat, OHat, CHat, Tom1, Tom2 ; Digidrum : Sl et le numéro du Digidrum que joue chaque pad, Sl 1 à Sl 8 tant que vous n'en choisissez pas d'autres) et les coups durent un pas.

### 5.3 Écrire des notes

Avec l'outil Draw :

- **Cliquez** sur une cellule vide pour écrire une note (avec la Note length et le Chord choisis). Faites glisser vers la droite sur la même ligne pour l'allonger ; faites glisser ailleurs pour écrire d'autres notes.
- **Cliquez sur une note** pour la supprimer (si sa longueur est la Note length courante) ou pour lui donner la Note length courante.
- **Faites glisser une note** pour la déplacer ; une note d'une sélection (cadre ou Shift+clic) déplace toute la sélection. Tant que le bouton reste appuyé sur une note qui peut bouger, le pointeur est une main ; il redevient le crayon quand vous le relâchez. Faites glisser son **bord gauche ou droit** (la partie extérieure de son premier ou de son dernier pas, où le pointeur devient une double flèche) pour la redimensionner.
- Sur une piste polyphonique, cliquez sur une autre ligne d'un pas qui a déjà une note pour ajouter une voix d'accord (jusqu'à 6 voix par pas) ; cliquez sur une voix d'accord pour la supprimer.
- Sur une piste Monophonic, et en Authentic 3ch et Dual chip 2x3ch, où une piste joue une note à la fois, les voix d'accord sont grisées : cliquez sur l'une d'elles pour en faire la note jouée, l'ancienne note devenant une voix grisée (de retour en 16 ou 32 ch Polyphonic, l'accord joue en entier).
- **Alt+clic** sur une note la scinde à ce pas.
- **Shift+clic** sur une note l'ajoute à une sélection multiple (ou l'en retire) ; **Shift+glisser** sur des cellules vides trace un cadre de sélection. Sur les lignes de contrôle (et sur les pistes de percussion), Shift+clic sélectionne un bloc de pas.
- **Clic droit** sur une note : Copy, Cut, Paste here, Longer ], Shorter [, Split here `\`, Join with next J, Length, Delete, Select all ^A. Clic droit sur une cellule vide quand une note est dans le presse-papiers : Paste note here, Select all ^A.
- **Glisser avec le bouton droit** trace un cadre de sélection ; les notes qu'il prend (celles qu'il couvre entièrement, de leur premier pas au dernier) sont encadrées de vert sur toute leur longueur. Un clic droit dessus donne Copy, Cut, Paste, Clear, Select all ^A, Deselect. Un clic droit sur un bloc de pas sélectionné (ou sur une cellule vide) donne Copy block, Cut block, Paste block, Clear block, Select all ^A, Deselect, Note length, Channel, Rename channel....

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
| INS | Mélodique, quand le projet a des instruments | Channel, 1 à 127 | Joue la note avec un instrument du projet (6.1) |
| GEN | Mélodique | Channel, Chip Voice, SID Voice, Sync Buzzer, FM Voice | Un autre générateur sur ce pas seulement |
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
| CMD | Affichée par défaut (menu Resolution > Step command rows la masque), et dès qu'une commande de sa boîte est écrite | SYN, VSL, BSL, NSL ; MUL pour plusieurs | Commandes de pas (5.5) |
| PAN | 16 et 32 ch, et pistes DMA | Center, −10 à +10 | Position stéréo de ce pas (une note relâchée par Stop la garde jusqu'à la fin de son relâchement) |
| VOL | Toutes | 0 à 15 | Accent (niveau 4 bits). Faites glisser pour dessiner ; clic droit pour un curseur (clic droit dessus : retour à 15 ; clic sur sa valeur pour en taper une) et son bouton **Slide** : le niveau glisse vers le VOL du pas suivant (`Gxx`, 5.5), dessiné en rampe |
| TIK | Toutes, sous VOL après un séparateur, avec les lignes de commandes | 1 à 31 | Ticks par ligne de toutes les pistes à partir de ce pas (`Sxx`, 5.5) ; n'agit qu'en cadencement Ticks, barrée en cadencement BPM |
| BPM | Application autonome, sous TIK (sous VOL après un séparateur) | 32 à 255 | Tempo de toutes les pistes à partir de ce pas : une seule ligne pour tout le motif, la même sur chaque piste (`Txx`, 5.5) |

ENV, VIB, LFO, MOD, TUN, DLY, CUT, SMP, TIK et BPM sont affichées avec la ligne CMD (Resolution > Step command rows), chacune aussi tant qu'une de ses valeurs est écrite ; ARP est affichée dès que l'arpégiateur de la piste est allumé. ARP, VIB, LFO et MOD disparaissent tant que l'arpégiateur, le vibrato, le LFO ou la table de modulation de la piste est éteint, et reviennent avec leurs valeurs quand il est rallumé.

Une valeur écrite là où elle ne peut pas agir est grisée et barrée d'une croix, et son infobulle dit pourquoi : le LFO de la piste sans profondeur (LFO), une piste de percussion (TUN), aucune enveloppe ne joue (ENV), pas une piste DMA ou aucune note ne part sur ce pas (SMP, DLY), aucun glissé ne joue (CRV), cadencement BPM (TIK), cadencement Ticks ou horloge externe (BPM). Elle reste modifiable et effaçable.

Une case où rien ne pourrait agir à cause du son de son pas est barrée même vide, et un clic n'y ouvre rien ; son infobulle dit pourquoi. Ce son est le générateur de la note (GEN, sinon son instrument, sinon la piste) : ENV sous une SID Voice, qui écrit ses propres volumes ; SRC sous un Sync Buzzer ; SRC et ENV sous le DMA ou un son de percussion ; SMP hors des notes DMA ; GEN sur les pistes DMA d'Authentic 3ch et de Dual chip, et sur une note DMA ou de percussion tenue ; INS et CND sur une note tenue, INS sur la note qu'atteint un glissé (il garde l'instrument de la note d'où il part) ; GAT et RTG pendant un glissé ; GAT sur un pas après lequel la note continue (seul le GAT de son dernier pas agit). Sur un pas où aucune note ne sonne, une case vide reste ouverte, puisqu'une note peut y être écrite plus tard ; une valeur déjà écrite y est barrée.

Une note dessinée sur plusieurs pas est tenue : SRC, GEN, CRV, PAN, VOL, RTG et ARP agissent sur chacun de ses pas. VOL règle son niveau pas par pas ; RTG la refrappe depuis le début de son pas ; ARP y lance ou reprend l'arpège (RTG attend tant que l'arpège joue, et pendant un glissé seul VOL agit). GAT agit sur le dernier pas de la note, où il la relâche ; OCT, TON, INS, CND et SLD appartiennent à son premier pas.

Cliquez sur une ligne on/off pour l'activer ou la désactiver (faites glisser pour peindre). Cliquez sur une ligne de valeurs pour ouvrir son menu ; GAT, PAN, BPM, TUN, DLY, CUT, SMP et TIK ouvrent une petite boîte (PAN affiche 0 au centre) ; CMD ouvre la boîte des commandes du pas. Faites un clic droit sur une cellule de contrôle pour la réinitialiser (faites glisser pour en réinitialiser davantage), sauf une case GEN, SRC ou ENV qui porte une valeur : son clic droit ouvre une fenêtre avec les réglages de la piste courante que la case met en jeu, qui agissent comme les mêmes commandes de la fenêtre principale. GEN : ceux de SID Voice (Timer ratio, SID min vol, la SID wave, le menu de ses formes, ses Steps, Period avec Sine ou Bounce, Width avec Square, disposés comme dans la fenêtre principale) ou de Sync Buzzer (Tuning, N, D, Fine, YM Pad, Presets, les pas) ; Chip Voice n'en a pas. SRC : Noise period et Volume, grisés quand la source de la case ne les utilise pas, et Level. ENV : Env freq et EP. Une seule fenêtre s'ouvre à la fois, celle de la dernière case cliquée. Faites un clic droit sur le libellé d'une ligne pour **Copy row** / **Paste row**, qui copie une ligne entière d'une piste à une autre (sauf BPM, que toutes les pistes partagent).

### 5.5 Commandes de pas (CMD et les lignes de commandes)

La ligne CMD et les lignes propres des commandes (ENV, VIB, LFO, MOD, TUN, DLY, CUT, SMP, TIK, BPM) sont affichées par défaut ; Resolution > Step command rows > Off les masque (une préférence) tant qu'aucune de leurs valeurs n'est écrite. ARP est affichée avec l'arpégiateur. Un pas porte jusqu'à quinze commandes par piste, une de chaque sorte, qui jouent ensemble, et le tempo du motif. La cellule CMD affiche le nom court de la commande de sa boîte (SYN, VSL, BSL, NSL), ou MUL quand il y en a plusieurs ; BPM, TUN, DLY, CUT, SMP et TIK affichent leur nombre, ENV la forme (8 à F) et R pour une relance, ARP, VIB, LFO et MOD On ou Off.

Cliquez sur une cellule CMD pour ouvrir la boîte **Commands** : une ligne par commande, dans l'ordre Sync Mark, Volume Slide, Buzzer Slide, Noise Slide, chacune avec sa valeur et un bouton **On**/**Off**. Un glissé a une vitesse de 0 à 15 (0 l'arrête) et un bouton pour son sens, **Up** ou **Down** (écrit `x0` ou `0y`). En 16 et 32 ch, le bouton des Buzzer et Noise Slides ouvre un menu : Up, Down et un sous-menu **Curve** avec les 13 formes de la ligne CRV ; le bouton affiche le sens et le nom court de la forme (Down EXP). Une forme autre que Linear (`Kxx`, `Qxx`) fait suivre cette forme au glissé, de son pas jusqu'à la commande suivante de son type (jusqu'à la fin de la piste s'il n'y en a pas), en couvrant ce que sa vitesse couvre en ligne droite ; Linear garde la vitesse par trame. Les autres commandes ont un curseur et un champ tapé en décimal. Un glissé n'est dans la boîte que s'il peut faire bouger une note entre son pas et la commande suivante de son type, la piste bouclant : Buzzer Slide une note dont on entend l'enveloppe (source ou entrée SRC Env, Sync Buzzer) ; Noise Slide une note dont on entend le bruit (Source ou SRC avec bruit, Chip Voice ou SID Voice) ; le Volume Slide, que le Slide d'une case VOL remplace, seulement là où un projet plus ancien en a écrit un. Chaque note a le générateur de sa case GEN, sinon de son instrument, sinon de la piste, et la source de sa case SRC, sinon de son instrument ou de la piste ; sa table de modulation compte aussi, et un pas vide compte comme la note qu'on pourrait y écrire. Aucun glissé sur une piste de percussion. Un glissé écrit sur le pas reste dans la boîte, pour pouvoir l'éteindre. Choisir une valeur met sa ligne sur On. **OK** écrit toutes les lignes dans le pas ; **Cancel**, le bouton par défaut (Entrée, Échap), ferme sans le changer. Une commande absente du pas propose une valeur de départ : marque 1, volume descendant d'un quart de pas par tick (`01`), les autres glissés montant d'une unité (`10`). Un clic droit sur une cellule CMD efface les commandes de sa boîte.

Les autres commandes ont leur propre ligne. **ARP**, **VIB**, **LFO** et **MOD** : un menu, No change, On (`J01`, `V01`, `L01`, `M01`) ou Off (`J00`, `V00`, `L00`, `M00`). **BPM** (application autonome seulement, puisqu'en plugin le DAW règle le tempo ; un tempo écrit reste dans le projet) : une boîte avec un curseur de 32 à 255, qui part du champ BPM ; la ligne appartient au motif, donc un tempo écrit sur une piste apparaît sur toutes. **ENV** (quand l'enveloppe joue) : un menu des formes 8 à F avec leur dessin, et un sous-menu **Restart** : None, At once (tick 0) ou At tick..., qui ouvre une boîte sur les ticks d'une ligne (un tick plus grand tapé est ramené au dernier). **DLY** et **CUT** : une boîte avec un curseur sur les ticks d'une ligne au tempo courant, à partir de 1 pour DLY et de 0 pour CUT ; un tick plus grand tapé dans son champ est ramené au dernier tick de la ligne, comme le curseur. La boîte de CUT a un bouton **Fade** à gauche de ses boutons (16 et 32 ch ; grisé dans les modes à 3 voies, qui coupent comme un Atari) : allumé, chaque CUT du projet éteint ses notes en fondu au lieu de les arrêter d'un coup, ce qui évite un clic (lignes CUT des percussions et fin d'un coup comprises). Le champ qui le suit donne la durée du fondu, de 1 à 100 ms, 5 par défaut ; il est grisé tant que Fade est éteint. Fade et sa durée appartiennent au projet, Fade est éteint par défaut, et aucun des deux n'a d'étape d'annulation. Un CUT en fondu affiche **F** après son tick dans la ligne CUT (« 4F ») ; sans Fade, ou dans les modes à 3 voies, le tick seul (« 4 »). **TUN** : une boîte en cents, de −99 à +99. **SMP** (pistes DMA) : une boîte de 0 à 255, par pas de 256 trames. **None** dans une boîte, et No change dans un menu, retirent la commande du pas ; un clic droit sur la cellule aussi (sur ENV, la forme et la relance).

| Commande (ligne de la boîte, ou sa propre ligne) | Effet |
|---|---|
| `Sxx` (ligne TIK) | Ticks par ligne à partir de cette ligne (cadencement Ticks seulement ; 1 à 31). Ignoré en cadencement BPM et en Host sync |
| `Txx` (ligne BPM) | Tempo de toutes les pistes à partir de cette ligne, de 32 à 255 BPM (une valeur plus petite tapée devient 32), une seule ligne pour le motif. Cadencement BPM avec l'horloge propre du séquenceur (Free run) et dans les exports ; ignoré en cadencement Ticks, en Host sync et en MIDI ext. Un projet ou un fichier d'une version précédente qui avait un tempo sur plusieurs pistes d'une même ligne garde le dernier. Le tempo tient jusqu'au `Txx` suivant : Play repart du champ BPM, une entrée de Song qui a son propre tempo repart de celui-ci à chaque passage, et toucher au champ BPM reprend la main |
| `Vxx` (ligne VIB) | Vibrato de la piste à partir de cette ligne : Off (`V00`) le suspend, On (`V01`) le rétablit (le vibrato du cadre VIBRATO ; la molette de modulation ajoute toujours le sien). Tient jusqu'au `Vxx` suivant ; Play et Stop rendent à la piste son vibrato. Avec Vibrato sur Off dans le cadre, `Vxx` ne rétablit rien |
| `Jxx` (ligne ARP) | Arpégiateur de la piste à partir de cette ligne : On (`J01`) le fait jouer sur les notes du motif, Off (`J00`) l'arrête, aussi sur un pas d'une note tenue, qui continue sans arpège. Tient jusqu'au `Jxx` suivant. Chaque motif commence avec l'arpégiateur éteint, comme Play ; lancée au milieu d'un motif, la piste prend le dernier `Jxx` avant ce pas. Avec l'arpégiateur sur Off dans sa fenêtre, `J01` ne fait rien. Les pas ARP d'une version précédente deviennent des `J01` et `J00` et jouent pareil |
| `Lxx` (ligne LFO) | LFO de la piste (fenêtre Modulation) à partir de cette ligne : Off (`L00`) le suspend, On (`L01`) le rétablit. Tient jusqu'au `Lxx` suivant ; chaque motif commence avec le LFO tel que réglé dans la fenêtre |
| `Mxx` (ligne MOD) | Table de modulation de la piste (fenêtre Modulation) à partir de cette ligne : Off (`M00`) la suspend, On (`M01`) la rétablit. Tient jusqu'au `Mxx` suivant ; chaque motif commence avec la table telle que réglée dans la fenêtre |
| `Rxx` (ligne ENV, Restart) | Relance l'enveloppe matérielle (réécrit R13) au tick xx de la ligne (0 à 255), sans redéclencher la note. En Authentic 3ch, seulement si la piste pilote l'enveloppe partagée |
| `Zxx` (Sync Mark) | Synchro de démo ; ne joue rien. Dans un export SNDH, la routine écrit xx (0 à 255) dans son octet de synchro à la trame de la ligne (plusieurs pistes sur une même ligne : la dernière l'emporte). Une démo trouve cet octet à l'adresse que la routine d'init SNDH renvoie dans le registre A0 ; il vaut 0 après l'init. L'endroit où maxYMiser garde son propre octet de synchro est **À CONFIRMER** |
| `Gxx` (ligne VOL, Slide) | Le niveau du pas glisse trame après trame, en ligne droite, de son VOL au VOL du pas suivant : l'accent en 16 et 32 ch et sur les pistes DMA, le volume 4 bits dans les modes 3 voies (dans les registres exportés). Pas sur les pistes de percussion |
| `Axy` (Volume Slide, projets plus anciens) | Le volume des notes monte de x ou descend de y quarts de pas (sur les 16 niveaux) par tick de replay, à partir du deuxième tick d'une note ; si les deux sont réglés, il bouge de x − y. Tient jusqu'au `Axy` suivant : `A00` l'arrête. Chaque nouvelle note repart de son propre volume. Sans effet sur un volume piloté par l'enveloppe. Le Slide d'une case VOL le remplace ; la boîte le montre là où un projet en a un |
| `Dxx` (ligne DLY) | Les notes de ce pas partent au tick xx de la ligne ; le reste de la ligne fait leur durée. Un tick au-delà de la ligne est ignoré (les notes partent à l'heure) |
| `Cxx` (ligne CUT) | Les notes de la piste s'arrêtent au tick xx de la ligne (`C00` : tout de suite, la note de ce pas ne sonne pas). Un tick au-delà de la ligne est ignoré. Cette ligne seulement |
| `Bxy` (Buzzer Slide) | La période de l'enveloppe matérielle (R11-R12) glisse : montée x (période plus courte, plus aigu) ou descente y en seizièmes de demi-ton par tick de replay. Tient jusqu'à `B00` ; chaque nouvelle note repart de sa propre période. Fait plier les basses Sync Buzzer et d'enveloppe |
| `Kxx` (Buzzer Slide, Curve) | 16 et 32 ch : la forme du `Bxy` du pas (1 à 12 : les formes de la ligne CRV après Linear). Le glissé la suit jusqu'au `Bxy` suivant de la piste ; les modes 3 voies l'ignorent |
| `Nxy` (Noise Slide) | La période de bruit (R6) glisse : montée x (période plus courte, bruit plus aigu) ou descente y en quarts de pas par tick de replay, entre 1 et 31. Tient jusqu'à `N00` ; chaque nouvelle note repart de son propre bruit |
| `Qxx` (Noise Slide, Curve) | 16 et 32 ch : la forme du `Nxy` du pas, comme `Kxx` |
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

Choisir une autre résolution alors que les motifs contiennent des notes ouvre une boîte qui demande si les notes la suivent. **Fit** place chaque note sur le pas de la nouvelle grille le plus proche de son instant (un demi-pas s'arrondit au pas suivant) et met à l'échelle sa longueur, les valeurs de pas écrites sur elle, les accords, la longueur des motifs, la ligne BPM, les longueurs propres des pistes et les silences du Song ; quand deux notes d'une ligne tombent sur le même pas, la première reste. Sur une grille plus fine, les nouveaux pas d'une note tenue gardent le volume, la source, le générateur et le panoramique du pas qui les précède. Fit s'annule en une étape. **Keep** laisse chaque note sur son pas, qui dure désormais la nouvelle valeur. **Cancel**, par défaut (Entrée et Échap), garde la résolution. Sans notes, ou avec la même résolution, aucune boîte ne s'ouvre.

### 5.9 Horloges et synchronisation sur l'hôte

| Horloge | Où | Comportement |
|---|---|---|
| **Host sync** | Plugin (VST3, CLAP, LV2) | Le transport de l'hôte démarre et arrête le séquenceur et règle sa position, son tempo et sa signature rythmique. Le bouton Play et Space ne font rien ; le champ BPM affiche « host » et le tempo de l'hôte |
| **Free run** | Application autonome | L'horloge propre du séquenceur : Play/Stop (Space), BPM 60 à 240, commandes `Txx` |

En Host sync, le pas k du motif commence à la noire k / (pas par noire) à partir du début de la timeline de l'hôte ; le motif se répète à chaque longueur de motif ; le morceau enchaîne ses entrées depuis le début. Quand vous lancez la lecture ou faites un saut (boucle, repositionnement), les notes tenues du séquenceur sont relâchées et le séquenceur est replacé. **Avec Loop désactivé, le motif (ou le morceau) est joué une fois à partir du début de la timeline, puis reste silencieux** jusqu'à ce que l'hôte revienne en arrière. Le tempo de l'hôte remplace le tempo propre à chaque entrée du morceau. Les arpèges réglés sur Clock MIDI ext suivent le temps de l'hôte (24 impulsions par noire).

MIDI Start, Stop, Continue, Song Position Pointer et l'horloge MIDI ne pilotent pas le séquenceur en Host sync. Avec l'horloge propre du séquenceur (Free run), MIDI Start le lance depuis le premier pas (à nouveau s'il joue déjà), MIDI Stop l'arrête, et Continue le lance lui aussi depuis le premier pas, cette horloge n'ayant pas de pause ; Song Position Pointer ne fait rien. Pour que le séquenceur suive l'horloge MIDI, réglez **Clock** (Options > Settings, MIDI) sur **MIDI ext**, comme dans le prototype : le curseur BPM est grisé, Play arme le séquenceur, MIDI Start allume Play et le lance depuis le début, Continue et Song Position Pointer reprennent à l'endroit demandé, MIDI Stop l'arrête, et le champ BPM affiche « ext » et le tempo mesuré. Changer Clock pendant la lecture arrête celle-ci. Les exports hors ligne (WAV, stems, exports de registres) n'ont pas d'horloge MIDI à suivre : ils jouent au tempo propre du séquenceur.

### 5.10 Enregistrement

- **Rec** (transport) enregistre dans le motif les notes que vous jouez sur la piste courante, au pas en cours de lecture. Dans l'application autonome, armer Rec lance aussi la lecture ; l'arrêt le désarme.
- **Step** (saisie pas à pas) écrit chaque note que vous jouez au curseur, qui avance ensuite. Il a priorité sur Rec.

Les notes de votre clavier MIDI et du clavier à l'écran sont enregistrées. Avec le routage MIDI Per channel, seules comptent les notes reçues sur le canal de la piste courante.

Avec Rec, une note est écrite au pas où sa touche s'enfonce et dure autant que la touche reste enfoncée, arrondie au pas le plus proche (un pas au moins) ; elle s'allonge à l'écran tant que vous la tenez et s'arrête à la fin de la piste. Les touches jouées ensemble font un accord : la première est la note du pas, les autres ses voix d'accord, chacune avec sa propre durée ; une touche enfoncée pendant que d'autres sont tenues les rejoint comme voix d'accord à son propre pas, et des pads frappés ensemble mettent les deux sons sur le pas. Sur une piste Monophonic, les touches jouées ensemble sont toutes écrites : la plus grave est la note du pas, en noir, les autres des voix d'accord grisées, qui sonnent dès que la piste repasse en Polyphonic ; une touche enfoncée à un pas suivant arrête la note encore tenue là où commence la nouvelle. Dans les modes 3 voies, qui jouent une note à la fois, seule la dernière touche est écrite. La première touche écrite à un pas pendant un passage remplace les notes qui s'y trouvaient, voix d'accord comprises, grisées ou non ; les autres touches du même passage à ce pas la rejoignent. Une piste de percussion garde ses autres coups, pour enregistrer un rythme un son après l'autre. Le Rec s'arme aussi depuis un contrôleur : CC 117 sur le canal 16 (le bouton Record d'un Launchkey) le bascule à l'appui (valeur 64 ou plus) et lance la lecture ; le relâchement (valeur 0) est ignoré. Avec Velocity sur Off (7.2), les notes gardent VOL 15 ; sinon la vélocité donne leur VOL, passée par la Velocity curve comme la note s'entend.

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

127 instruments (numérotés de 1 à 127) stockés dans le projet, affichés 32 à la fois : les flèches **<** et **>** sous les emplacements tournent les pages (1-32, 33-64, 65-96, 97-127), et la fenêtre s'ouvre sur la page de l'instrument de la piste courante. Un instrument est un son de piste : source, volume, enveloppe, générateur, vibrato, ADSR, table Modulation et séquences, arpège. Le niveau, le panoramique et le gate restent ceux de la piste qui le joue.

- Cliquez sur un emplacement utilisé pour le charger dans la piste courante ; cliquez sur un emplacement vide pour y stocker la piste courante (un nom vous est demandé, 16 caractères au plus).
- Clic droit : Store channel, Load into channel, Rename, Save to Patches, Load from Patches, Clear.
- Dans le Step Sequencer, la ligne **INS** joue une note avec un instrument (son menu se termine par Instruments..., qui ouvre cette fenêtre). La ligne apparaît dès que le projet a un instrument.
- **Program Change** n (1 à 127) sélectionne l'instrument n pour les notes jouées en direct (section 7.4) : tous les instruments sont accessibles.

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

Jusqu'à 127 emplacements d'échantillons 4 bits joués par le timer d'une piste Digidrum, à leur fréquence à partir de leur note de référence. Une piste Digidrum a huit pads, chacun jouant l'emplacement que vous lui choisissez dans le cadre GENERATOR (3.8) : dans le séquenceur, la ligne sur laquelle vous écrivez choisit le pad ; au clavier et en MIDI, c'est le pad choisi dans le cadre GENERATOR.

- **Slot** : huit numéros à la fois, avec **<** et **>** pour montrer les huit d'avant ou d'après (clic : le jouer et le sélectionner). Un emplacement muet faute de mémoire est barré, et son infobulle le dit. **Name** : un champ de 16 caractères, pris du fichier chargé ; tapez dedans, Entrée ou un clic ailleurs garde le nom (une étape d'annulation). Le nom ouvre la ligne de texte au-dessus de la forme d'onde, ici et dans le cadre GENERATOR, et suit le numéro de l'emplacement dans les infobulles. Dans cette fenêtre, la ligne ne commence plus par le numéro du sample, que montrent les boutons des emplacements.
- **Rate** 2 000 à 20 000 Hz à la note de référence (préréglages 7800, 8000, 11025). La changer conserve les valeurs, donc la hauteur change ; un fichier se charge à cette fréquence. Avec **Resample** allumé (sous les préréglages ; une préférence, éteinte par défaut), une nouvelle fréquence reprend la source 8 bits à cette fréquence, comme dans la fenêtre DMA : l'emplacement garde sa durée et sa hauteur, Start et End restent aux mêmes instants ; une étape d'annulation par geste.
- **Root** (la note qui joue l'emplacement à sa fréquence) et **Fine** (−100 à +100 cents).
- **Load...** (ou clic sur la forme d'onde, ou un fichier audio déposé dessus) : un fichier audio (WAV, AIFF, FLAC, Ogg Vorbis), rééchantillonné à la fréquence, quelle que soit sa durée (6.6 dit ce que l'Atari peut contenir). Quand l'emplacement contient déjà un échantillon, une alerte le nomme avec le fichier, avec **Cancel** (par défaut) et **Replace**. **Play** joue l'emplacement une fois, de Start à End, à travers la table de volume de la puce ; avec Loop, il répète cette partie jusqu'à un nouveau clic sur Play ou Loop ou la fermeture de la fenêtre, Play restant allumé pendant ce temps. **Clear** vide l'emplacement.
- **Start** et **End** : la partie de l'échantillon jouée (End est la valeur qui suit la dernière jouée), avec ou sans Loop ; charger un fichier les règle sur l'échantillon entier. La forme d'onde grise ce qui est en dehors. Les exports YM6 et SNDH écrivent cette partie.
- **Loop** : répète Start à End tant que la note est tenue, puis joue jusqu'à End.
- **4-bit** : **Linear** ou **YM levels** (valeurs calées sur les niveaux de sortie mesurés de la puce, pour que l'échantillon garde sa forme). La source 8 bits est conservée, vous pouvez donc refaire la conversion.
- **Tools** sur la source 8 bits : Normalize, Reverse, Fade out, **Sign** (inverse le bit de poids fort de chaque octet : signé ↔ non signé), −3 dB, +3 dB. Reverse retourne aussi Start et End, pour que la partie jouée soit le même son lu à l'envers. −3 dB juste après un +3 dB qui a écrêté rend les crêtes écrêtées : les pas qui se suivent sont calculés à partir des valeurs d'avant le premier.
- **Stereo** : ce que Load prend d'un fichier stéréo : son canal gauche (**Left**), son canal droit (**Right**) ou les deux mixés (**Mix**, par défaut). Une préférence, qui vaut pour le prochain fichier chargé ; un fichier mono se charge tel quel.
- **Banks** : enregistrer les emplacements en banque nommée (huit au moins, jusqu'au dernier chargé), charger ou supprimer une banque. La bibliothèque de banques (`Digidrums.json`) est partagée par tous les projets.
- **Memory**, sous les emplacements en Authentic 3ch et Dual chip : la mémoire que prennent les échantillons sur celle de l'Atari, avec sa jauge (6.6). Elle n'est pas montrée en 16 et 32 ch, qui n'ont pas de limite.

Charger un fichier ou une banque ne peut pas être annulé ; les autres réglages le peuvent.

### 6.5 DMA (Edit > Samples > DMA (STE))

Le son DMA du STE joue des échantillons 8 bits signés, jusqu'à 127 (autant que d'instruments, pour que chaque instrument d'un XM garde son échantillon), aussi longs que la mémoire de l'Atari le permet (6.6). Il demande Computer STE : avec Computer ST l'entrée du menu est grisée, et une fenêtre restée ouverte l'indique et propose **Use STE**. Avec STE, les pistes DMA (Authentic 3ch, Dual chip) ou les pistes de type DMA (16 et 32 ch) jouent ces échantillons, chaque piste choisissant son échantillon dans le cadre GENERATOR.

La fenêtre est organisée comme celle des Digidrums : l'emplacement à gauche, son échantillon à droite.

- **Slot** : huit numéros à la fois, avec **<** et **>** pour montrer les huit d'avant ou d'après ; un emplacement muet faute de mémoire est barré. **Memory** et sa jauge suivent dessous en Authentic 3ch et Dual chip, comme dans la fenêtre Digidrums. **Name** : un champ de 16 caractères, pris du fichier chargé ou, après un import XM ou MOD, du sample ou de son instrument ; tapez dedans comme dans la fenêtre Digidrums. Le nom ouvre la ligne de texte au-dessus de la forme d'onde, ici et dans le cadre GENERATOR, sans le numéro du sample devant, que montrent les boutons des emplacements ; il suit le numéro du sample dans les infobulles et dans le texte du cadre GENERATOR.
- **Rate** : 6.3k, 12.5k, 25k, 50k (6 258, 12 517, 25 033, 50 066 Hz). La changer conserve les octets, donc la hauteur et la durée changent, comme sur le STE. Avec **Resample** allumé (sous Rate ; une préférence, éteinte par défaut), une nouvelle fréquence rééchantillonne le sample à la place : il garde sa durée et sa hauteur, Start et End restent aux mêmes instants ; une étape d'annulation.
- **Root** : la note qui joue l'échantillon à sa fréquence, et **Fine** (−100 à +100 cents), qui accorde le sample à partir d'elle, aperçu et export SNDH compris.
- **Load...** (ou clic sur la forme d'onde, ou un fichier déposé dessus) : un fichier audio (rééchantillonné à la fréquence) ou des octets 8 bits bruts (`.raw`, `.spl`, `.sam`). Quand l'emplacement contient déjà un sample, une alerte demande avant de le remplacer, comme dans la fenêtre Digidrums. **Play** joue le sample une fois de Start à End ; avec Loop, il répète cette partie jusqu'à un nouveau clic sur Play ou Loop ou la fermeture de la fenêtre, Play restant allumé pendant ce temps. **Clear** vide l'emplacement : sample, nom, Start et End.
- **Start** et **End** (en trames) : la partie du sample jouée, avec ou sans Loop (End est la trame qui suit la dernière jouée) ; charger un fichier les règle sur le sample entier. La forme d'onde grise ce qui est en dehors. L'export SNDH écrit le sample jusqu'à End.
- **Loop** : répète Start à End tant que la note est tenue ; sinon la partie joue une fois, jusqu'à End.
- **Format** : **Mono** (un octet par trame ; un fichier stéréo est mixé en mono) ou **Stereo** (octets entrelacés gauche, droite). La forme d'onde d'un sample stéréo montre ses deux canaux, marqués **L** et **R** en haut à droite.
- **Tools** : Normalize, Reverse, Fade out, **Sign** (inverse le bit de poids fort de chaque octet : signé ↔ non signé), −3 dB, +3 dB, comme dans la fenêtre Digidrums. Avec un sample stéréo, **L** et **R** choisissent les canaux traités (les deux allumés par défaut, au moins un) ; Normalize sur les deux canaux les monte d'un même gain, en gardant leur balance.
- **Banks** (sous Fine) : enregistrer les samples (réglages, octets et noms ; huit au moins, jusqu'au dernier chargé) en banque nommée, charger ou supprimer une banque. La bibliothèque de banques (`DMA.json`) est partagée par tous les projets.

Charger un fichier ou une banque ne peut pas être annulé.

### 6.6 Mémoire de l'Atari

Un Atari garde ses échantillons dans sa mémoire, que se partagent les Digidrums et les échantillons DMA : sur une vraie machine, un morceau ne peut pas en utiliser davantage. **RAM**, dans Options > Settings (SOUND ENGINE), choisit la mémoire de l'Atari pour lequel le projet est écrit : **512 Ko**, **1 Mo**, **2 Mo** ou **4 Mo** (par défaut, un STE au maximum). Elle est gardée avec le projet ; la changer ne s'annule pas. Les échantillons disposent de cette mémoire moins 128 Ko, laissés au TOS, à la routine de replay et au morceau ; en Dual chip, qui joue deux Atari, ils en ont deux fois plus.

**Memory** montre ce qu'en prennent les échantillons, avec une jauge : les Digidrums comptent un octet par valeur (l'échantillon entier, quels que soient Start et End), les échantillons DMA un octet par octet, et seulement avec Computer STE (sur le ST ils ne jouent pas). La limite vaut en **Authentic 3ch** et **Dual chip**, les modes qu'un vrai Atari peut jouer ; en **16 ch** et **32 ch**, qu'aucun Atari ne joue, il n'y en a pas : les fenêtres ne montrent pas Memory et Settings grise RAM.

Quand les échantillons prennent plus que ce que la mémoire leur laisse, ils sont comptés dans l'ordre, les Digidrums 1 à 127 puis les échantillons DMA 1 à 127 : le premier qui ne tient pas et tous ceux qui le suivent sont **muets**, barrés dans les fenêtres et dans le cadre GENERATOR, et la jauge passe au rouge. Ils sonnent de nouveau dès que la mémoire suffit (une RAM plus grande, un échantillon vidé ou raccourci). Les exports les laissent de côté.

Avant une action qui rendrait des échantillons muets, une alerte dit combien prennent les échantillons et combien la mémoire leur laisse, avec **Cancel** (par défaut, Entrée ou Échap) et **Proceed** : passer de 16 ou 32 ch à Authentic 3ch ou Dual chip, passer de Computer ST à STE (les échantillons DMA se mettent à compter), choisir une RAM plus petite, charger un fichier d'échantillon ou une banque, et importer un morceau avec des échantillons (XM, MOD, maxYMiser, YM-Tracker, TTrak, YM ou SNDH avec digidrums).

---

## 7. MIDI

### 7.1 Routage

MIDI routing, dans le cadre MIDI d'Options > Settings :

| Routage | Les notes reçues jouent |
|---|---|
| **Omni** (par défaut) | La piste courante (paramètre d'hôte Focus Track, 1 à 32), activée ou non |
| **Layer** | La piste courante et toutes les pistes activées à la fois (aussi le bouton Layer) |
| **Per channel** | Le canal MIDI n joue la piste n (pistes 1 à 16 ; les pistes 17 à 32 jouent en Omni ou en Layer) |

Le clavier à l'écran joue toujours la piste courante. Le canal d'une piste DMA ne va qu'à cette piste, quel que soit le routage.

**Pads de percussion.** Les pads d'un contrôleur MIDI en mode Drum (un Novation Launchkey, par exemple) envoient les notes 36 à 51 sur le canal 10, le canal des percussions General MIDI. Sur le canal choisi par **Drum pads** (Settings, cadre MIDI ; Channel 10 par défaut), les notes 36 à 43, la rangée du bas de huit pads, jouent les sons Chipdrum 1 à 8 (Kick à Tom 2), et les notes 44 à 51, la rangée du haut, les pads Digidrum 1 à 8 (le Digidrum que joue chacun, 3.8), quel que soit le routage. Un pad joue sur la piste courante quand elle est active et a son son (Chipdrum ou Digidrum), sinon sur la première piste active qui a son son, sinon sur la première piste de percussion active, sinon sur la première piste qui a son son, sinon sur la piste courante ; le coup joue jusqu'au bout, et relâcher un pad Digidrum arrête sa boucle. La vélocité agit comme sur une touche. Les autres notes du canal et sa pression ne font rien. Quand la piste courante joue le son du pad, le pad s'allume à l'écran et s'enregistre dans le séquenceur comme un clic sur le pad. **Off** rend le canal au routage, comme les autres canaux. Avec MPE sur On, chaque canal joue une note : Drum pads est grisé et ne fait rien.

**Touches de percussion.** Sur les autres canaux aussi, les touches Do2 à Ré#3 (36 à 51) jouent les pads quand elles arrivent sur une piste Chipdrum ou Digidrum (la piste du canal avec le routage par canal, la piste courante sinon) : Kick Do2, Snare Do#2, Clap Ré2, Hat Ré#2, Open Hat Mi2, Closed Hat Fa2, Tom 1 Fa#2, Tom 2 Sol2, puis les pads Digidrum 1 à 8 de Sol#2 à Ré#3. Des pads qui envoient sur le canal du clavier, ou un hôte qui envoie toutes les notes sur le canal 1, jouent ainsi le kit. Les lignes du Step Sequencer restent à leur place ; les autres touches d'une piste de percussion jouent comme avant. Avec **Launch** allumé dans le Step Sequencer, Do2 à Si2 choisissent à la place les motifs et les banques (5.2).

### 7.2 Vélocité, pitch bend, molette de modulation, aftertouch et MPE

Ces réglages sont dans le cadre MIDI d'Options > Settings (section 3.12). Les percussions (Chipdrum, Digidrum) ignorent la molette, l'aftertouch et le slide MPE.

- **Velocity** : **Accent** (par défaut : un gain audio en 16 et 32 ch, le volume 4 bits en Authentic 3ch), **Vol 4-bit** (le volume 4 bits dans tous les modes) ou **Off** (les notes enregistrées dans le Step Sequencer gardent VOL 15).
- **Velocity curve** et **Aftertouch curve**, chacune son réglage : **Linear** (par défaut) laisse les valeurs du contrôleur telles quelles ; **Soft** relève les touchers légers (une vélocité 64 joue comme 90), pour un clavier qui demande de la force ; **Hard** les abaisse (64 joue comme 32), pour un clavier trop sensible ; **S-curve** aplatit les deux bouts et raidit le milieu. 0 et 127 restent tels quels. Elles agissent sur ce que joue un contrôleur (notes, pads de percussion, pression du canal et polyphonique, pression MPE) et sur le VOL enregistré, pas sur le clavier à l'écran ni sur un fichier MIDI importé. Une courbe est grisée tant que son réglage est sur Off (Velocity Off, Aftertouch Off), la courbe choisie gardée.
- **Pitch bend** : ± Bend range (0 à 24 demi-tons, 2 par défaut). **Bend** choisit ce qu'il pilote : **Pitch** (par défaut), **Volume** (le centre laisse le volume 4 bits tel quel ; le haut le monte vers 15, le bas le descend vers 0) ou **Envelope** (la fréquence d'enveloppe bouge des demi-tons du bend, la note ne bouge pas : un balayage du timbre en Sync Buzzer et avec les sources Env).
- **Molette de modulation** (CC 1) : **Wheel** choisit ce qu'elle pilote, de rien en bas à tout en haut : **Vibrato** (par défaut, jusqu'à Wheel depth, 0 à 200 cents, 50 par défaut), **Volume** (le volume 4 bits monté vers 15), **Envelope** (la fréquence d'enveloppe, jusqu'à une octave plus haut), **LFO** (la profondeur du LFO de la piste : rien en bas, la profondeur réglée dans la fenêtre Modulation en haut) ou **Off**.
- **Aftertouch** : la pression des touches, de tout le canal (channel pressure) ou de chaque note (aftertouch polyphonique) ; une note prend la plus forte des deux. **Aftertouch** offre les mêmes choix que Wheel (Vibrato par défaut). Quand la molette et l'aftertouch pilotent la même chose, la plus forte des deux joue.
- **MPE** (16 et 32 ch seulement) : avec MPE sur On, le canal 1 joue comme d'habitude et ses contrôleurs agissent sur toutes les notes, tandis que les canaux 2 à 16 jouent chacun leur note, que leur pitch bend (± **MPE bend range**, 48 demi-tons par défaut), leur pression (le choix Aftertouch) et leur CC 74 (**MPE slide**) bougent seule. MPE slide pilote le **Volume** (par défaut : 64 laisse le volume 4 bits tel quel, 127 donne 15, 0 donne 0), l'**Envelope** (une octave dans chaque sens) ou le **Vibrato** (de 64 jusqu'à Wheel depth à 127). Les notes vont à la piste courante (ou à toutes les pistes en Layer) ; le routage Per channel ne s'applique pas.

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
| 84 | Piste courante (valeur / 4 + 1 : 0-3 piste 1, 124-127 piste 32) |
| 120, 123 | All notes off |
| 121 | Remise à zéro du pitch bend, de la modulation et de la pression du canal (avec MPE sur On, sur les canaux 2 à 16 : leur bend, leur pression et leur slide) |

Un contrôleur appris avec MIDI learn (7.6) a priorité sur ce tableau. Les SysEx sont ignorés ; l'aftertouch est à la section 7.2.

### 7.4 Program Change

Program Change n sélectionne l'instrument n (1 à 127) pour les notes jouées ensuite, sur la piste du canal (Per channel), la piste courante (Omni) ou toutes les pistes (Layer). Le programme 0 rend à la piste son propre son. Les hôtes qui numérotent les programmes à partir de 1 affichent n + 1. Un pas du séquenceur dont la ligne INS nomme un instrument garde la priorité. Le choix n'est pas enregistré dans le projet (l'hôte renvoie son Program Change à la lecture). Les pistes de percussion n'ont pas d'instruments.

### 7.5 Horloge MIDI

- Les arpégiateurs en **Clock: MIDI ext** avancent sur l'horloge MIDI à la division choisie (dans un DAW, sur le temps de l'hôte).
- Le séquenceur suit le transport de l'hôte dans un DAW et sa propre horloge dans l'application autonome (5.9).

### 7.6 MIDI learn

1. Options > **MIDI learn**. Un bandeau en bas à gauche affiche « MIDI learn: click a slider or button, then move a control... ».
2. Cliquez sur un curseur ou un bouton de STYM2149 (le clic l'arme sans le modifier).
3. Tournez un potentiomètre, ou appuyez sur un pad ou une touche de votre contrôleur. Le bandeau affiche l'association, par exemple `CC74 -> [ef]`.
4. Cliquez sur une autre commande, ou quittez avec Options > **Stop MIDI learn**. **Échap** désarme la commande armée, le MIDI learn restant actif, pour en armer une autre.

Un curseur appris suit le contrôleur sur toute sa plage ; un bouton appris est enfoncé quand la valeur monte au-delà de 64 (une note compte pour 127, son relâchement pour 0). Une note apprise ne joue plus de son. Les messages de mode de canal (CC 120 à 127) ne sont jamais appris. Le canal est ignoré.

La table MIDI est une préférence partagée par toutes les instances et tous les projets (section 9) ; chaque copie de l'application autonome a la sienne. **Clear MIDI map** la vide. Les commandes apprises ne répondent que tant que la fenêtre du plugin est ouverte ; un volume du Mixer appris fonctionne avec la fenêtre Mixer fermée.

Dans le Step Sequencer, **Launch** et les boutons **Bank** (A à D) et **Pattern** (1 à 8) s'apprennent aussi : un bouton du contrôleur peut allumer et éteindre Launch. Une touche apprise pour un bouton Bank ou Pattern remplace sa touche par défaut (Do2 à Si2, 5.2) et n'agit que tant que Launch est allumé ; la touche par défaut ainsi libérée rejoue des notes.

### 7.7 Automation de l'hôte et paramètres

STYM2149 expose 1102 paramètres d'hôte : 46 paramètres globaux (Master, Voice Mode, Chip Clock, Output Chain, Resampling, STE Bass, STE Treble, VBL Rate, Soft Clip, Timer, Focus Track, MIDI Routing, Velocity, Bend Range, Wheel Depth, Arp Clock, Arp Division, Free Tempo, Swing, Gate, Gate All, Computer, Chip Clock Hz, Arp Sync, Master 2, Master Pan, Master Link, Delay On, Delay Sync, Delay Division, Delay Time, Delay Feedback, Delay Hi Cut, Delay Level, Delay Ping-Pong, Tuning A4, Bend Target, Wheel Target, Aftertouch, MPE, MPE Bend Range, MPE Slide, Drum Pads, Velocity Curve, Aftertouch Curve, Master Mono) et 33 pour chacune des 32 pistes, nommés « T1 Level », « T2 Env Period », etc. (On, Level, Pan, Mute, Solo, Tone, Noise, Noise Period, Volume, Vol Mode, Env Period, Env Shape, Generator, Timer Ratio, SID Min Vol, Vibrato Depth, Vibrato Rate, Attack, Decay, Sustain, Release, ADSR Mode, Voicing, Arp On, Arp Rate, Gate, Vibrato On, Send, Level R, Mute R, Solo R, Link, Mono). Les quinze réglages MIDI (MIDI Routing, Velocity, Bend Range, Wheel Depth, Arp Clock, Arp Division, Bend Target, Wheel Target, Aftertouch, MPE, MPE Bend Range, MPE Slide, Drum Pads, Velocity Curve, Aftertouch Curve), Chip Clock Hz (la fréquence de l'horloge Custom) et Tuning A4 ne sont pas automatisables.

Les motifs, le morceau, les instruments, les séquences, le kit et les échantillons ne sont pas des paramètres : ils sont enregistrés dans l'état du projet.

### 7.8 Sorties

En plus de la sortie stéréo principale, le plugin propose 32 sorties mono de piste (« Track 1 » à « Track 32 ») et 2 sorties mono de puce (« Chip 1 », « Chip 2 »), désactivées par défaut. Activez-les dans votre hôte. Une sortie de piste transporte la piste seule après son mixeur (niveau, muet, solo), sans panoramique ; une sortie de puce transporte chaque puce dans les modes à 3 voix et reste silencieuse en 16 et 32 ch. Une sortie activée pendant que le plugin tourne reste silencieuse jusqu'à ce que l'hôte prépare à nouveau le plugin. Le plugin n'a pas d'entrée audio. Il annonce une queue de relâchement de 2 secondes et une petite latence avec le rééchantillonnage Sinc.

---

## 8. Fichiers

Les boîtes de dialogue de fichiers sont celles de votre système. Elles s'ouvrent dans le dernier dossier que vous avez utilisé.

### 8.1 Fichiers de projet

File > Load, Save et Save as lisent et écrivent les fichiers binaires propres à STYM2149, au format de l'état que votre hôte enregistre (décrit champ par champ dans `docs/FILE_FORMAT.md` des sources) :

| Fichier | Nom par défaut | Contenu |
|---|---|---|
| Preset | `stym2149.stym` | Le projet entier, y compris les instruments, les séquences, le morceau, les échantillons DMA et Digidrum. Le charger arrête toutes les notes et ne peut pas être annulé |
| Pattern | `stym2149.stympat` | Un motif (32 pistes) ; se charge dans le motif courant |
| Song | `stym2149.stymsng` | Le morceau et les motifs créés |
| Drum kit | `stym2149.stymkit` | Le kit Chipdrum |

**Save** réécrit le dernier fichier utilisé pour ce type (et le demande la première fois) ; **Save as** demande toujours. Dans l'application autonome, File > Configuration enregistre le même projet avec la dernière prise de l'enregistreur dans un fichier `.stymcfg`, que le démarrage suivant rouvre (2.4). Le type est écrit dans le fichier : un fichier d'un autre type est refusé avec un message qui dit ce qu'il contient (« This file holds a drum kit. »). Un fichier écrit par une version plus récente de STYM2149 est refusé et rien ne change.

Projets, motifs, morceaux et kits n'existent que sous ces fichiers : STYM2149 ne lit ni n'écrit de fichier de projet JSON. Le seul fichier JSON que vous échangez est la bibliothèque de patches (`stym2149-patches.json`, Import et Export de la fenêtre Patches, 6.2) ; celle qu'écrit une version plus récente de STYM2149 est lue pour ce que cette version connaît, et un message le signale.

### 8.2 Import

File > Play... joue un fichier dans le YM/MOD Player (4.5), sans modifier le projet : morceaux YM et SNDH, fichiers STYM2149, morceaux maxYMiser et YM-Tracker, modules TTrak, XM, MOD, S3M et OKT.

| Import | Accepte | Résultat |
|---|---|---|
| MIDI file | Fichier MIDI standard, format 0 ou 1, 8 MB au plus | Motifs et morceau (ci-dessous) |
| Tracker song | XM (Fasttracker II 1.04), MOD (ProTracker, 4 à 32 voies, FLT8 et modules Soundtracker à 15 samples compris), S3M (Scream Tracker 3), OKT (Oktalyzer), morceau maxYMiser (`.mys` et son `.myv`), morceau YM-Tracker (`.ymt`), module TTrak (`.tpm`) ; 64 MB au plus | Motifs, morceau et instruments ; échantillons DMA pour les modules (ci-dessous) |
| SNDH / YM file | SNDH pour Atari ST ou STE (un sous-morceau), YM2 à YM6 (compressés LHA ou non) | Motifs, morceau et instruments des pistes 1 à 3, en Authentic 3ch (ci-dessous) |

**Tracker song** prend tous les formats de tracker par une seule entrée : c'est le contenu du fichier qui dit lequel il est (la signature d'un module XM, S3M ou OKT, un morceau ou un jeu d'instruments maxYMiser, un morceau YM-Tracker, un module TTrak, sinon un module MOD qui se lit), jamais son extension. Le message nomme le format trouvé (« S3M imported ») ; un fichier d'aucun de ces formats est refusé (« Tracker song not imported »). **SNDH / YM file** lit de même l'un ou l'autre type de morceau, d'après son contenu.

**Import MIDI.** File > Import > MIDI file, ou déposez un fichier `.mid` ou `.midi` n'importe où sur la fenêtre du plugin (le premier fichier MIDI si vous en déposez plusieurs). Un pas vaut un pas de la résolution du séquenceur (une double croche en 1/16). Les motifs prennent la longueur du fichier, arrondie à des mesures entières de la signature rythmique : un seul motif jusqu'à 256 pas ; un fichier plus long remplit des motifs d'autant de mesures entières que 256 pas en contiennent, le dernier prenant les mesures restantes. 32 motifs au plus (le reste est abandonné et signalé). Les pistes remplies perdent leur longueur propre (Len) et jouent tout le motif. Un fichier d'un seul canal va à la piste courante : sur une piste Chipdrum ou Digidrum, il est lu comme des percussions quel que soit son canal (une boucle de batterie est souvent sur le canal 1), ses notes donnant les sons du kit. Sinon les canaux vont aux pistes du mode (pistes 1–16 en 16 et 32 ch ; pistes 1–3 en Authentic 3ch ; 1–3 et 5–7 en Dual chip), les canaux mélodiques d'abord, le canal 10 en dernier. Le canal 10 (percussions General MIDI associées au kit) est joué par Chipdrum ou Digidrum : quand sa piste joue déjà l'un des deux, elle le garde ; sinon une boîte demande lequel (Chipdrum par défaut, Cancel n'importe rien). Avec Digidrum en Authentic 3ch et Dual chip, le canal 10 va à la piste 4, la piste Digidrum, et les trois canaux de la puce restent aux canaux mélodiques ; les autres canaux deviennent des pistes Chip Voice ; les notes longues sont liées. VOL reçoit la vélocité ramenée à 0–15, sur chaque pas de la note. Quand deux notes commencent ensemble sur un même canal, une seule est gardée (pas d'accords). L'import commence au motif courant et ne remplace que les pistes qu'il remplit : les autres pistes gardent leurs notes et leur état. Un motif dont d'autres pistes contiennent des notes garde sa longueur quand la partie du fichier est plus courte (la partie s'y répète) et prend celle de la partie quand elle est plus longue (les autres pistes répètent leurs pas). Le tempo du fichier n'est appliqué que si les autres pistes sont vides ; sinon le tempo reste, et le message de l'import le dit. Le morceau reçoit une entrée par motif seulement s'il était vide. Quand les pistes remplies contiennent des notes dans ces motifs, une boîte demande d'abord, par exemple, « Track 3 holds notes. Replace them? » : Cancel, le bouton par défaut, n'importe rien ; un import sur une piste vide ne demande rien. Une piste Chipdrum ou Digidrum devient Chip Voice pour une partie mélodique ; les pistes SID Voice, Sync Buzzer et DMA gardent leur générateur. L'import entier constitue une seule étape d'annulation.

**Import XM.** La table d'ordre du module devient le morceau (avec un point de reprise là où il boucle), ses motifs deviennent les motifs 1 à n (32 au plus). Computer passe à STE ; les canaux vont aux pistes qui jouent le son DMA (les 16 en 16 ch, les 32 en 32 ch, réglées sur le générateur DMA ; pistes 5–12 en Authentic 3ch ; 9–16 en Dual chip). En 16 et 32 ch, un module de 8 canaux ou moins va lui aussi sur les pistes 5–12, celles du DMA en Authentic 3ch, pour que le projet puisse passer en 3ch et continuer de le jouer. Un module qui joue plus de canaux que le mode n'a de pistes (plus de 8 en Authentic 3ch ou en Dual chip, plus de 16 en 16 ch) demande d'abord où il va : **16 ch** (jusqu'à 16 canaux) ou **32 ch** (au-delà), le choix par défaut, passe Voices dans le mode qui les joue tous ; **3ch**, **2x3ch** ou **16 ch** garde le mode, avec ses 8 pistes DMA ou ses 16 pistes ; **Cancel** n'importe rien. Quand le module a plus de canaux que de pistes, un canal va sur la piste de canaux qui ne sonnent jamais sur les mêmes lignes (une note sonne de sa ligne jusqu'à son relâchement, la fin de son échantillon quand il ne boucle pas, une coupure ou la note suivante du canal), la piste prenant ses lignes là où il sonne ; un canal qui sonne sur les lignes de toutes les pistes est laissé de côté, et le message compte les uns et les autres. File > Play joue un tel module en 16 ou 32 ch sans demander. Les instruments les plus joués qui ont des échantillons, 127 au plus, deviennent les instruments 1 à n et les échantillons DMA 1 à n (8 bits, entiers, fréquence STE la plus proche ; une boucle qui ne commence pas l'échantillon est déroulée sur 32 Ko, le STE ne bouclant que des échantillons entiers). Si le tempo du module est constant et correspond à 125, 150, 175 ou 250 BPM, l'import utilise le cadencement Ticks à 50, 60, 70 ou 100 Hz ; sinon le cadencement BPM, où chaque entrée du morceau reçoit le tempo de sa position et un changement de tempo au milieu d'un motif (`Fxx`) devient une commande `Txx` sur sa ligne. Les effets qui ont une commande en deviennent une sur leur ligne : `Axy` → `Axy` (jusqu'à 15 ; un glissé XM dure sa ligne, la ligne suivante reçoit donc `A00`) ; `9xx` → `Oxx` (mis à l'échelle de la fréquence DMA du sample) ; `EDx` → `Dxx` ; `ECx` → `Cxx` ; `E5x` → `Fxx` pour sa note. L'arpège `0xy` et les portamentos `1xx` et `2xx` sont laissés de côté. Les pas d'une note tenue portent son volume (ligne VOL). Un message liste ce qui n'a pas pu être importé.

**Import MOD.** File > Import > Tracker song prend un module ProTracker (`.mod` : signatures M.K., M!K!, FLT4, 6CHN, 8CHN, 16CH et semblables, le FLT8 de StarTrekker, et les anciens modules Soundtracker à 15 samples) comme l'import XM ci-dessus, Computer passant à STE. Les notes viennent des périodes Amiga (le C-2 de ProTracker, période 428, est C4) ; les samples sont pris à la fréquence PAL de l'Amiga (la période 428 joue 8287 Hz), avec leur accord fin et leur boucle ; les voies gardent le panoramique de l'Amiga, 1 et 4 à gauche, 2 et 3 à droite, comme le font les lecteurs MOD du STE. `E5x` règle l'accord fin de sa note et `E8x` le panoramique de sa voie ; les autres effets sont lus comme dans un XM. Comme pour un XM, les 127 samples les plus joués au plus reçoivent un emplacement DMA.

**Import S3M.** Un module Scream Tracker 3 (`.s3m`) suit lui aussi le chemin de l'import XM, Computer passant à STE. Ses voies PCM vont aux pistes dans l'ordre ; ses voies et instruments AdLib sont laissés de côté (STYM2149 n'a pas de puce OPL), et le message dit combien. Les notes gardent leur octave (le C-4 du S3M est C4) et chaque sample joue à son propre C2Spd ; les voies gardent le panoramique de ST3 (3 sur 15 à gauche, 12 à droite, toutes au centre dans un module mono) ou la table de panoramiques du module. Les commandes qui ont un équivalent XM que l'import rend sont lues comme tel : `Axx` vitesse, `Bxx` saut, `Cxx` fin de motif, `Dxy` glissé de volume, `Oxx` décalage, `Txx` tempo, `Xxx` et `S8x` panoramique, `S2x` accord fin, `SCx` coupure, `SDx` retard. Les glissés de volume fins (`DxF`, `DFy`) et les autres commandes sont laissés de côté et listés sous leur nom S3M. Les samples non signés et signés, 8 et 16 bits, sont lus ; un sample stéréo est mélangé en mono.

**Import OKT.** Un module Oktalyzer (`.okt`, Amiga) suit le même chemin. Chacune de ses quatre voies Amiga est une voie, ou deux quand elle est partagée en voix mixées (8 voies au plus) ; elles gardent le panoramique de l'Amiga, 1 et 4 à gauche, 2 et 3 à droite, les deux voix d'une voie partagée de son côté. Ses notes (1 à 36, C-1 à B-3 de ProTracker : C-2 est C4) jouent les samples à la fréquence PAL de l'Amiga, comme pour un MOD ; une note d'une voie partagée joue à plein volume, comme dans Oktalyzer. Les ticks battent à 50 Hz à la vitesse du module. `P` (saut de position), `S` (vitesse), `V` (volume et ses glissés) et `R` (relâchement) sont lus ; les glissés fins de `V` et les autres effets (arpèges, portamentos, glissés de notes, filtre) sont laissés de côté et listés sous leur nom Oktalyzer.

**Import YM et SNDH.** File > Import > SNDH / YM file (`.sndh .snd .ym .ym2 .ym3 .ym5 .ym6`, le format lu d'après le contenu) change un morceau en motifs que vous pouvez éditer, là où File > Play... ne fait que le jouer. Les trames de registres du morceau (celles du fichier YM, ou celles que l'Atari émulé écrit en jouant le SNDH : sa durée indiquée, sinon 3 minutes, 10 minutes au plus) deviennent les notes de ses trois voix sur les pistes 1 à 3. Un SNDH à plusieurs sous-morceaux demande d'abord lequel importer. Une boîte montre ensuite la grille devinée d'après les débuts de notes (« Grid guessed from 54 notes: 98 % of them start on a step. ») et vous laisse corriger **Frames per step** (une trame est un tick de lecture du morceau, 50 Hz pour la plupart) et **Steps per pattern** ; Import est le choix par défaut, Cancel n'importe rien. Le projet passe en Authentic 3ch (MPE Off), pistes 1 à 3 allumées en Chip Voice au Level 100 % et les autres pistes éteintes ; la fréquence VBL devient la plus proche de celle du morceau et le Tuning A4 celui sur lequel le morceau est accordé. Le timing passe en Ticks avec les trames par pas choisies comme Speed, sans groove. Chaque note reçoit sa NOTE et son OCT (avec `Fxx` quand elle s'écarte de 5 cents ou plus), INS, SRC, ENV quand elle joue l'enveloppe, et TIE sur ses pas ; une note qui commence au milieu d'un pas attend sa trame avec `Dxx`, une note qui finit au milieu d'un pas reçoit son GAT. Ce que fait une note trame par trame (hauteur, volume, mixeur, période de bruit) devient la table de son instrument, une trame par pas sur 64 pas (Chip Voice avec la période d'enveloppe, ou Sync Buzzer pour un buzzer accordé sur la note) ; les notes qui jouent la même chose partagent un instrument, et au-delà de 32 les plus proches fusionnent. Les motifs 1 à n et les instruments 1 à k sont remplacés (32 motifs au plus, des motifs plus longs étant pris avant de couper le morceau) ; le Song les joue dans l'ordre et reprend là où le morceau boucle. Tout l'import est une seule étape d'annulation. Le message liste ce que les motifs ne jouent pas comme le morceau (« Not as in the tune: ») : effets de timer (voix SID, digidrums, sync buzzers), DAC du STE, notes fusionnées dans l'instrument le plus proche, etc. Un morceau qui ne joue aucune note, ou seulement par des effets de timer ou des échantillons, est refusé.

**Import maxYMiser.** File > Import > Tracker song prend un morceau maxYMiser (`.mys`) et son jeu d'instruments (`.myv`) : choisissez l'un ou l'autre, l'autre est le fichier de même nom à côté, sinon une seconde boîte le demande. Le morceau est joué une fois par le modèle de la routine de maxYMiser qu'a STYM2149, et ses trames deviennent des motifs comme pour un import YM, avec ce que dit le morceau : chaque ligne est un pas (la vitesse du morceau comme Speed en timing Ticks, `Sxx` là où elle change), chaque motif du morceau une partie du Song, qui reprend à la position de reprise du morceau ; chaque ligne qui lance une note donne une note, dont l'instrument suit les trames qui la suivent. Le projet passe en Authentic 3ch avec les pistes 1 à 3, comme pour un fichier YM. Les effets timer deviennent ceux du moteur : un SID devient un instrument SID Voice à forme d'onde Custom, un sync buzzer un instrument Sync Buzzer avec ses formes et une enveloppe accordée ; les digidrums vont sur la piste 4, en générateur Digidrum, avec les samples du jeu d'instruments dans les emplacements 1 à 8. Un square FM devient un instrument FM Voice en Semitones, ses pas tels que dans le morceau, son Timer ratio tiré de la fréquence du timer. La largeur d'impulsion, la FM du buzzer et le sync square ne sont pas repris, et le message le dit. Tout l'import, emplacements compris, s'annule en une fois. Un SNDH enregistré par maxYMiser garde son morceau et son jeu d'instruments après la routine de replay : File > Import > SNDH / YM file les trouve et importe le morceau de cette façon, sans la boîte de grille (avec plusieurs morceaux, le sous-morceau choisi en désigne un) ; un morceau sans note est lu à partir de la routine, comme tout SNDH.

**Import YM-Tracker.** File > Import > Tracker song prend un morceau enregistré par YM-Tracker (`.ymt`) et fait de ses lignes des pas. La table des positions devient le Song, parcourue comme pour un XM : un saut `Bxx`, la fin de la table ou le retour à une position déjà jouée donne le point de reprise, et un `Dxx` termine là son motif ; les motifs rencontrés deviennent les motifs 1 à n (32 au plus). Le projet passe en Authentic 3ch : les trois premières voies de la première puce vont sur les pistes 1 à 3, la voie des digidrums et les frappes `Zxx` sur la piste 4 (Digidrum) avec les échantillons du morceau dans les emplacements 1 à 8 à leur propre fréquence ; les autres voies ne sont pas reprises. Un morceau à un seul tempo de 125, 150, 175, 250 ou 500 BPM passe en timing Ticks à sa vitesse (un tick de YM-Tracker dure 2,5 / BPM secondes : 50, 60, 70, 100 ou 200 Hz), avec `Sxx` là où `Fxx` change la vitesse ; tout autre morceau passe en timing BPM, ses changements de tempo sur la ligne BPM. Une note (le C-5 de YM-Tracker est C4) tient jusqu'à la suivante, son note off ou son `Kxx` (GAT) ; la colonne de volume, `Cxx`, `EAx` et `EBx` donnent VOL sur chaque pas tenu, `Axy`, `5xy` et `6xy` la glissade de volume de leur ligne, `3xx` SLD sur la note d'avant, `ECx` et `EDx` les commandes `Cxx` et `Dxx`, `Rxx` lui-même. VOL 1 joue le volume 4 bits 8 d'un son de volume 15 : les lignes plus douces sont jouées là, et le message les compte. Chaque instrument du morceau, avec les effets que jouent ses notes (arpège `0xy`, vibrato `4xy`, SID `Qxx`, SID sinus `Sxx`, Sync-Buzzer `Uxx`, enveloppe `Xxx`, `Pxx`, `Yxx`, `Mxx`), devient un instrument (32 au plus) : son ADSR en ticks, ton et bruit, ses macros d'arpège, de hauteur et de volume dans la table Modulation, son vibrato, et son enveloppe matérielle en Sync Buzzer accordé à la note ou à sa période fixe. Tout l'import s'annule en une fois ; le message liste ce que les motifs ne jouent pas comme le morceau (« Différences avec le morceau : »).

**Import TTrak.** File > Import > Tracker song prend un module enregistré par TTrak 1.04 (`.tpm`). Comme pour un morceau maxYMiser, le module est joué une fois en entier par le modèle de la routine de lecture de TTrak qu'a STYM2149, et ses trames deviennent des motifs avec ce que dit le module : chaque ligne est un pas (la vitesse et le groove du module en Speed du timing Ticks à sa fréquence de lecture, `Sxx` là où ils la changent), chaque position du morceau une partie du Song, qui reprend à la position de reprise du module ; chaque ligne qui donne une note ou un instrument donne une note, son instrument suivant les trames qui en partent. Le projet passe en Authentic 3ch avec les pistes 1 à 3, accordé sur la table de hauteurs de TTrak (La4 à 438,8 Hz). Les effets de timer deviennent ceux du moteur : un SID (carré, 2 ou 16 niveaux, morph) devient un instrument SID Voice à forme d'onde Custom, un sync buzzer un instrument Sync Buzzer avec ses quatre formes, une FM du ton un instrument FM Voice avec ses quatre pas (les x1, x½, x⅓ et x⅔ de la période de TTrak deviennent x1, x2, x3 et x1.5 de la fréquence) ; les digidrums de la voie qui en joue le plus vont sur la piste 4, générateur Digidrum, avec les échantillons 4 bits du module (les huit premiers joués) dans les emplacements 1 à 8. Tout l'import, emplacements compris, s'annule en une fois.

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
| maxYMiser | | ✓ | Un morceau (`.mys`) et son jeu d'instruments (`.myv`) de même nom, pour maxYMiser sur Atari ST |
| YM-Tracker | | ✓ | Un morceau YM-Tracker (`.ymt`) nommé d'après son fichier |
| TTrak | | ✓ | Un module TTrak 1.04 (`.tpm`), son titre le nom du fichier (16 caractères) |

- Export song est grisé, sans son sous-menu, tant que le morceau n'a aucune entrée ; son infobulle le dit. Les exports tournent en arrière-plan ; un seul à la fois.
- Les exports audio sont calculés hors ligne : ils s'arrêtent exactement quand le séquenceur s'arrête, sans queue de relâchement, et sont coupés à une heure de musique (ou à la limite de 4 GB du WAV et du ZIP).
- Les **exports de registres** (VBL, YM3, YM6, SNDH) demandent **Authentic 3ch** (une puce, trois voix). Ils capturent ce que vous entendez, une trame par tick de replay. Après l'export, un message liste ce que le fichier ne reproduit pas, par exemple : effets de timer figés (VBL, YM3), players YM3 qui supposent 2 MHz et 50 Hz, périodes recalculées pour 2 MHz (SNDH), son DMA absent (formats qui ne l'ont pas). Les échantillons muets faute de mémoire (6.6) sont laissés de côté, et un fichier YM6 porte 32 digidrums au plus : les frappes des autres sont abandonnées. Une SID wave autre que Base (Square, Sine, Bounce, Custom) entre dans le fichier comme l'onde carrée simple, à la hauteur de la note. Une FM Voice ne joue sa FM wave qu'en SNDH : la routine de lecture écrit la période de chaque pas à chaque interruption du timer de la voie ; VBL, YM3 et YM6 n'ont aucun effet qui change une période de ton, et jouent la note seule (le message compte ces trames).
- L'**export maxYMiser** demande lui aussi Authentic 3ch. Ce que joue le morceau est écrit en lignes que la routine de maxYMiser rejoue vers les mêmes registres : une ligne par pas, une position à chaque entrée du Song (et toutes les 64 lignes), les motifs identiques d'une voie écrits une fois. Chaque note reçoit un instrument qui tient son volume, son arpège, son vibrato, son mixeur et son bruit trame par trame ; les notes qui jouent pareil partagent le même, et au-delà de 32 les moins jouées se fondent dans le plus proche. SID Voice, Sync Buzzer et Digidrum deviennent les timers SID, sync buzzer et digidrum de la routine, et une FM Voice son square FM : en Semitones ses pas sont écrits tels quels, en Ratios chaque multiple à ses demi-tons les plus proches (x5 et x0.5 approchés, plus de 8 pas ramenés à 8), et le message compte les trames jouées de façon approchée. Le message liste ce que maxYMiser ne joue pas comme on l'entend (« Différences avec l'écoute : »), par exemple des trames jouées avec un instrument fondu, des fréquences de timer hors de portée du MFP ou le son DMA du STE non repris.
- L'**export YM-Tracker** demande lui aussi Authentic 3ch. Les entrées du Song sont écrites dans l'ordre où elles jouent (répétitions et sauts Loop déroulés, 256 positions au plus), chacune un motif de ses pas, les identiques écrits une fois ; avec Loop, le morceau reprend à l'entrée de reprise, sinon il finit sur un motif muet. Les pistes 1 à 3 vont sur les voies 1 à 3 et les frappes de la piste Digidrum sur leur voie en `Zxx` ; la voie 4 porte la vitesse ou le tempo (`Fxx`). Chaque son que jouent les notes devient un instrument (16 au plus, le plus proche fondu au-delà) : ADSR, ton, bruit, enveloppe ou Sync Buzzer, vibrato, et la table Modulation en listes d'arpège, de hauteur et de volume. SLD devient `3xx`, un SID Voice `Qxx` (`Sxx` pour Sine et Bounce), une glissade de volume `Axy`, et `Cxx`, `Dxx`, `Rxx`, `Exx` leurs effets YM-Tracker, un effet par case. Le message liste ce que YM-Tracker ne joue pas comme on l'entend (« Différences avec l'écoute : »), par exemple le LFO, l'écho, les notes d'accord, les pas de la FM Voice (sa note joue sans eux) ou les pistes DMA du STE.
- L'**export TTrak** demande lui aussi Authentic 3ch. Ce que joue le Song est écrit en lignes et en instruments que la routine de TTrak rejoue jusqu'aux mêmes registres : une ligne par pas (l'effet `$1C` donne la vitesse là où elle change), un pas sur plusieurs lignes là où un instrument doit partir en son milieu (un digidrum, une enveloppe qui repart) ; une position à chaque entrée du Song et toutes les 64 lignes (`$0B` termine une position plus courte), les motifs identiques écrits une fois ; avec Loop, le module reprend à l'entrée de reprise, sinon il finit sur une position muette. Chaque note reçoit un instrument qui tient son enveloppe d'amplitude, sa table de bruit, sa hauteur (une onde de modulation de fréquence, un désaccord ou un arpège) et la forme de son buzzer, ses volumes montés à 15, l'atténuation de la ligne (`$1F`) retirant la différence ; les notes qui jouent pareil en partagent un, et au-delà de 32 les moins jouées se fondent dans le plus proche. SID Voice devient le SID carré de TTrak, Sync Buzzer son sync buzzer (ou un buzzer simple quand son enveloppe tourne déjà au rythme du timer, ce qui laisse les deux timers de TTrak aux autres), Digidrum son digidrum avec les 32 premiers échantillons, FM Voice sa FM du ton quand sa FM wave se répète tous les quatre pas avec les multiples x1, x1.5, x2 ou x3 et joue sans l'enveloppe (sinon la note seule, comptée dans le message). Le message donne la fréquence du module et l'endroit où il boucle, et liste ce que TTrak ne joue pas comme on l'entend (« Différences avec l'écoute : »), par exemple un troisième effet de timer en même temps (TTrak a deux timers), des hauteurs hors de ses tables, ou des notes jouées avec un instrument fondu.
- Le Recorder (4.4) exporte sa prise en YM6, SNDH ou VBL dump.

### 8.4 Conformité à la puce

L'indicateur **CHIP** de la barre de menus compte ce qu'un export de registres ne reproduirait pas dans le motif courant : plus de trois pistes avec des notes, Dual chip, accords sur une seule voix, notes hors de la plage de périodes de la puce, panoramique et niveaux du mixeur (le ST est mono et n'écrit que le volume 4 bits), un niveau Master différent de 70, les limites des Digidrums ou du DMA, et une FM Voice (sa FM wave ne s'entend qu'en SNDH ; trop de pas à un ratio élevé demandent un timer plus rapide que la plupart des routines du ST). Cliquez dessus pour lire la liste. « CHIP: OK » signifie que les exports VBL, YM3, YM6 et SNDH correspondront à ce que vous entendez.

---

## 9. Préférences

Les préférences sont à vous, pas à un projet : toutes les instances de STYM2149 les partagent, et chacune se règle depuis l'interface et est enregistrée immédiatement.

| Préférence | Se règle avec |
|---|---|
| Taille de la fenêtre | En redimensionnant la fenêtre |
| Infobulles | Options > Settings > Tooltips (On, Off) |
| Langue de l'interface | Options > Settings > Interface language : English, intégré, puis les langues des mêmes fichiers PO que les infobulles (leurs entrées de contexte « ui »), chacune nommée dans sa langue. L'anglais par défaut, quelle que soit la langue du système. Les menus, boutons, libellés, titres de fenêtres et alertes changent aussitôt dans toutes les fenêtres STYM2149 ouvertes ; les noms que vous donnez (patches, samples, instruments, pistes) et ceux du produit, de ses fonctions et des machines (Chip Voice, Digidrum, Atari ST...) restent tels quels. Les infobulles gardent leur propre langue : par exemple une interface en anglais avec des infobulles en français. Les infobulles nomment les menus et les boutons par leur nom anglais. Les traductions attendent encore une relecture par des locuteurs natifs (**À CONFIRMER**) |
| Langue des infobulles | Options > Settings > Tooltip language : English, intégré, puis les langues des fichiers PO par nom (Deutsch, Español, Français, Italiano, Português avec les installeurs). Par défaut, la langue de votre système si le menu l'a, sinon l'anglais. Les installeurs mettent les cinq langues dans un dossier de l'ordinateur (`C:\ProgramData\STYM2149\Languages` sous Windows, `~/.local/share/STYM2149/Languages` sous Linux, `/Library/Application Support/STYM2149/Languages` sous macOS, où on les copie à la main tant qu'il n'y a pas d'installeur macOS) ; un plugin copié sans elles n'affiche que l'anglais. Votre propre dossier Languages, à côté du fichier de préférences (`%APPDATA%\STYM2149\Languages`, `~/Library/Application Support/STYM2149/Languages`, `~/.config/STYM2149/Languages`), contient un fichier PO de gettext par langue (`nl.po`), fait avec un éditeur de texte ou un outil de traduction comme Poedit ; le menu relit les deux dossiers à chaque ouverture. **Languages folder...**, sous Tooltip language, écrit un modèle avec toutes les lignes anglaises et un README dans votre dossier, puis l'ouvre. Un de vos fichiers au nom d'une langue installée (`fr.po`) corrige ses lignes. Les traductions attendent encore une relecture par des locuteurs natifs (**À CONFIRMER**) |
| Disposition du clavier | Options > Settings > Keyboard layout : Auto (detect), QWERTY, AZERTY, QWERTZ. Auto suit la disposition de votre système (Windows, Linux ; macOS **À CONFIRMER**) |
| Noms des notes | Options > Settings > Note names : English (C D E) ou Latin (Do Re Mi) |
| Renderer | Options > Settings > Renderer (Windows seulement, grisé ailleurs) : Software (par défaut) ou Direct2D, la façon dont la fenêtre arrive à l'écran. Software ne recopie que ce qui change ; avec Direct2D, une copie de la fenêtre pouvait clignoter quelques secondes en haut à droite du bureau Windows |
| Pixels | Options > Settings > Pixels : Smooth (par défaut) ou Sharp (section 3.1) |
| Oscilloscope | Options > Oscilloscope : coché (affiché, par défaut) ou non (section 3.10) |
| Source et Merge de l'oscilloscope | Les boutons Main / Track et Merge de l'oscilloscope : Main et Merge éteint par défaut (section 3.10) |
| Register monitor | Options > Register monitor : coché (affiché, par défaut) ou non (section 3.10) |
| Note monitor | Options > Note monitor : coché (affiché, par défaut) ou non, la ligne Note du cadre SYSTEM (section 3.9) |
| MIDI monitor | Options > MIDI monitor : coché (affiché, par défaut) ou non, le moniteur MIDI du cadre SYSTEM (section 3.9) |
| Démo d'information | Options > Settings > Information demo : On (par défaut) ou Off, la démo au-dessus du texte de Fuji > Information, ou l'icône |
| Lignes du moniteur MIDI | Options > Settings > MIDI monitor : 100, 500 (par défaut), 1000 ou 5000 lignes gardées pour remonter dedans, ou Custom, de 1 à 100 000 (section 3.9) |
| Table MIDI learn | Options > MIDI learn, Clear MIDI map |
| Lignes de commandes | Step Sequencer > Resolution > Step command rows : On (par défaut) ou Off (CMD, ENV, VIB, LFO, MOD, TUN, DLY, CUT, SMP, TIK, BPM) |
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

L'application autonome garde ses périphériques audio et MIDI et la configuration enregistrée ou chargée en dernier (File > Configuration, 2.4) dans un fichier à part, `STYM2149.settings` : dans `%APPDATA%\STYM2149` sous Windows, `~/.config` sous Linux, `~/Library/Application Support` sous macOS. Elle rouvre cette configuration au démarrage suivant. Quitter n'enregistre rien ; la session que les versions précédentes enregistraient à chaque Quit est abandonnée au premier démarrage de cette version.

Vous pouvez lancer l'application autonome plusieurs fois en même temps, par exemple pour jouer de chaque copie avec son propre contrôleur MIDI. Chaque copie lancée prend un numéro, affiché dans son titre à partir de la deuxième copie (STYM2149 (2)), et garde ses propres périphériques audio et MIDI, sa configuration et la place de sa fenêtre dans un fichier à elle, à côté de `STYM2149.settings` : `STYM2149 (2).settings`, et ainsi de suite. Une copie lancée pour la première fois reprend la sortie audio de la première copie, aucune entrée MIDI (choisissez son contrôleur dans Options > Audio/MIDI settings) et un nouveau projet. Quand une copie se ferme, son numéro se libère : la copie lancée ensuite prend le plus petit numéro libre, avec les réglages de cette copie. Les préférences, la bibliothèque Patches et les banques sont communes à toutes les copies. La table MIDI learn est propre à chaque copie, puisque chaque copie peut avoir son contrôleur : la première copie garde la table apprise avant, une nouvelle copie part d'une table vide. Certains pilotes audio ne servent qu'une application à la fois (la plupart des pilotes ASIO sous Windows) : quand une nouvelle copie reprend le pilote ASIO de la première copie, une alerte le signale à son démarrage. Donnez à chaque copie sa propre sortie, ou prenez un pilote qui mélange plusieurs applications (Windows Audio).

Pour revenir aux valeurs par défaut, fermez toutes les instances de STYM2149 et supprimez le fichier de réglages.

---

## 10. Raccourcis clavier et gestes de la souris

Les raccourcis fonctionnent tant que la fenêtre de STYM2149 a le focus clavier. Savoir si un hôte donné transmet toutes les touches (par exemple Space) au plugin est **À CONFIRMER** pour chaque DAW. ^X dans les menus signifie Ctrl+X.

### 10.1 Partout

| Touches | Action |
|---|---|
| Space | Play / Stop (Free run ; en Host sync, c'est le transport de l'hôte qui décide) |
| Esc | Fermer le menu ouvert ; sinon, pendant le MIDI learn avec une commande armée, la désarmer ; sinon fermer la fenêtre au premier plan ; sinon arrêter toutes les notes (panique) |
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
| Shift+Ctrl+V / S / U / O / C / I / D | Générateur : Chip Voice / SID Voice / Sync Buzzer / FM Voice / Chipdrum / Digidrum / DMA (Computer STE) |
| Ctrl+/ ou Ctrl+? | Fenêtre SHORTCUTS : la liste des raccourcis du prototype, puis ceux qu'ajoute STYM2149 (aussi dans le menu Atari > Shortcuts) |
| Ctrl+N | Nouveau projet |
| Ctrl+Q | Quitter (application autonome seulement) |

Avec le Step Sequencer au premier plan, Ctrl+A, Ctrl+C, Ctrl+X et Ctrl+V agissent sur la grille (section 10.2) ; les raccourcis de générateur prennent Shift, ils ne se mélangent donc plus.

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
| Ctrl+A | Sélectionner toutes les notes de la piste : un cadre autour des notes, ou un bloc de tous les pas sur une piste de percussion |
| Ctrl+C / X / V | Copier / couper le cadre, le bloc ou la note sous le curseur ; coller la copie (sans sélection, la dernière copie au curseur) |
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
- Utilisez un format que votre DAW prend en charge : il n'y a ni version VST2, ni AU, ni AAX dans la 0.9.0, et Ableton Live n'a pas CLAP.
- STYM2149 est listé sous le fabricant « PRISM », comme instrument.

**Windows bloque l'installateur.** SmartScreen affiche « Windows a protégé votre ordinateur » parce que l'installateur n'est pas signé : cliquez sur **Informations complémentaires**, puis sur **Exécuter quand même**.

**Pas de son.**
- La piste courante doit être activée (son onglet), et les notes doivent être comprises entre B0 et C9.
- En routage Omni, le MIDI joue la piste courante ; en routage Per channel, le canal n joue la piste n.
- Vérifiez Mute et Solo dans le Mixer, ainsi que le niveau Master.
- Pendant que le YM/MOD Player joue un morceau, il remplace la sortie de l'instrument : fermez le YM/MOD Player.

**Le séquenceur ne joue pas dans mon DAW.** Lancez le transport du DAW (Play et Space dans le plugin ne font rien en Host sync). Assurez-vous que les pistes qui contiennent des notes sont activées. Si le motif joue une fois puis s'arrête, activez **Loop** dans le transport du Step Sequencer.

**Une sortie séparée est muette.** Les sorties activées pendant que le plugin tourne restent silencieuses jusqu'à ce que l'hôte prépare à nouveau le plugin (arrêt et redémarrage du moteur audio, ou rechargement du projet). Les sorties de puce sont silencieuses en 16 et 32 ch.

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
