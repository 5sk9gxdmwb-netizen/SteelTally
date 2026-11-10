from pathlib import Path
import subprocess, os, json, datetime
root=Path('E:/SteelTallyToolchain')
download=root/'official-20261110-094827'
cmake=download/'cmake-3.31.8-windows-x86_64/extracted/cmake-3.31.8-windows-x86_64/bin/cmake.exe'
ninja=Path('C:/Qt/Tools/mingw1310_64/bin/mingw32-make.exe')
generator='MinGW Makefiles'
base=download/'qtbase-everywhere-src-6.8.3/extracted/qtbase-everywhere-src-6.8.3'
svg=download/'qtsvg-everywhere-src-6.8.3/extracted/qtsvg-everywhere-src-6.8.3'
compiler=Path('C:/Qt/Tools/mingw1310_64/bin')
work=root/('static-6.8.3-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
work.mkdir()
prefix=work/'install'
env=os.environ.copy()
env['PATH']=os.pathsep.join([str(compiler),str(cmake.parent),str(ninja.parent),'C:/Windows/System32','C:/Windows'])
for key in ('QTDIR','QT_PLUGIN_PATH','QT_QPA_PLATFORM_PLUGIN_PATH','CMAKE_PREFIX_PATH'):env.pop(key,None)
record={'work':str(work),'prefix':str(prefix),'cmake':str(cmake),'ninja':str(ninja),'steps':[]}
def run(name,args):
    print(name,flush=True)
    with (work/(name+'.log')).open('w',encoding='utf-8') as log:
        result=subprocess.run([str(cmake),*map(str,args)],cwd=work,env=env,stdout=log,stderr=subprocess.STDOUT)
    record['steps'].append({'name':name,'args':list(map(str,args)),'exit_code':result.returncode})
    (root/'static-build-record.json').write_text(json.dumps(record,indent=2),encoding='utf-8')
    if result.returncode:raise SystemExit(name+' failed; see '+str(work/(name+'.log')))
run('configure-base',['-S',base,'-B',work/'base-build','-G',generator,f'-DCMAKE_MAKE_PROGRAM={ninja}',f'-DCMAKE_C_COMPILER={compiler}/gcc.exe',f'-DCMAKE_CXX_COMPILER={compiler}/g++.exe',f'-DCMAKE_INSTALL_PREFIX={prefix}','-DCMAKE_BUILD_TYPE=Release','-DBUILD_SHARED_LIBS=OFF','-DFEATURE_static_runtime=ON','-DQT_BUILD_TESTS=OFF','-DQT_BUILD_EXAMPLES=OFF','-DBUILD_WITH_PCH=OFF','-DFEATURE_opengl=OFF','-DFEATURE_vulkan=OFF','-DFEATURE_sql=OFF','-DFEATURE_testlib=OFF','-DFEATURE_dbus=OFF','-DFEATURE_icu=OFF','-DFEATURE_openssl=OFF'])
run('build-base',['--build',work/'base-build','--parallel','4'])
run('install-base',['--install',work/'base-build'])
run('configure-svg',['-S',svg,'-B',work/'svg-build','-G',generator,f'-DCMAKE_MAKE_PROGRAM={ninja}',f'-DCMAKE_PREFIX_PATH={prefix}',f'-DCMAKE_INSTALL_PREFIX={prefix}','-DCMAKE_BUILD_TYPE=Release','-DBUILD_SHARED_LIBS=OFF','-DQT_BUILD_TESTS=OFF','-DQT_BUILD_EXAMPLES=OFF',f'-DCMAKE_CXX_COMPILER={compiler}/g++.exe',f'-DCMAKE_C_COMPILER={compiler}/gcc.exe'])
run('build-svg',['--build',work/'svg-build','--parallel','4'])
run('install-svg',['--install',work/'svg-build'])
print('STATIC_QT_INSTALLED '+str(prefix),flush=True)
