from pathlib import Path
import shutil,subprocess,json,hashlib
base=Path('E:/SteelTallyToolchain/static-6.8.3-20261110-101215')
stage=Path('C:/SteelTallySchannelDiagnostic');stage.mkdir(exist_ok=True)
source=Path('E:/SteelTallyToolchain/official-20261110-094827/qtbase-everywhere-src-6.8.3/extracted/qtbase-everywhere-src-6.8.3/src/plugins/tls/schannel')
p=stage/'qtls_schannel.cpp'
# Rebuild from the shipped modified source, with the original headers alongside it.
for header in source.glob('*.h'):
    shutil.copy2(header, stage/header.name)
shutil.copy2(Path(__file__).with_name('qtls_schannel.cpp'), p)
flags=(base/'base-build/src/plugins/tls/schannel/CMakeFiles/QSchannelBackendPlugin.dir/flags.make').read_text();defs=next(x for x in flags.splitlines() if x.startswith('CXX_DEFINES =')).split('=',1)[1].split();opts=next(x for x in flags.splitlines() if x.startswith('CXX_FLAGS =')).split('=',1)[1].split()
rsp=base/'base-build/src/plugins/tls/schannel/CMakeFiles/QSchannelBackendPlugin.dir/includes_CXX.rsp'
subprocess.run(['C:/Qt/Tools/mingw1310_64/bin/g++.exe',*defs,'@'+str(rsp),*opts,'-c',str(p),'-o',str(stage/'qtls_schannel.cpp.obj')],check=True)
archive=stage/'libqschannelbackend.a';shutil.copy2(base/'install/plugins/tls/libqschannelbackend.a',archive)
subprocess.run(['C:/Qt/Tools/mingw1310_64/bin/ar.exe','r',str(archive),str(stage/'qtls_schannel.cpp.obj')],check=True)
shutil.copytree(base/'install',stage/'install',dirs_exist_ok=True)
shutil.copy2(archive,stage/'install/plugins/tls/libqschannelbackend.a')
(stage/'manifest.json').write_text(json.dumps({'original_prefix':str(base/'install'),'diagnostic_only':True,'source':str(source),'archive_sha256':hashlib.sha256(archive.read_bytes()).hexdigest()},indent=2))
