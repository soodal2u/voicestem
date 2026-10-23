; VoiceStem 설치 프로그램 (Inno Setup 6)
; 빌드: installer\build-installer.ps1

#define AppName "VoiceStem"
#ifndef AppVersion
  #define AppVersion "0.2.0"
#endif
#ifndef BuildDir
  #define BuildDir "C:\dev\voicestem-build\VoiceStem_artefacts\Release"
#endif

[Setup]
AppId={{8E3B6F2A-4C1D-4E7B-9A55-2F6C0D3B7E91}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=soodal
; 모든 호스트가 찾는 표준 VST3 폴더(C:\Program Files\Common Files\VST3)에 넣으므로 관리자 권한 필요
PrivilegesRequired=admin
UsedUserAreasWarning=no
; 제거 프로그램·단독 실행 앱은 여기, 플러그인 번들은 [Files]에서 VST3 폴더로
DefaultDirName={autopf}\{#AppName}
DisableDirPage=yes
DisableProgramGroupPage=yes
OutputDir=..\dist
OutputBaseFilename=VoiceStem-Setup-{#AppVersion}
UninstallDisplayName={#AppName} (VST3)
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
CloseApplications=no

[Languages]
Name: "korean"; MessagesFile: "compiler:Languages\Korean.isl"

[Tasks]
Name: "standalone"; Description: "단독 실행 앱도 설치 (DAW 없이 마이크로 바로 테스트)"; Flags: unchecked

[Files]
Source: "{#BuildDir}\VST3\VoiceStem.vst3\*"; DestDir: "{commoncf64}\VST3\VoiceStem.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#BuildDir}\Standalone\VoiceStem.exe"; DestDir: "{app}"; Flags: ignoreversion; Tasks: standalone

[Icons]
Name: "{autoprograms}\VoiceStem"; Filename: "{app}\VoiceStem.exe"; Tasks: standalone

[UninstallDelete]
Type: files; Name: "{commoncf64}\VST3\VoiceStem.vst3\Contents\x86_64-win\VoiceStem.vst3.old"
Type: dirifempty; Name: "{commoncf64}\VST3\VoiceStem.vst3\Contents\x86_64-win"
Type: dirifempty; Name: "{commoncf64}\VST3\VoiceStem.vst3\Contents\Resources"
Type: dirifempty; Name: "{commoncf64}\VST3\VoiceStem.vst3\Contents"
Type: dirifempty; Name: "{commoncf64}\VST3\VoiceStem.vst3"

[Code]
// DAW·호스트가 플러그인을 쓰고 있으면 DLL을 덮어쓸 수 없다. 사용 중인 DLL도 이름은 바꿀 수 있으므로
// .old로 비켜 두고 새 파일을 넣는다. 호스트는 다시 불러올 때 새 버전을 쓴다.
procedure MoveAsideInUse(const Binary: String);
var
  Old: String;
begin
  Old := Binary + '.old';
  DeleteFile(Old);   // 지난번에 비켜 둔 것 (이젠 안 쓰이면 지워진다)
  if FileExists(Binary) and not DeleteFile(Binary) then
    RenameFile(Binary, Old);
end;

// 예전에 손으로 복사해 둔 사용자 폴더 사본(%LOCALAPPDATA%\Programs\Common\VST3)은 중복 표시되므로 치운다
procedure RemoveUserCopy();
var
  Bundle: String;
begin
  Bundle := ExpandConstant('{localappdata}\Programs\Common\VST3\VoiceStem.vst3');
  if not DirExists(Bundle) then
    Exit;
  MoveAsideInUse(Bundle + '\Contents\x86_64-win\VoiceStem.vst3');
  DeleteFile(Bundle + '\Contents\Resources\moduleinfo.json');
  DelTree(Bundle, True, True, True);   // 사용 중인 .old가 남아 있으면 그 파일과 폴더만 남는다
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssInstall then
    MoveAsideInUse(ExpandConstant('{commoncf64}\VST3\VoiceStem.vst3\Contents\x86_64-win\VoiceStem.vst3'));
  if CurStep = ssPostInstall then
    RemoveUserCopy();
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
    MoveAsideInUse(ExpandConstant('{commoncf64}\VST3\VoiceStem.vst3\Contents\x86_64-win\VoiceStem.vst3'));
end;
