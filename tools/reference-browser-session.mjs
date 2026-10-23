import { createServer } from 'node:http';
import { createReadStream, existsSync } from 'node:fs';
import { mkdir, mkdtemp, readFile, rm } from 'node:fs/promises';
import { spawn } from 'node:child_process';
import path from 'node:path';

// Source exporters use a local-only page; the immutable browser tree is served without modification.
export async function referenceBrowser(root, profileName) {
  const source = path.join(root, 'splat-ref');
  const server = createServer((req,res) => {
    if(req.method!=='GET'){res.writeHead(405).end();return;}
    let name;try{name=decodeURIComponent(new URL(req.url,'http://localhost').pathname);}catch{res.writeHead(400).end();return;}
    if(name==='/__export.html'){
      res.writeHead(200,{'Content-Type':'text/html; charset=utf-8'}).end('<!doctype html><link rel="stylesheet" href="/styles/ui.css"><script type="importmap">{"imports":{"three":"/vendor/three/build/three.module.js","three/addons/":"/vendor/three/jsm/"}}</script>');return;
    }
    const file=path.resolve(source,'.'+name);
    if(!file.startsWith(source+path.sep)){res.writeHead(403).end();return;}
    const stream=createReadStream(file);stream.on('error',()=>{if(!res.headersSent)res.writeHead(404);res.end();});
    stream.on('open',()=>{res.writeHead(200,{'Content-Type':({'.js':'text/javascript','.css':'text/css','.woff2':'font/woff2','.json':'application/json'})[path.extname(file)]||'application/octet-stream'});stream.pipe(res);});
  });
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  const candidates=[process.env.EDGE_PATH,...['PROGRAMFILES(X86)','PROGRAMFILES','LOCALAPPDATA'].map(key=>process.env[key]&&path.join(process.env[key],'Microsoft/Edge/Application/msedge.exe'))].filter(Boolean);
  const executable=candidates.find(existsSync);
  if(!executable){server.close();throw new Error('Microsoft Edge was not found; set EDGE_PATH');}
  await mkdir(path.join(root,'build'),{recursive:true});
  const profile=await mkdtemp(path.join(root,'build',profileName+'-'));
  // Keep Canvas rasterization reproducible while leaving the source WebGL texture baker on the GPU.
  const child=spawn(executable,['--headless=new','--no-first-run','--no-default-browser-check','--disable-extensions','--disable-accelerated-2d-canvas','--remote-debugging-port=0',`--user-data-dir=${profile}`,'about:blank'],{windowsHide:true,stdio:'ignore'});
  const pause=ms=>new Promise(resolve=>setTimeout(resolve,ms));
  let socket;const pending=new Map();let request=0;
  async function close(){
    if(socket){try{socket.send(JSON.stringify({id:++request,method:'Browser.close'}));}catch{}socket.close();}
    for(const waiter of pending.values()){clearTimeout(waiter.timer);waiter.reject(new Error('Source browser closed'));}pending.clear();
    if(child.exitCode===null)await Promise.race([new Promise(resolve=>child.once('exit',resolve)),pause(2000)]);
    if(child.exitCode===null)child.kill();
    await new Promise(resolve=>server.close(resolve));
    await rm(profile,{recursive:true,force:true,maxRetries:10,retryDelay:100});
  }
  try{
    let port;
    for(let i=0;i<200;i++){
      if(child.exitCode!==null)throw new Error('Source browser exited before DevTools opened');
      try{port=Number((await readFile(path.join(profile,'DevToolsActivePort'),'utf8')).split(/\r?\n/)[0]);if(port>0)break;}catch{}
      await pause(100);
    }
    if(!port)throw new Error('Timed out waiting for Edge DevTools');
    const info=await (await fetch(`http://127.0.0.1:${port}/json/version`)).json();
    socket=new WebSocket(info.webSocketDebuggerUrl);
    await new Promise((resolve,reject)=>{socket.addEventListener('open',resolve,{once:true});socket.addEventListener('error',reject,{once:true});});
    socket.addEventListener('message',event=>{
      const message=JSON.parse(event.data),waiter=pending.get(message.id);if(!waiter)return;
      pending.delete(message.id);clearTimeout(waiter.timer);
      if(message.error)waiter.reject(new Error(message.error.message));else waiter.resolve(message.result||{});
    });
    const send=(method,params={},sessionId)=>new Promise((resolve,reject)=>{
      const id=++request,timer=setTimeout(()=>{pending.delete(id);reject(new Error(`Source browser timeout: ${method}`));},240000);
      pending.set(id,{resolve,reject,timer});socket.send(JSON.stringify({id,method,params,...(sessionId?{sessionId}:{})}));
    });
    const {targetId}=await send('Target.createTarget',{url:'about:blank'});
    const {sessionId}=await send('Target.attachToTarget',{targetId,flatten:true});
    await send('Page.enable',{},sessionId);await send('Runtime.enable',{},sessionId);
    await send('Page.navigate',{url:`http://127.0.0.1:${server.address().port}/__export.html`},sessionId);
    const evaluate=async(expression)=>{
      const result=await send('Runtime.evaluate',{expression,awaitPromise:true,returnByValue:true},sessionId);
      if(result.exceptionDetails)throw new Error(result.exceptionDetails.exception?.description||result.exceptionDetails.text);
      return result.result?.value;
    };
    let ready=false;
    for(let i=0;i<100;i++){try{ready=await evaluate('document.readyState === "complete"');}catch{}if(ready)break;await pause(100);}
    if(!ready)throw new Error('Source export page did not finish loading');
    return {evaluate,close};
  }catch(error){await close();throw error;}
}
