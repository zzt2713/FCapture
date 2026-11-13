; ============================================================================
;  F-Capture 安装包脚本（Inno Setup 6）
;
;  用法：
;    1. 先跑 .\scripts\build.sh 生成 dist\F-Capture\
;    2. 用 Inno Setup 打开本文件 → Build → Compile
;       或命令行： "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer\FCapture.iss
;    3. 产物：dist\installer\F-Capture-<版本>-setup.exe
;
;  注意：
;    - 安装版**不带** portable.txt，因此配置落在 %LOCALAPPDATA%\F-Capture
;    - 卸载默认保留用户配置；如需彻底删除，用 /PURGECONFIG 或手动删目录
; ============================================================================

#define AppName        "F-Capture"
#define AppShortName   "FCapture"
#define AppVersion     "1.6.0"
#define AppPublisher   "Fool"
#define AppCopyright   "Copyright (C) 2026 Fool"
#define AppExeName     "FCapture.exe"
#define AppId          "8E4A2C71-5F3B-4D62-9A18-3C7B6D2E9F04"
#define AppVerName     AppName + " " + AppVersion

; 源码树根目录（本 .iss 在 installer\ 下）
#define ProjectRoot    SourcePath + ".."
#define DistDir        ProjectRoot + "\dist\F-Capture"

[Setup]
AppId={#AppId}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppVerName}
AppPublisher={#AppPublisher}
AppCopyright={#AppCopyright}
VersionInfoVersion={#AppVersion}
VersionInfoCompany={#AppPublisher}
VersionInfoDescription={#AppName}
VersionInfoProductName={#AppName}
VersionInfoProductVersion={#AppVersion}
VersionInfoCopyright={#AppCopyright}

; 默认安装到 Program Files（需要管理员权限）
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
PrivilegesRequired=admin

; 图标：全部使用项目自带 logo
SetupIconFile={#ProjectRoot}\resources\app.ico
UninstallDisplayIcon={app}\{#AppExeName}
UninstallDisplayName={#AppName}

; 输出
OutputDir={#ProjectRoot}\dist\installer
OutputBaseFilename={#AppShortName}-{#AppVersion}-setup
Compression=lzma2/max
SolidCompression=yes
ArchitecturesAllowed=x64compatible
; 若使用 Inno Setup < 6.3，请把上面这行改成：ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64compatible
; 若使用 Inno Setup < 6.3，请把上面这行改成：ArchitecturesInstallIn64BitMode=x64
WizardStyle=modern
ShowLanguageDialog=no

LicenseFile={#ProjectRoot}\LICENSE
; 顶层只装发布时需要的文件，不打包源码

[Languages]
; ShowLanguageDialog=no 时使用第一条。中文语言包随 Inno Setup 安装提供。
Name: "chinesesimplified"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[InstallDelete]
; 升级时清掉旧的 exe，避免被占用导致安装失败后残留半套文件
Type: files; Name: "{app}\{#AppExeName}"

[Files]
; 主程序与依赖（递归带入所有 DLL / 插件目录 / 用户手册）
Source: "{#DistDir}\{#AppExeName}";            DestDir: "{app}"; Flags: ignoreversion
Source: "{#DistDir}\*.dll";                    DestDir: "{app}"; Flags: ignoreversion
Source: "{#DistDir}\*.md";                     DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#DistDir}\platforms\*";              DestDir: "{app}\platforms";              Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#DistDir}\imageformats\*";           DestDir: "{app}\imageformats";           Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#DistDir}\iconengines\*";            DestDir: "{app}\iconengines";            Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#DistDir}\styles\*";                 DestDir: "{app}\styles";                 Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#DistDir}\tls\*";                    DestDir: "{app}\tls";                    Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#DistDir}\networkinformation\*";     DestDir: "{app}\networkinformation";     Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#DistDir}\generic\*";                DestDir: "{app}\generic";                Flags: ignoreversion recursesubdirs createallsubdirs

; 不打包 dist\data\（那是便携版的运行数据，含本机配置）
; 也不打包 portable.txt：安装版必须走 %LOCALAPPDATA% 存配置，
; 否则配置会写进 Program Files（无权限）导致启动即失败。

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExeName}"; IconFilename: "{app}\{#AppExeName}"
Name: "{group}\用户手册"; Filename: "{app}\用户手册.md"; Check: FileExists(ExpandConstant('{app}\用户手册.md'))
Name: "{group}\卸载 {#AppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"; IconFilename: "{app}\{#AppExeName}"; Tasks: desktopicon

[Run]
; 安装完成后可选启动。程序是托盘常驻，用 nowait 避免安装向导阻塞
Filename: "{app}\{#AppExeName}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent

[UninstallRun]
; 卸载前结束进程，否则文件被占用删不掉
Filename: "{sys}\taskkill.exe"; Parameters: "/F /IM {#AppExeName}"; Flags: runhidden skipifdoesntexist; RunOnceId: "KillApp"

[Code]
function InitializeSetup(): Boolean;
var
  ResultCode: Integer;
begin
  // 结束正在运行的实例，否则 exe 被占用会导致写入失败。
  // taskkill 退出码：0 = 已结束，128 = 进程不存在；都不阻塞安装。
  Exec(ExpandConstant('{sys}\taskkill.exe'),
       '/F /IM {#AppExeName}', '', SW_HIDE,
       ewWaitUntilTerminated, ResultCode);

  Result := True;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if (CurUninstallStep = usPostUninstall) and not UninstallSilent then
  begin
    // 安装版把配置存在 %LOCALAPPDATA%，卸载默认不删
    MsgBox('用户配置与日志保留在：' + #13#10 +
           ExpandConstant('{localappdata}') + '\F-Capture' + #13#10#13#10 +
           '如需彻底删除，请手动移除该目录。',
           mbInformation, MB_OK);
  end;
end;
