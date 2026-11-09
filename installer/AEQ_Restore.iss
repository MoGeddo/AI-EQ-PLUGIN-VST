; AEQ Restore - Windows installer (Inno Setup 6.3+)
;
; Do not compile this by hand: run  installer\build_installer.ps1
; It builds the plugin, stages the artefacts into installer\staging\ and then
; calls ISCC with the version number.  The staging folder is what this script
; reads, so the wizard never depends on a build path that moves around.
;
; Output: dist\AEQ_Restore_Setup_<version>.exe

#define AppName "AEQ Restore"
#ifndef AppVersion
  #define AppVersion "1.2.31"
#endif
#define FileVersion AppVersion + ".0"
#define AppPublisher "AEQ"
#define AppExe "AEQ Restore.exe"
#define RepoURL "https://github.com/MoGeddo/AI-EQ-PLUGIN-VST"

[Setup]
; A stable AppId is what makes an upgrade replace the old install instead of
; stacking a second copy next to it.  Never change this line.
AppId={{B4E1A9C2-6F3D-4A8E-9C1B-2D7E5F0A83C4}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#RepoURL}
AppSupportURL={#RepoURL}/issues
AppUpdatesURL={#RepoURL}/releases
AppContact={#RepoURL}
VersionInfoVersion={#FileVersion}
VersionInfoCompany={#AppPublisher}
VersionInfoDescription={#AppName} installer
VersionInfoProductName={#AppName}
VersionInfoProductVersion={#AppVersion}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableWelcomePage=no
LicenseFile=LICENSE.txt
OutputDir=..\dist
OutputBaseFilename=AEQ_Restore_Setup_{#AppVersion}
SetupIconFile=aeq.ico
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName} {#AppVersion}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
WizardImageFile=wizard.bmp
WizardSmallImageFile=wizard_small.bmp
WizardImageStretch=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
MinVersion=10.0
; A DAW keeps the plugin loaded, and Windows will not let us overwrite a loaded
; DLL.  Inno detects that through these masks and asks the user to close it.
CloseApplications=yes
CloseApplicationsFilter=*.exe,*.dll,*.vst3
RestartApplications=no
AllowNoIcons=yes

[Languages]
; One language keeps the flow to the fewest clicks, and it is what most plugin
; installers ship.  To show a language picker, add e.g.
;   Name: "arabic"; MessagesFile: "compiler:Languages\Arabic.isl"
; and install language files from the Inno Setup installer.
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "vst3"; Description: "VST3 plugin (64-bit) — the plugin itself"; GroupDescription: "What to install:"; Flags: checkedonce
Name: "standalone"; Description: "Standalone application — check the result without opening a DAW"; GroupDescription: "What to install:"; Flags: checkedonce
Name: "reference"; Description: "Reference curve (CSV) + quick start notes"; GroupDescription: "What to install:"; Flags: unchecked

[Files]
; The .vst3 is a *folder* (a bundle), not a file - it must be copied recursively
; or the plugin will never load.
Source: "staging\vst3\{#AppName}.vst3\*"; DestDir: "{commonpf64}\VST3\{#AppName}.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs; Tasks: vst3
Source: "staging\standalone\{#AppExe}"; DestDir: "{app}"; Flags: ignoreversion; Tasks: standalone
Source: "..\presets\target_curve.csv"; DestDir: "{app}\reference"; Flags: ignoreversion; Tasks: reference
Source: "QUICKSTART.txt"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: standalone
Name: "{group}\Quick start"; Filename: "{app}\QUICKSTART.txt"
Name: "{group}\Uninstall {#AppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: standalone

[Run]
Filename: "{app}\{#AppExe}"; Description: "Open {#AppName} now"; Flags: nowait postinstall skipifsilent; Tasks: standalone
Filename: "{app}\QUICKSTART.txt"; Description: "Read the quick start"; Flags: shellexec nowait postinstall skipifsilent unchecked

[UninstallDelete]
; Removing the bundle's empty folders; the files themselves are tracked by Inno.
Type: filesandordirs; Name: "{commonpf64}\VST3\{#AppName}.vst3"
Type: filesandordirs; Name: "{app}\reference"
Type: dirifempty; Name: "{app}"

[Code]
procedure CurPageChanged(CurPageID: Integer);
begin
  if CurPageID = wpFinished then
    WizardForm.FinishedLabel.Caption :=
      'Installation complete.' + #13#10#13#10 +
      'Plugin:  C:\Program Files\Common Files\VST3\{#AppName}.vst3' + #13#10 + #13#10 +
      'Open your DAW and rescan plug-ins (most DAWs: Preferences > Plug-ins > Rescan). ' +
      'If the plugin does not appear, close the DAW completely and open it again.';
end;
