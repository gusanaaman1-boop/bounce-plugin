; BOUNCE - Windows installer, for Inno Setup 6. Run by CI (.github/workflows/windows.yml)
; after the build and the test suite pass; it only packs what the build produced.

#ifndef AppVersion
  #define AppVersion "1.2.0"
#endif
#ifndef SrcRoot
  #define SrcRoot "..\build\Bounce_artefacts\Release"
#endif

#define AppName      "BOUNCE"
#define AppPublisher "Naaman"
#define AppAuthor    "Gussa Naaman"

#if VER < EncodeVer(6,0,0)
  #error BOUNCE's installer needs Inno Setup 6.
#endif

; Checked when the installer is MADE, not on the user's machine: an empty or half-built
; bundle is a compile error here.
#if !FileExists(SrcRoot + "\VST3\BOUNCE.vst3\Contents\x86_64-win\BOUNCE.vst3")
  #error The VST3 bundle is missing or empty - the build did not finish.
#endif
#if !FileExists(SrcRoot + "\Standalone\BOUNCE.exe")
  #error The standalone was not built.
#endif

[Setup]
; AppId is frozen: it is how Windows recognises an upgrade of the same product.
AppId={{E36EFB69-3B7A-41E0-9A24-D6F0D137227D}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppCopyright={#AppAuthor}
VersionInfoVersion={#AppVersion}
VersionInfoCompany={#AppPublisher}
VersionInfoDescription=BOUNCE - one hit becomes a sequence of hits
DefaultDirName={autopf}\{#AppPublisher}\{#AppName}
DefaultGroupName={#AppPublisher}
DisableProgramGroupPage=yes
OutputDir=..\dist
OutputBaseFilename=BOUNCE-{#AppVersion}-windows
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
InfoBeforeFile=INFO-BEFORE.txt
#ifdef BnIcon
SetupIconFile={#BnIcon}
#endif
UninstallDisplayName={#AppName} {#AppVersion}
UninstallDisplayIcon={app}\BOUNCE.exe
PrivilegesRequired=admin
CloseApplications=yes
RestartApplications=no

[Types]
Name: "full";   Description: "Everything"
Name: "custom"; Description: "Choose what to install"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plug-in (Cubase, Live, Reaper, Bitwig)"; Types: full custom; Flags: checkablealone
Name: "app";  Description: "Standalone application";                      Types: full custom

[Files]
; The VST3 is a folder (bundle): copy it whole. An older install is replaced in place.
Source: "{#SrcRoot}\VST3\BOUNCE.vst3\*"; DestDir: "{commoncf64}\VST3\BOUNCE.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SrcRoot}\Standalone\BOUNCE.exe"; DestDir: "{app}"; Components: app; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; DestName: "BOUNCE-MANUAL.md"; Flags: ignoreversion

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\BOUNCE.exe"; Components: app

[UninstallDelete]
Type: dirifempty; Name: "{commoncf64}\VST3\BOUNCE.vst3"

[Code]
procedure CurStepChanged(CurStep: TSetupStep);
var
  Target: String;
begin
  if (CurStep = ssPostInstall) and WizardIsComponentSelected('vst3') then
  begin
    Target := ExpandConstant('{commoncf64}\VST3\BOUNCE.vst3\Contents\x86_64-win\BOUNCE.vst3');
    if not FileExists(Target) then
      MsgBox('INSTALL FAILED: the plug-in is not at' + #13#10 + Target + #13#10#13#10 +
             'Close your DAW and run this installer again as administrator.', mbCriticalError, MB_OK);
  end;
end;
