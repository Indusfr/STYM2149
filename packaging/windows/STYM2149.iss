; STYM2149 — Windows installer for all users (M8, C1, D-181, D-199, D-210, D-211, D-218, D-269, D-283)
; Programming: Pascal "Indus" Nowak (with Claude's help).
; Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
;
; Built by the CI with Inno Setup 6.3 or later:
;   ISCC.exe /DAppVersion=0.9.0 [/DBuild=<CI run number>] /DArtefacts=<build-plugin\src\plugin\STYM2149_artefacts\Release>
;            /DRepo=<repository>
;            /O<output folder> packaging\windows\STYM2149.iss
; Needs administrator rights (D-199). VST3 and CLAP go to the system-wide folders that hosts scan by default
; (C:\Program Files\Common Files\VST3 and ...\CLAP), the application to C:\Program Files\PRISM\STYM2149 by default,
; the tooltip languages (PO files: French, German, Spanish, Portuguese, Italian) to C:\ProgramData\STYM2149\Languages,
; where the plugins and the application read them (D-283).
; No AAX (D-175). Not signed (D-176): Windows SmartScreen warns on the first run, as the manual explains.

#ifndef AppVersion
  #error Pass the version: /DAppVersion=x.y.z
#endif
#ifndef Artefacts
  #error Pass the JUCE artefacts folder: /DArtefacts=...\STYM2149_artefacts\Release
#endif
#ifndef Repo
  #define Repo "..\.."
#endif
; The build number (D-211), shown by Fuji > Information, goes into the file name and the name of the installed version.
#ifdef Build
  #define BuildSuffix "-build" + Build
  #define BuildLabel " build " + Build
#else
  #define BuildSuffix ""
  #define BuildLabel ""
#endif

[Setup]
AppId={{50AB0BA1-66D4-4A91-AB65-1FBDB8849998}
AppName=STYM2149
AppVersion={#AppVersion}
AppVerName=STYM2149 {#AppVersion}{#BuildLabel}
AppPublisher=PRISM
AppPublisherURL=https://github.com/Indusfr/STYM2149
AppSupportURL=https://github.com/Indusfr/STYM2149/issues
AppCopyright=Copyright © 2026 Indus / PRISM
VersionInfoVersion={#AppVersion}
PrivilegesRequired=admin
; Application folder (sixth DAW test, item 71, D-269), for an update too: the folder of the former versions
; (C:\Program Files\Indus) is not offered again, its files are removed below.
DefaultDirName={commonpf64}\PRISM\STYM2149
UsePreviousAppDir=no
DisableDirPage=no
; The old per-user plugin folders are cleaned below; they belong to the user who runs the installer.
UsedUserAreasWarning=no
DefaultGroupName=STYM2149
DisableProgramGroupPage=yes
LicenseFile={#Repo}\LICENSE
OutputBaseFilename=STYM2149-{#AppVersion}{#BuildSuffix}-windows-x64-setup
Compression=lzma2/max
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
WizardStyle=modern
; Application icon (D-218): the installer's own icon; the application carries it too (ICON_BIG and ICON_SMALL).
SetupIconFile={#Repo}\packaging\icon\STYM2149.ico
UninstallDisplayName=STYM2149
UninstallDisplayIcon={app}\STYM2149.exe

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "fr"; MessagesFile: "compiler:Languages\French.isl"
Name: "de"; MessagesFile: "compiler:Languages\German.isl"
Name: "es"; MessagesFile: "compiler:Languages\Spanish.isl"
Name: "pt"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"
Name: "it"; MessagesFile: "compiler:Languages\Italian.isl"

[CustomMessages]
; FullInstallation and CustomInstallation are [Messages] of the language files, which {cm:...} cannot read: the setup
; types get their own messages, worded as in those files.
en.TypeFull=Full installation
en.TypeCustom=Custom installation
fr.TypeFull=Installation complète
fr.TypeCustom=Installation personnalisée
de.TypeFull=Vollständige Installation
de.TypeCustom=Benutzerdefinierte Installation
es.TypeFull=Instalación completa
es.TypeCustom=Instalación personalizada
pt.TypeFull=Instalação completa
pt.TypeCustom=Instalação personalizada
it.TypeFull=Installazione completa
it.TypeCustom=Installazione personalizzata
en.CompVst3=VST3 plugin
en.CompClap=CLAP plugin
en.CompApp=Standalone application
fr.CompVst3=Plugin VST3
fr.CompClap=Plugin CLAP
fr.CompApp=Application autonome
de.CompVst3=VST3-Plugin
de.CompClap=CLAP-Plugin
de.CompApp=Eigenständige Anwendung
es.CompVst3=Plugin VST3
es.CompClap=Plugin CLAP
es.CompApp=Aplicación independiente
pt.CompVst3=Plugin VST3
pt.CompClap=Plugin CLAP
pt.CompApp=Aplicativo independente
it.CompVst3=Plugin VST3
it.CompClap=Plugin CLAP
it.CompApp=Applicazione standalone

[Types]
Name: "full"; Description: "{cm:TypeFull}"
Name: "custom"; Description: "{cm:TypeCustom}"; Flags: iscustom

[Components]
Name: "vst3"; Description: "{cm:CompVst3}"; Types: full custom
Name: "clap"; Description: "{cm:CompClap}"; Types: full custom
Name: "app"; Description: "{cm:CompApp}"; Types: full custom

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Components: app; Flags: unchecked

[InstallDelete]
; A previous version of the bundle is replaced as a whole, so that none of its files stays; so are the languages.
Type: files; Name: "{commonappdata}\STYM2149\Languages\*.po"
Type: filesandordirs; Name: "{commoncf64}\VST3\STYM2149.vst3"; Components: vst3
; Copies left by the per-user installer of D-181 are removed, so that hosts do not list STYM2149 twice.
Type: filesandordirs; Name: "{localappdata}\Programs\Common\VST3\STYM2149.vst3"; Components: vst3
Type: files; Name: "{localappdata}\Programs\Common\CLAP\STYM2149.clap"; Components: clap
; The files that the former versions put in C:\Program Files\Indus (D-199), then the folder if nothing else is left in
; it (D-269); unless the application is installed there again.
Type: files; Name: "{commonpf64}\Indus\STYM2149.exe"; Check: FormerDirLeft
Type: files; Name: "{commonpf64}\Indus\LICENSE.txt"; Check: FormerDirLeft
Type: files; Name: "{commonpf64}\Indus\README.md"; Check: FormerDirLeft
Type: files; Name: "{commonpf64}\Indus\README.fr.md"; Check: FormerDirLeft
Type: files; Name: "{commonpf64}\Indus\THIRD_PARTY.md"; Check: FormerDirLeft
Type: files; Name: "{commonpf64}\Indus\unins000.*"; Check: FormerDirLeft
Type: filesandordirs; Name: "{commonpf64}\Indus\docs"; Check: FormerDirLeft
Type: filesandordirs; Name: "{commonpf64}\Indus\licenses"; Check: FormerDirLeft
Type: dirifempty; Name: "{commonpf64}\Indus"; Check: FormerDirLeft

[Files]
Source: "{#Artefacts}\VST3\STYM2149.vst3\*"; DestDir: "{commoncf64}\VST3\STYM2149.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Artefacts}\CLAP\STYM2149.clap"; DestDir: "{commoncf64}\CLAP"; Components: clap; Flags: ignoreversion
Source: "{#Artefacts}\Standalone\STYM2149.exe"; DestDir: "{app}"; Components: app; Flags: ignoreversion
Source: "{#Repo}\languages\*.po"; DestDir: "{commonappdata}\STYM2149\Languages"; Flags: ignoreversion
Source: "{#Repo}\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion
Source: "{#Repo}\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Repo}\README.fr.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Repo}\docs\MANUAL.md"; DestDir: "{app}\docs"; Flags: ignoreversion
Source: "{#Repo}\docs\MANUAL.fr.md"; DestDir: "{app}\docs"; Flags: ignoreversion
Source: "{#Repo}\THIRD_PARTY.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Repo}\third_party\licenses\*"; DestDir: "{app}\licenses"; Flags: ignoreversion

[Icons]
Name: "{group}\STYM2149"; Filename: "{app}\STYM2149.exe"; Components: app
Name: "{group}\{cm:UninstallProgram,STYM2149}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\STYM2149"; Filename: "{app}\STYM2149.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\STYM2149.exe"; Description: "{cm:LaunchProgram,STYM2149}"; Components: app; Flags: nowait postinstall skipifsilent unchecked

[Code]
// The per-user installation of D-181 has the same AppId but its own uninstall entry (HKCU), its application in
// %LOCALAPPDATA%\Programs\STYM2149 and its Start menu group and desktop icon for the user: left in place, its
// shortcuts still started the old application. Its uninstaller runs before the files are installed; what it leaves
// (no uninstaller, or a file in use) is removed after it (D-210).
const
  PerUserKey = 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{50AB0BA1-66D4-4A91-AB65-1FBDB8849998}_is1';

procedure RemovePerUserInstall();
var
  Uninstaller: String;
  ResultCode: Integer;
begin
  if not RegQueryStringValue(HKCU, PerUserKey, 'UninstallString', Uninstaller) then
    Exit;
  Uninstaller := RemoveQuotes(Uninstaller);
  if FileExists(Uninstaller) then
    Exec(Uninstaller, '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  if CompareText(RemoveBackslash(ExpandConstant('{app}')), ExpandConstant('{userpf}\STYM2149')) <> 0 then
    DelTree(ExpandConstant('{userpf}\STYM2149'), True, True, True);
  DelTree(ExpandConstant('{userprograms}\STYM2149'), True, True, True);
  DeleteFile(ExpandConstant('{userdesktop}\STYM2149.lnk'));
  RegDeleteKeyIncludingSubkeys(HKCU, PerUserKey);
end;

// Former application folder (D-199), left for C:\Program Files\PRISM\STYM2149 (D-269): true unless the user
// installs into it again.
function FormerDirLeft(): Boolean;
begin
  Result := CompareText(RemoveBackslash(ExpandConstant('{app}')), ExpandConstant('{commonpf64}\Indus')) <> 0;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssInstall then
    RemovePerUserInstall();
end;
