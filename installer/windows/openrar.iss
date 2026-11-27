; Inno Setup 6 installer script for OpenRAR (Windows x64)
; Build with: iscc /DAppVersion=1.0.0 installer\windows\openrar.iss

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif

[Setup]
AppId={{8D7A4D52-3E49-4C81-9E93-55D9C419E100}
AppName=OpenRAR
AppVersion={#AppVersion}
AppPublisher=Stefan Miladinovic (OpenRAR)
AppPublisherURL=https://github.com/miladinovst/OpenRAR
AppSupportURL=https://github.com/miladinovst/OpenRAR/issues
DefaultDirName={autopf}\OpenRAR
DefaultGroupName=OpenRAR
AllowNoIcons=yes
LicenseFile=..\..\LICENSE
OutputDir=..\..\dist
OutputBaseFilename=OpenRAR-Setup-{#AppVersion}-win64
SetupIconFile=..\..\assets\openrar.ico
UninstallDisplayIcon={app}\OpenRAR-GUI.exe
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
ChangesEnvironment=yes

[Languages]
; Official Inno Setup 6 built-in languages (OpenRAR itself supports all 13 UI languages at runtime)
Name: "english";    MessagesFile: "compiler:Default.isl"
Name: "spanish";    MessagesFile: "compiler:Languages\Spanish.isl"
Name: "portuguese"; MessagesFile: "compiler:Languages\Portuguese.isl"
Name: "french";     MessagesFile: "compiler:Languages\French.isl"
Name: "japanese";   MessagesFile: "compiler:Languages\Japanese.isl"
Name: "russian";    MessagesFile: "compiler:Languages\Russian.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"
Name: "contextmenu"; Description: "Integrate into Windows Explorer right-click menu"; GroupDescription: "System Integration:"
Name: "addpath";     Description: "Add OpenRAR CLI to PATH"; GroupDescription: "System Integration:"

[Files]
Source: "..\..\build\OpenRAR-GUI.exe";      DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\build\openrar.exe";          DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\build\openrar_sfx_stub.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\assets\openrar.ico";         DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\README.md";                  DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\LICENSE";                    DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\OpenRAR"; Filename: "{app}\OpenRAR-GUI.exe"; IconFilename: "{app}\openrar.ico"
Name: "{group}\{cm:UninstallProgram,OpenRAR}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\OpenRAR"; Filename: "{app}\OpenRAR-GUI.exe"; IconFilename: "{app}\openrar.ico"; Tasks: desktopicon

[Run]
Filename: "{app}\openrar.exe"; Parameters: "install-menu"; Flags: runhidden; Tasks: contextmenu
Filename: "{app}\OpenRAR-GUI.exe"; Description: "{cm:LaunchProgram,OpenRAR}"; Flags: nowait postinstall skipifsilent

[UninstallRun]
Filename: "{app}\openrar.exe"; Parameters: "uninstall-menu"; Flags: runhidden; RunOnceId: "RemoveShellMenu"

[Code]
procedure AddToUserPath(Dir: string);
var
  CurPath: string;
begin
  if not RegQueryStringValue(HKEY_CURRENT_USER, 'Environment', 'Path', CurPath) then
    CurPath := '';
  if Pos(';' + Lowercase(Dir) + ';', ';' + Lowercase(CurPath) + ';') = 0 then
  begin
    if (Length(CurPath) > 0) and (CurPath[Length(CurPath)] <> ';') then
      CurPath := CurPath + ';';
    CurPath := CurPath + Dir;
    RegWriteExpandStringValue(HKEY_CURRENT_USER, 'Environment', 'Path', CurPath);
  end;
end;

procedure RemoveFromUserPath(Dir: string);
var
  CurPath, LowerCur, LowerDir: string;
  P: Integer;
begin
  if not RegQueryStringValue(HKEY_CURRENT_USER, 'Environment', 'Path', CurPath) then Exit;
  LowerCur := ';' + Lowercase(CurPath) + ';';
  LowerDir := ';' + Lowercase(Dir) + ';';
  P := Pos(LowerDir, LowerCur);
  if P > 0 then
  begin
    Delete(CurPath, P, Length(Dir));
    StringChangeEx(CurPath, ';;', ';', True);
    if (Length(CurPath) > 0) and (CurPath[1] = ';') then Delete(CurPath, 1, 1);
    if (Length(CurPath) > 0) and (CurPath[Length(CurPath)] = ';') then Delete(CurPath, Length(CurPath), 1);
    RegWriteExpandStringValue(HKEY_CURRENT_USER, 'Environment', 'Path', CurPath);
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if (CurStep = ssPostInstall) and WizardIsTaskSelected('addpath') then
    AddToUserPath(ExpandConstant('{app}'));
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then
    RemoveFromUserPath(ExpandConstant('{app}'));
end;
