; Installateur Windows de Camerist (Inno Setup 6).
; Compilé automatiquement par .github/scripts/Package-Windows.ps1, qui passe :
;   /DMyAppVersion=0.1.0  /DSourceDir=...\release\<config>\camerist
;   /DOutputDir=...\release  /DOutputName=camerist-0.1.0-windows-x64-Installer
;
; Le plugin est installé dans le dossier des plugins partagé d'OBS :
;   C:\ProgramData\obs-studio\plugins\camerist\bin\64bit\camerist.dll
;   C:\ProgramData\obs-studio\plugins\camerist\data\...
; OBS (28 et plus) charge automatiquement tout ce qui s'y trouve,
; quel que soit l'endroit où OBS lui-même est installé.

#ifndef MyAppVersion
  #define MyAppVersion "0.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\..\..\release\RelWithDebInfo\camerist"
#endif
#ifndef OutputDir
  #define OutputDir "..\..\..\release"
#endif
#ifndef OutputName
  #define OutputName "camerist-installer"
#endif

[Setup]
AppId={{040231BA-FC26-43A2-8C04-F8B4DC999A0B}
AppName=Camerist
AppVersion={#MyAppVersion}
AppVerName=Camerist {#MyAppVersion}
AppPublisher=tomi_tomeh
AppPublisherURL=https://github.com/OWNER/camerist
AppSupportURL=https://github.com/OWNER/camerist/issues
AppUpdatesURL=https://github.com/OWNER/camerist/releases
DefaultDirName={commonappdata}\obs-studio\plugins\camerist
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableReadyPage=no
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
; OBS doit être fermé pendant l'installation (verrou créé par OBS au lancement)
AppMutex=OBSStudioCore
CloseApplications=no
OutputDir={#OutputDir}
OutputBaseFilename={#OutputName}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=Camerist (OBS plugin)
UninstallFilesDir={app}\uninstall
VersionInfoVersion={#MyAppVersion}
VersionInfoProductName=Camerist
VersionInfoDescription=Camerist plugin for OBS Studio installer

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "fr"; MessagesFile: "compiler:Languages\French.isl"

[CustomMessages]
en.ObsNotFound=OBS Studio does not seem to be installed on this computer.%n%nCamerist is a plugin for OBS Studio 31 or newer. You can install it anyway and it will load the next time OBS starts.%n%nContinue?
fr.ObsNotFound=OBS Studio ne semble pas installé sur cet ordinateur.%n%nCamériste est un plugin pour OBS Studio 31 ou plus récent. Tu peux l'installer quand même, il se chargera au prochain lancement d'OBS.%n%nContinuer ?

[Messages]
en.FinishedLabelNoIcons=Camerist is installed. Start OBS: the Camerist dock appears on its own (otherwise, open the Docks menu and tick Camerist).
fr.FinishedLabelNoIcons=Camériste est installé. Lance OBS : le dock Camériste apparaît tout seul (sinon, ouvre le menu Docks et coche Camériste).

[InstallDelete]
; Retire les fichiers d'une version précédente avant de copier les nouveaux.
; Les réglages et positions (dans %APPDATA%\obs-studio\plugin_config\camerist) ne sont pas touchés.
Type: filesandordirs; Name: "{app}\bin"
Type: filesandordirs; Name: "{app}\data"

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Excludes: "*.pdb"; Flags: ignoreversion recursesubdirs createallsubdirs

[Code]
function ObsInstalled(): Boolean;
var
  Path: String;
begin
  Result :=
    RegQueryStringValue(HKLM64, 'SOFTWARE\OBS Studio', '', Path) or
    RegQueryStringValue(HKLM32, 'SOFTWARE\OBS Studio', '', Path) or
    RegQueryStringValue(HKCU, 'SOFTWARE\OBS Studio', '', Path) or
    FileExists(ExpandConstant('{commonpf64}\obs-studio\bin\64bit\obs64.exe'));
end;

function InitializeSetup(): Boolean;
begin
  Result := True;
  if not ObsInstalled() then
    Result := MsgBox(CustomMessage('ObsNotFound'), mbConfirmation, MB_YESNO) = IDYES;
end;
