"""Exercise real wide add/sub code generation and native results at O0 and O2."""
from pathlib import Path
import argparse,hashlib,json,platform,subprocess,time,traceback

def sha(path):
 with Path(path).open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()

def main():
 parser=argparse.ArgumentParser(description=__doc__)
 parser.add_argument('--repo',type=Path,required=True)
 parser.add_argument('--compiler',type=Path,required=True)
 parser.add_argument('--output',type=Path,required=True)
 args=parser.parse_args();root=args.repo.resolve();out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
 compiler=args.compiler.resolve();source=root/'tests/neverc/standards/test_u128_add_sub.c'
 report={'passed':False,'completed':False,'commands':[],'cases':[],'frontend_timeout_seconds':60,
         'compiler_crash_fixed':False,'full_ci_passed':False,'goal_complete':False}
 def save():(out/'results.json').write_text(json.dumps(report,indent=2)+'\n')
 def run(argv,label):
  argv=list(map(str,argv));log=out/(label+'.log');start=time.time_ns()
  with log.open('w') as stream:
   try:code=subprocess.run(argv,cwd=root,stdout=stream,stderr=subprocess.STDOUT,timeout=60).returncode
   except subprocess.TimeoutExpired:stream.write('\nWIDE INTEGER COMMAND TIMED OUT\n');code=-1000
  report['commands'].append({'command':argv,'returncode':code,'timeout_seconds':60,'elapsed_ns':time.time_ns()-start,'log':log.name,'log_sha256':sha(log)});save();print(label,code,flush=True);return code
 try:
  assert platform.system()=='Windows' and platform.machine().lower() in ['arm64','aarch64']
  report.update(revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),compiler_sha256=sha(compiler),source_sha256=sha(source),source=str(source.relative_to(root)),host={'platform':platform.platform(),'machine':platform.machine()})
  assert run([compiler,'--version'],'compiler-version')==0
  for mode in ['-O0','-O2']:
   binary=out/('u128-'+mode[1:]+'.exe')
   code=run([compiler,'--target=aarch64-pc-windows-msvc','-std=c11','-Wall','-Wextra','-Werror',mode,source,'-o',binary],'compile-'+mode[1:])
   assert code==0
   data=binary.read_bytes();offset=int.from_bytes(data[60:64],'little');assert data[:2]==b'MZ' and data[offset:offset+6]==b'PE\0\0\x64\xaa'
   code=run([binary],'native-'+mode[1:]);assert code==0
   report['cases'].append({'mode':mode,'binary':binary.name,'binary_sha256':sha(binary),'size':len(data),'arithmetic_cases':6,'operation_checks':12,'native_returncode':code});save()
  report['passed']=report['completed']=True
 except Exception:
  report['error']=traceback.format_exc();save();raise
 finally:save()
 print('PASS actual O0/O2 wide arithmetic; whole CI and complete compiler repair require separate evidence.',flush=True)

if __name__=='__main__':main()
