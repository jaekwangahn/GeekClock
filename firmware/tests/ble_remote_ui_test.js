// Run with: node firmware/tests/ble_remote_ui_test.js
const vm=require('vm'),fs=require('fs'),assert=require('assert'),path=require('path');
const html=fs.readFileSync(path.join(__dirname,'../ble-remote.html'),'utf8'),code=html.match(/<script>([\s\S]*?)<\/script>/)[1];
class Element{constructor(){this.textContent='';this.hidden=false;this.disabled=false;this.value='85';this.children=[];this.dataset={};this.handlers={};this.attrs={};this.classes=new Set();this.classList={toggle:(k,on)=>{if(on)this.classes.add(k);else this.classes.delete(k);},remove:k=>this.classes.delete(k)};}addEventListener(n,f){this.handlers[n]=f;}setAttribute(k,v){this.attrs[k]=v;}append(...v){this.children.push(...v);}replaceChildren(...v){this.children=v;}}
const ids={},buttons=[];
for(const m of html.matchAll(/id="([^"]+)"/g))ids[m[1]]=new Element();
for(const m of html.matchAll(/<button\b([^>]*)>/g)){const attrs=m[1],b=new Element();for(const d of attrs.matchAll(/data-([a-z]+)="([^"]+)"/g))b.dataset[d[1]]=d[2];if(Object.keys(b.dataset).length)buttons.push(b);}
const select=s=>buttons.filter(b=>s.split(',').some(part=>{const m=part.match(/\[data-([^\]]+)\]/);return m&&b.dataset[m[1]]!==undefined;}));
const writes=[],listeners={};let autoAck=true;
const status={ok:true,screen:'clock',character:'chiikawa',brightness:85,pet:{fullness:80,happiness:80,energy:80,level:2},pomodoro:{phase:'focus',running:false,remainingSeconds:1500,completedFocus:1,totalFocusMinutes:25},room:{coins:6,wallpaper:0,rug:0,bed:0,decor:7,ownedMask:64}};
let context;
const notify=(obj,chunk=20)=>{const bytes=new TextEncoder().encode(JSON.stringify(obj)+'\n');for(let i=0;i<bytes.length;i+=chunk){const b=bytes.slice(i,i+chunk);context.onNotification({target:{value:new DataView(b.buffer,b.byteOffset,b.byteLength)}});}};
const rx={writeValueWithResponse:async payload=>{const command=new TextDecoder().decode(payload).trim();writes.push(command);if(autoAck)notify({...status,command});}};
const tx={addEventListener:(n,f)=>listeners[n]=f,removeEventListener:n=>delete listeners[n],startNotifications:async()=>tx};
const device={name:'Chiikawa-PRO',addEventListener:(n,f)=>listeners[n]=f,gatt:{connected:false,connect:async()=>{device.gatt.connected=true;return{getPrimaryService:async()=>({getCharacteristic:async id=>id.startsWith('6e400002')?rx:tx})};},disconnect:()=>{device.gatt.connected=false;listeners.gattserverdisconnected?.();}}};
const sandbox={document:{getElementById:id=>ids[id],querySelectorAll:select,createElement:()=>new Element()},navigator:{bluetooth:{requestDevice:async()=>device}},isSecureContext:true,TextEncoder,TextDecoder,Date,Math,Number,JSON,Promise,Error,Uint8Array,setInterval:()=>{},setTimeout,clearTimeout,console};
context=vm.createContext(sandbox);vm.runInContext(code,context);
const flush=()=>new Promise(resolve=>setImmediate(resolve));
(async()=>{
 await context.connect();assert.equal(ids.controls.disabled,false);assert.equal(writes[0],'status');assert.equal(ids.timer.textContent,'25:00');assert.equal(ids.wallet.textContent,'6 코인');assert.equal(ids.shop.children.length,8);
 assert(buttons.find(b=>b.dataset.screen==='clock').classes.has('active'));
 const text='한글 오류를 분할해서 받아요';notify({ok:false,error:text},1);assert.equal(ids.feedback.textContent,text);
 ids.brightness.value='55';vm.runInContext('brightnessDirty=true',context);notify({...status,brightness:30});assert.equal(ids.brightness.value,'55');
 autoAck=false;writes.length=0;
 const p1=context.send('screen pet'),p2=context.send('pomo start');await flush();assert.deepEqual(writes,['screen pet']);
 notify({...status,screen:'pet'});await flush();assert.deepEqual(writes,['screen pet'],'periodic status must not acknowledge command');
 notify({...status,command:'char next'});await flush();assert.deepEqual(writes,['screen pet'],'unmatched command must not advance queue');
 notify({...status,screen:'pet',command:'screen pet'});await p1;await flush();assert.deepEqual(writes,['screen pet','pomo start']);
 notify({...status,command:'pomo start',pomodoro:{...status.pomodoro,running:true}});await p2;
 const bad=context.send('room buy 8');await flush();notify({...status,ok:false,command:'room buy 8',error:'insufficient_coins'});await assert.rejects(bad,/코인이 부족/);
 const waiting=context.send('status');await flush();device.gatt.disconnect();await assert.rejects(waiting,/연결이 끊/);assert.equal(ids.controls.disabled,true);assert.equal(ids.timer.textContent,'—:—');
 autoAck=true;await context.connect();assert.equal(ids.controls.disabled,false);assert.equal(ids.timer.textContent,'25:00');
 for(const b of buttons.filter(b=>b.dataset.command)){assert(new TextEncoder().encode(b.dataset.command+'\n').length<=20,b.dataset.command);}
 const insecure=vm.createContext({...sandbox,isSecureContext:false,navigator:{},document:{...sandbox.document,getElementById:id=>new Element()}});vm.runInContext(code,insecure);
 console.log('BLE UI: split UTF8/JSON, matching acknowledgments, serialized writes, server errors, dirty brightness, disconnect/reconnect, command20B bounds, unsupported contexts PASS');
})().catch(e=>{console.error(e);process.exitCode=1;});
