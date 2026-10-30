# Contributor: GPT-6 Astra (AI-assisted), 2026-10-30. GPL-3.0.
from pathlib import Path
import json, shutil, subprocess, zipfile, hashlib, argparse, tempfile

parser = argparse.ArgumentParser()
parser.add_argument("--only", choices=["moonlight", "sunshine"])
options = parser.parse_args()

root = Path(__file__).resolve().parent.parent
config = json.loads((root/'build/environment.json').read_text(encoding='utf-8-sig'))
def configured(name):
    return (root/'build'/config[name]).resolve()
out = root/'dist'
out.mkdir(exist_ok=True)
work = root/'.local/packaging'
work.mkdir(parents=True, exist_ok=True)
# Use a short, private staging path for Qt's deeply nested QML resources.
staging = tempfile.TemporaryDirectory(prefix='sunshine-moonlight-')
payloads = Path(staging.name).resolve()
if not payloads.is_relative_to(Path(tempfile.gettempdir()).resolve()):
    raise RuntimeError('Unexpected staging directory')
moon = payloads/'Moonlight-Focus'
sun = payloads/'Sunshine-Focus-Sync'
if options.only != 'sunshine':
    moon.mkdir(exist_ok=True)
    for p in configured('moonlightRuntime').iterdir():
        if p.name in {'Moonlight.exe','Uninstall.exe','Moonlight Game Streaming Project','portable.dat'} or p.suffix.lower() in {'.log','.pdb'}: continue
        if p.is_dir(): shutil.copytree(p, moon/p.name, dirs_exist_ok=True)
        else: shutil.copy2(p, moon/p.name)
    shutil.copy2(configured('moonlightBuild')/'app/release/Moonlight.exe', moon/'Moonlight.exe')
    (moon/'portable.dat').touch()
    profile = moon/'Moonlight Game Streaming Project'
    profile.mkdir(exist_ok=True)
    (profile/'Moonlight.ini').write_text('[General]\nfocusSwitchEnabled=true\nfocusSwitchKey=81\nfocusSwitchModifiers=2\nsyncKeyboardLocks=true\nsyncImeFromHost=true\nsyncClipboard=true\nmouseacceleration=true\nwindowmode=1\ncapturesyskeys=2\n', encoding='utf-8')
    (moon/'Start Moonlight.cmd').write_text('@echo off\ncd /d "%~dp0"\nstart "" "%~dp0Moonlight.exe"\n', encoding='ascii')
runtime = configured('msysRoot')/'ucrt64/bin'
if options.only != 'moonlight':
    sun.mkdir(exist_ok=True)
    for p in configured('sunshineBase').iterdir():
        if p.name in {'config','logs','credentials','sunshine.conf','sunshine_state.json','sunshine.exe','Uninstall.exe'} or p.suffix.lower() in {'.log','.pdb'}: continue
        if p.is_dir(): shutil.copytree(p, sun/p.name, dirs_exist_ok=True)
        else: shutil.copy2(p, sun/p.name)
    shutil.copy2(configured('sunshineBuild')/'sunshine.exe', sun/'sunshine.exe')
    shutil.copy2(configured('guiBuild')/'release/sunshine-gui.exe', sun/'assets/gui/sunshine-gui.exe')
    shutil.copytree(configured('sunshineSource')/'build/assets/web', sun/'assets/web', dirs_exist_ok=True)

    # Include the complete transitive non-system DLL closure of the new host build.
    objdump = configured('msysRoot')/'ucrt64/bin/objdump.exe'
    queue = [sun/'sunshine.exe']
    seen = set()
    while queue:
        binary = queue.pop()
        if binary.name.lower() in seen: continue
        seen.add(binary.name.lower())
        data = subprocess.check_output([str(objdump), '-p', str(binary)], text=True, errors='replace')
        for line in data.splitlines():
            if 'DLL Name:' not in line: continue
            name = line.split('DLL Name:',1)[1].strip()
            if (runtime/name).is_file():
                shutil.copy2(runtime/name, sun/name)
                queue.append(sun/name)

for name, path, source in [('Moonlight-Focus',moon,configured('moonlightSource')), ('Sunshine-Focus-Sync',sun,configured('sunshineSource'))]:
    if options.only and not name.lower().startswith(options.only): continue
    shutil.copy2(source/'LICENSE',path/'LICENSE.txt')
    shutil.copy2(root/'docs/usage-zh-CN.txt',path/'使用说明.txt')
    shutil.copy2(root/'CONTRIBUTORS.md',path/'CONTRIBUTORS.md')
    shutil.copy2(root/'docs/LICENSES.md',path/'LICENSES.md')
    shutil.copy2(root/'docs/ime-sync-zh-CN.txt',path/'中英文同步说明.txt')
    if name.startswith('Sunshine'):
        shutil.copy2(configured('controlPanelSource')/'LICENSE',path/'assets/gui/LICENSE-MIT.txt')
        shutil.copy2(root/'docs/admin-autostart-zh-CN.txt',path/'高权限开机自启说明.txt')
    (path/'BUILD-INFO.txt').write_text(f'{name} custom focus-sync build 2026-10-30\nSource: {source.name}\nNot an official upstream release.\n',encoding='utf-8')
    with (path/'BUILD-INFO.txt').open('a', encoding='utf-8') as info:
        info.write('Host-authoritative IME synchronization: ImeSync1\n')
    if name.startswith('Sunshine'):
        with (path/'BUILD-INFO.txt').open('a', encoding='utf-8') as info:
            info.write('Keyboard lock sync revision: LockFix2 (input desktop access)\n')
    with zipfile.ZipFile(out/f'{name}-x64.zip','w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
        for p in path.rglob('*'):
            if p.is_file(): z.write(p,Path(name)/p.relative_to(path))

    files = [p.relative_to(path) for p in path.rglob('*') if p.is_file()]
    dirs = sorted([p.relative_to(path) for p in path.rglob('*') if p.is_dir()],key=lambda p:len(p.parts),reverse=True)
    uninst = '\n'.join('  Delete "$INSTDIR\\'+str(p)+'"' for p in files if p.suffix != '.ini')
    uninst += '\n'+'\n'.join('  RMDir "$INSTDIR\\'+str(p)+'"' for p in dirs)
    executable = 'Moonlight.exe' if name.startswith('Moonlight') else 'sunshine.exe'
    install_dir = '$LOCALAPPDATA\\Programs\\'+name
    file_directive = f'  File /r "{path}\\*"'
    if name.startswith('Moonlight'):
        file_directive = f'''  File /r /x "Moonlight.ini" "{path}\\*"
  IfFileExists "$INSTDIR\\Moonlight Game Streaming Project\\Moonlight.ini" profile_done 0
  SetOutPath "$INSTDIR\\Moonlight Game Streaming Project"
  File "{profile/'Moonlight.ini'}"
profile_done:
  SetOutPath "$INSTDIR"'''
    nsi = f'''Unicode true
!include "MUI2.nsh"
Name "{name} (custom build)"
OutFile "{out / (name+'-Setup-x64.exe')}"
InstallDir "{install_dir}"
RequestExecutionLevel user
SetCompressor /SOLID lzma
!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_SHOWREADME "$INSTDIR\\使用说明.txt"
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "{path/'LICENSE.txt'}"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "SimpChinese"
Section "Install"
  SetOutPath "$INSTDIR"
{file_directive}
  WriteUninstaller "$INSTDIR\\Uninstall.exe"
  CreateDirectory "$SMPROGRAMS\\{name}"
  CreateShortcut "$SMPROGRAMS\\{name}\\{name}.lnk" "$INSTDIR\\{executable}"
  CreateShortcut "$SMPROGRAMS\\{name}\\使用说明.lnk" "$INSTDIR\\使用说明.txt"
  WriteRegStr HKCU "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\{name}" "DisplayName" "{name} (custom build)"
  WriteRegStr HKCU "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\{name}" "UninstallString" '"$INSTDIR\\Uninstall.exe"'
SectionEnd
Section "Uninstall"
{uninst}
  Delete "$INSTDIR\\Uninstall.exe"
  RMDir "$INSTDIR"
  Delete "$SMPROGRAMS\\{name}\\{name}.lnk"
  Delete "$SMPROGRAMS\\{name}\\使用说明.lnk"
  RMDir "$SMPROGRAMS\\{name}"
  DeleteRegKey HKCU "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\{name}"
SectionEnd
'''
    nsi_path = work/f'{name}.nsi'
    nsi_path.write_text(nsi,encoding='utf-8-sig')
    subprocess.run([str(runtime/'makensis.exe'),'/V2',str(nsi_path)],check=True)

checksums = []
for p in sorted(out.glob('*-x64.*')):
    checksums.append(hashlib.file_digest(p.open('rb'),'sha256').hexdigest()+'  '+p.name)
(out/'SHA256SUMS.txt').write_text('\n'.join(checksums)+'\n',encoding='utf-8')
staging.cleanup()
print('Packaged installers, portable ZIPs and SHA256SUMS.txt')
