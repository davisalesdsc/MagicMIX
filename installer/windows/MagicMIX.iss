; Instalador do Windows (Inno Setup 6). Compilado pelo CI:
;   ISCC /DAppVersion=0.1 /DSrcDir=<pasta com .vst3 e .exe> /DOutDir=<pasta de saída> MagicMIX.iss

#ifndef AppVersion
  #define AppVersion "0.0"
#endif
#ifndef SrcDir
  #define SrcDir "..\..\..\out"
#endif
#ifndef OutDir
  #define OutDir "."
#endif

#define Plugin "MagicMIX"

[Setup]
AppId={{999175BD-C4BB-4CE6-B117-CAC34DF74F41}
AppName=MagicMIX
AppVersion={#AppVersion}
AppVerName=MagicMIX {#AppVersion}
AppPublisher=Davi Sales
DefaultDirName={autopf}\{#Plugin}
DefaultGroupName=MagicMIX
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
MinVersion=10.0
OutputDir={#OutDir}
OutputBaseFilename=MagicMIX-{#AppVersion}-Windows-Setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=MagicMIX
UninstallDisplayIcon={app}\{#Plugin}.exe
CloseApplications=yes

[Languages]
Name: "pt"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[Types]
Name: "completa"; Description: "Completa (recomendado)"
Name: "personalizada"; Description: "Personalizada"; Flags: iscustom

[Components]
Name: "vst3"; Description: "Plugin VST3 (REAPER, Ableton, Cubase, Studio One...)"; Types: completa personalizada
Name: "app"; Description: "App MagicMIX (usa a entrada da placa de som direto, sem DAW)"; Types: completa personalizada

[Tasks]
Name: "atalho"; Description: "Criar atalho do app na Área de Trabalho"; Components: app; Flags: unchecked

[InstallDelete]
; apaga a versão anterior do plugin antes de copiar (arquivo que sumiu numa versão nova não fica)
Type: filesandordirs; Name: "{commoncf64}\VST3\{#Plugin}.vst3"; Components: vst3

[Files]
Source: "{#SrcDir}\{#Plugin}.vst3\*"; DestDir: "{commoncf64}\VST3\{#Plugin}.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SrcDir}\{#Plugin}.exe"; DestDir: "{app}"; Components: app; Flags: ignoreversion
Source: "{#SrcDir}\LEIA-ME.md"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist

[Icons]
Name: "{autoprograms}\MagicMIX"; Filename: "{app}\{#Plugin}.exe"; Components: app
Name: "{autodesktop}\MagicMIX"; Filename: "{app}\{#Plugin}.exe"; Components: app; Tasks: atalho

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\{#Plugin}.vst3"

[Messages]
pt.FinishedLabel=Instalado! Abra o seu programa (REAPER, Ableton, Cubase...) e peça para procurar plugins de novo. Depois coloque "MagicMIX" na pista, dê play e escolha um tratamento na Biblioteca.%n%nNão precisa de conta nem de chave: tudo roda no seu computador.
