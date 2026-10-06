#!/usr/bin/env python3
"""Real GRUB/QEMU acceptance: boot, scheduler, desktop input, installed VM, reboot.
Writes only newly-created temporary app-store images. Uses QMP stdio, no sockets.
"""
import argparse, json, os, pathlib, select, shutil, subprocess, sys, time
ROOT=pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import disk as disktool

def check(value, text):
    if not value: raise AssertionError(text)
    print('PASS', text, flush=True)
class Guest:
    def __init__(self, out, image, memory=64, cpus=4):
        self.out=out; self.log=out/'debug.log'; self.sequence=0
        runtime=ROOT.parent/'gtos-runtime'; env=os.environ.copy()
        qemu=shutil.which('qemu-system-i386'); bios=[]
        if env.get('GTOS_QEMU_DATA_DIR'): bios=['-L',env['GTOS_QEMU_DATA_DIR']]
        if (runtime/'root/usr/bin/qemu-system-i386').exists():
            qemu=str(runtime/'root/usr/bin/qemu-system-i386')
            env['LD_LIBRARY_PATH']=str(runtime/'root/usr/lib/x86_64-linux-gnu')
            env['QEMU_MODULE_DIR']=str(runtime/'root/usr/lib/x86_64-linux-gnu/qemu')
            bios=['-L',str(runtime/'root/usr/share/qemu')]
        if not qemu: raise RuntimeError('qemu-system-i386 is required')
        cmd=[qemu]+bios+['-machine','pc','-accel','tcg','-m',str(memory)+'M','-smp',str(cpus),'-cdrom',str(ROOT/'GTOS.iso'),'-boot','d','-drive','file='+str(image)+',format=raw,if=ide,index=0','-nic','none','-display','none','-qmp','stdio','-no-reboot','-no-shutdown','-debugcon','file:'+str(self.log),'-global','isa-debugcon.iobase=0xe9']
        self.err=open(out/'qemu.log','w');self.p=subprocess.Popen(cmd,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=self.err,env=env,bufsize=0)
        self.buffer=b'';self.line(10);self.call('qmp_capabilities');self.wait('SCHEDULER RUNTIME PASS',15)
    def line(self,timeout):
        end=time.monotonic()+timeout
        while b'\n' not in self.buffer:
            remaining=end-time.monotonic()
            if remaining<=0 or not select.select([self.p.stdout],[],[],remaining)[0]: raise TimeoutError('QMP read timeout')
            chunk=os.read(self.p.stdout.fileno(),65536)
            if not chunk: raise RuntimeError('QEMU exited: '+str(self.p.poll()))
            self.buffer+=chunk
        line,self.buffer=self.buffer.split(b'\n',1)
        return json.loads(line)
    def call(self,command,args=None):
        self.sequence+=1;ident=self.sequence
        value={'execute':command,'id':ident}
        if args is not None:value['arguments']=args
        self.p.stdin.write((json.dumps(value)+'\n').encode());self.p.stdin.flush()
        while True:
            value=self.line(10)
            if value.get('id')==ident:
                if 'error'in value:raise RuntimeError(str(value))
                return value.get('return')
    def text(self):return self.log.read_text() if self.log.exists() else ''
    def wait(self,needle,timeout=5):
        end=time.monotonic()+timeout
        while time.monotonic()<end:
            text=self.text()
            if 'PANIC' in text or 'SELFTEST FAIL' in text or 'RUNTIME FAIL' in text:raise AssertionError(text)
            if needle in text:return
            time.sleep(.03)
        raise TimeoutError(needle+'\n'+self.text())
    def key(self,key,hold=80):
        self.call('human-monitor-command',{'command-line':f'sendkey {key} {hold}'})
        time.sleep(hold/1000+.12)
    def mouse(self,dx,dy,click=False):
        for axis,value in [('x',dx),('y',dy)]:
            while value:
                step=max(-90,min(90,value));value-=step
                self.call('input-send-event',{'events':[{'type':'rel','data':{'axis':axis,'value':step}}]})
                time.sleep(.05)
        if click:
            for down in [True,False]:
                self.call('input-send-event',{'events':[{'type':'btn','data':{'down':down,'button':'left'}}]});time.sleep(.1)
    def screenshot(self,name):
        path=(self.out/(name+'.ppm')).resolve();self.call('screendump',{'filename':str(path)})
        try:
            from PIL import Image
            im=Image.open(path);im.resize((960,600),Image.Resampling.NEAREST).save(self.out/(name+'.png'))
            return im.resize((320,200),Image.Resampling.NEAREST)
        except ImportError:return None
    def close(self):
        if self.p.poll() is None:
            try:self.call('quit');self.p.wait(timeout=5)
            except Exception:self.p.kill();self.p.wait()
        self.err.close()
def boot(out,image,memory=64,cpus=4):
    out.mkdir(parents=True,exist_ok=True)
    g=Guest(out,image,memory,cpus)
    check('PHYSICAL SELFTEST PASS' in g.text(),'physical allocator boot self-test')
    check('HEAP SELFTEST PASS' in g.text(),'heap boot self-test')
    check(f'CPU DETECTED {cpus:08X}' in g.text(),f'{cpus} firmware CPUs detected')
    check('CPU ONLINE 00000001' in g.text(),'BSP-only scheduling reported honestly')
    check(f'AP PARKED {cpus-1:08X}' in g.text(),'secondary CPUs acknowledged, self-tested and safely parked')
    check('SCHEDULER RUNTIME PASS' in g.text(),'real task sleep/yield/return and GUI coexist')
    check('SYSCALL ABI PASS' in g.text(),'software interrupt 0x80 reaches handler')
    return g
def paddle(im):
    if im is None:return None
    pts=[x for y in range(142,161) for x in range(24,296) if im.getpixel((x,y))[0]>150 and im.getpixel((x,y))[1]>210 and im.getpixel((x,y))[2]<140]
    return sum(pts)/len(pts) if pts else None
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--output',default=str(ROOT/'obj/qemu-tests'));args=ap.parse_args()
    out=pathlib.Path(args.output).resolve();out.mkdir(parents=True,exist_ok=True)
    image=out/'apps.img'
    if image.exists():raise RuntimeError('Use a new output directory; never overwrite an existing test image')
    subprocess.run([sys.executable,str(ROOT/'tools/disk.py'),'create',str(image),'--size-mib','8'],check=True)
    g=boot(out/'boot64',image)
    try:
        g.screenshot('desktop');g.mouse(-265,97,True);im=g.screenshot('mouse-apps')
        check(im is not None and im.getpixel((6,101))!=im.getpixel((6,51)),'actual mouse opens the app list')
        g.key('2');g.wait('UI HARDWARE');g.screenshot('hardware')
        g.key('3');g.key('i');g.wait('APP INSTALL OK');g.screenshot('installed-apps')
        g.key('ret');g.wait('APP LAUNCH OK');before=g.screenshot('game-before')
        g.key('right',350);after=g.screenshot('game-after')
        a,b=paddle(before),paddle(after)
        if a is not None and b is not None:check(b>a+10,'actual game paddle responds to right-arrow input')
        else:raise AssertionError('Pillow and visible paddle are required for gameplay acceptance')
        g.key('r');g.wait('APP RESTART OK');g.key('esc');g.wait('APP CLOSE OK')
        g.key('ret');check(g.text().count('APP LAUNCH OK')>=2,'close and relaunch application')
        g.mouse(265,-97,True);check(g.text().count('APP CLOSE OK')>=2,'mouse closes running app')
        g.key('ret');g.mouse(0,0,True);check(g.text().count('APP CLOSE OK')>=3,'repeated mouse close after relaunch')
    finally:g.close()
    subprocess.run([sys.executable,str(ROOT/'tools/disk.py'),'check',str(image)],check=True)
    g=boot(out/'reboot64',image)
    try:
        check('APP STORE COUNT 00000001' in g.text(),'installation persisted across full QEMU reboot')
        g.key('3');g.screenshot('persistent-app');g.key('ret');g.wait('APP LAUNCH OK');g.key('esc');g.key('u');g.wait('APP REMOVE OK');g.screenshot('removed-app')
    finally:g.close()
    g=boot(out/'reboot32',image,32,1)
    try:
        check('APP STORE COUNT 00000000' in g.text(),'removal persisted across full reboot')
        g.key('i');g.wait('APP INSTALL OK');g.key('ret');g.wait('APP LAUNCH OK');g.screenshot('reinstalled-game')
    finally:g.close()
    g=boot(out/'boot128',image,128,4)
    try:check('APP STORE COUNT 00000001' in g.text(),'reinstalled package persisted in 128-MiB guest')
    finally:g.close()
    invalid=out/'unformatted.img'
    with invalid.open('xb') as f:f.truncate(8*1024*1024)
    import hashlib
    before=hashlib.sha256(invalid.read_bytes()).digest()
    g=boot(out/'unformatted',invalid)
    try:
        check('APP STORE UNAVAILABLE' in g.text(),'unformatted disk refused without autoformat')
        g.key('i');g.wait('APP INSTALL FAILED')
    finally:g.close()
    check(hashlib.sha256(invalid.read_bytes()).digest()==before,'unformatted disk bytes unchanged after refused install')
    print('QEMU acceptance passed. Evidence: '+str(out),flush=True)
if __name__=='__main__':main()
