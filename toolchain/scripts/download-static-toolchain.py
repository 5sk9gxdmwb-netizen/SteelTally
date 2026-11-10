from pathlib import Path
import subprocess, hashlib, json, datetime
root=Path('E:/SteelTallyToolchain')/('official-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
root.mkdir(parents=True)
urls=[
'https://download.qt.io/archive/qt/6.8/6.8.3/submodules/qtbase-everywhere-src-6.8.3.zip',
'https://download.qt.io/archive/qt/6.8/6.8.3/submodules/qtsvg-everywhere-src-6.8.3.zip',
'https://github.com/Kitware/CMake/releases/download/v3.31.8/cmake-3.31.8-windows-x86_64.zip',
'https://github.com/ninja-build/ninja/releases/download/v1.12.1/ninja-win.zip']
rows=[]
for url in urls:
    name=url.rsplit('/',1)[1]
    folder=root/name.removesuffix('.zip'); folder.mkdir()
    archive=folder/name
    print('Downloading '+url,flush=True)
    result=subprocess.run(['curl','--fail','--location','--connect-timeout','30','--max-time','1800','--retry','2','--output',str(archive),url])
    row={'url':url,'path':str(archive),'curl_exit':result.returncode}
    if result.returncode==0:
        row.update(bytes=archive.stat().st_size,sha256=hashlib.sha256(archive.read_bytes()).hexdigest())
    rows.append(row)
    (root/'manifest.json').write_text(json.dumps(rows,indent=2),encoding='utf-8')
print('MANIFEST '+str(root/'manifest.json'),flush=True)
if any(row['curl_exit'] for row in rows):raise SystemExit(1)
