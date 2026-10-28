; Inno Setup 6.3+; payload is the complete windeployqt portable stage.
; ISCC /DStageDir=C:\absolute\stage /DOutputDir=C:\absolute\dist /DTargetArch=x64 packaging\windows\kadrolov.iss
#ifndef StageDir
  #define StageDir "..\..\stage"
#endif
#ifndef OutputDir
  #define OutputDir "..\..\dist"
#endif
#ifndef TargetArch
  #define TargetArch "x64"
#endif
#ifndef AppVersion
  #define AppVersion "0.1.2"
#endif
#if TargetArch != "x64" && TargetArch != "x86" && TargetArch != "arm64"
  #error Unsupported TargetArch: use x64, x86 or arm64
#endif
#if !FileExists(StageDir + "\kadrolov.exe")
  #error StageDir must contain the complete deployed application
#endif

[Setup]
AppId={{341BFC35-553C-47D3-9CB1-59F93110D434}
AppName=Кадролов
AppVersion={#AppVersion}
AppPublisher=Kadrolov contributors
AppPublisherURL=https://github.com/DenKedr/kadrolov
AppSupportURL=https://github.com/DenKedr/kadrolov/issues
AppUpdatesURL=https://github.com/DenKedr/kadrolov/releases
DefaultDirName={localappdata}\Programs\Kadrolov
DefaultGroupName=Кадролов
PrivilegesRequired=lowest
DisableProgramGroupPage=yes
OutputDir={#OutputDir}
OutputBaseFilename=kadrolov-windows-{#TargetArch}-setup
SetupIconFile=..\..\assets\icons\kadrolov.ico
UninstallDisplayIcon={app}\kadrolov.exe
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no
SetupMutex=KadrolovSetup-341BFC35-553C-47D3-9CB1-59F93110D434
#if TargetArch == "arm64"
ArchitecturesAllowed=arm64
ArchitecturesInstallIn64BitMode=arm64
MinVersion=10.0.22000
#elif TargetArch == "x64"
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
#else
ArchitecturesAllowed=x86compatible
MinVersion=10.0.17763
#endif

[Languages]
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"

[Tasks]
Name: "desktopicon"; Description: "Создать значок на рабочем столе"; GroupDescription: "Значки:"; Flags: unchecked

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\Кадролов"; Filename: "{app}\kadrolov.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\Кадролов"; Filename: "{app}\kadrolov.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\kadrolov.exe"; Description: "Запустить Кадролов"; Flags: nowait postinstall skipifsilent

[Code]
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  RunValue: String;
begin
  if CurUninstallStep = usUninstall then
  begin
    { Remove only this install's optional startup command. Do not touch settings,
      history, screenshots, recordings or a different portable copy's command. }
    if RegQueryStringValue(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Run', 'Kadrolov', RunValue) then
      if CompareText(RunValue, '"' + ExpandConstant('{app}\kadrolov.exe') + '" --background') = 0 then
        RegDeleteValue(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Run', 'Kadrolov');
  end;
end;
