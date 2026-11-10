from pathlib import Path
import os,json,subprocess
root=Path('E:/SteelTallyToolchain')
record=json.loads((root/'static-build-record.json').read_text())
work=Path(record['work']); cmake=record['cmake']; prefix=record['prefix']
compiler='C:/Qt/Tools/mingw1310_64/bin'
env=os.environ.copy();env['PATH']=os.pathsep.join([compiler,str(Path(cmake).parent),'C:/Windows/System32','C:/Windows'])
def run(name,args):
 print(name,flush=True)
 with (work/(name+'.log')).open('w',encoding='utf-8') as log:
  p=subprocess.run([cmake,*args],cwd=work,env=env,stdout=log,stderr=subprocess.STDOUT)
 record['steps'].append({'name':name,'args':args,'exit_code':p.returncode})
 (root/'static-build-record.json').write_text(json.dumps(record,indent=2),encoding='utf-8')
 if p.returncode:raise SystemExit(name+' failed')
run('rebuild-base-serial',['--build',str(work/'base-build'),'--parallel','1'])
run('install-base',['--install',str(work/'base-build')])
svg=root/'official-20261110-094827/qtsvg-everywhere-src-6.8.3/extracted/qtsvg-everywhere-src-6.8.3'
run('configure-svg',['-S',str(svg),'-B',str(work/'svg-build'),'-G','MinGW Makefiles','-DCMAKE_MAKE_PROGRAM='+compiler+'/mingw32-make.exe','-DCMAKE_PREFIX_PATH='+prefix,'-DCMAKE_INSTALL_PREFIX='+prefix,'-DCMAKE_BUILD_TYPE=Release','-DBUILD_SHARED_LIBS=OFF','-DQT_BUILD_TESTS=OFF','-DQT_BUILD_EXAMPLES=OFF','-DCMAKE_CXX_COMPILER='+compiler+'/g++.exe','-DCMAKE_C_COMPILER='+compiler+'/gcc.exe'])
run('build-svg',['--build',str(work/'svg-build'),'--parallel','1'])
run('install-svg',['--install',str(work/'svg-build')])
print('STATIC_QT_INSTALLED '+prefix,flush=True)
