#pragma once
static const char kUpdatePage[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>BlueSquid firmware updates</title><style>
body{font:17px system-ui;max-width:620px;margin:32px auto;padding:20px;background:#0d1114;color:#f4f7f8}button,input,select{font:inherit;margin:14px 0;padding:12px}button{background:#35d4e8;border:0;border-radius:8px}button:disabled{opacity:.5}progress{width:100%}#status{white-space:pre-wrap}small{color:#bbc7cd}</style>
<h1>Firmware updates</h1><p id="device">Loading devices…</p>
<p>Keep the van parked and power connected until the update finishes.</p>
<form id="form"><label id="targetLabel" hidden>Device<br><select id="target"><option value="controller">Controller</option><option value="local">Touchscreen</option></select><br></label>
<label>Firmware package (.bsfw)<br><input id="file" type="file" accept=".bsfw" required></label><br>
<button id="button" disabled>Install update</button></form>
<small id="hint"></small><progress id="progress" max="100" value="0"></progress><p id="status" role="status"></p>
<script>
let token='',busy=false,info=null;
const status=document.querySelector('#status'),button=document.querySelector('#button'),target=document.querySelector('#target'),fileInput=document.querySelector('#file');
const sleep=ms=>new Promise(resolve=>setTimeout(resolve,ms));
function lock(value){busy=value;button.disabled=value||!info;target.disabled=fileInput.disabled=value}
async function request(path,options={}){const controller=new AbortController(),timeout=setTimeout(()=>controller.abort(),6000);try{return await fetch(path,{cache:'no-store',...options,signal:controller.signal})}finally{clearTimeout(timeout)}}
async function getInfo(){const r=await request('/info');if(!r.ok)throw Error('Update page unavailable. Check your Wi-Fi connection and reload.');info=await r.json();token=info.token;
let description=info.device+' '+info.version;if(info.relay){const c=info.controller;description+=' · Controller '+(c.connected?(c.version==='0.0.0'?'connected':c.version):'offline');document.querySelector('#targetLabel').hidden=false;document.querySelector('#hint').textContent='Stay on the BlueSquid hotspot for both updates. Update the Controller first, then the touchscreen. Cerbo readings pause during a Controller update.'}else target.value='local';
document.querySelector('#device').textContent=description;if(!busy)button.disabled=false;return info}
async function network(start){const r=await request('/controller/'+(start?'connect':'disconnect'),{method:'POST',headers:{'X-BlueSquid-OTA':token}});if(!r.ok)throw Error(await r.text()||'Controller unavailable.')}
async function prepareController(){status.textContent='Preparing Controller… Stay connected to the BlueSquid hotspot.';await network(true);const started=Date.now();while(Date.now()-started<45000){await sleep(1000);const d=await getInfo();if(d.controller.ready)return;if(d.controller.phase===3)throw Error('Controller could not join the update connection. Retry after it reconnects.')}throw Error('Controller connection timed out. Check that it is powered on and connected.')}
function upload(file,controller){return new Promise((resolve,reject)=>{const data=new FormData();data.append('firmware',file);const xhr=new XMLHttpRequest();xhr.open('POST',controller?'/controller/update':'/update');xhr.setRequestHeader('X-BlueSquid-OTA',token);xhr.timeout=300000;
xhr.upload.onprogress=e=>{if(e.lengthComputable)document.querySelector('#progress').value=100*e.loaded/e.total;status.textContent='Transferring firmware… Keep this page open.'};
xhr.onload=()=>xhr.status===200?resolve(xhr.responseText):reject(Error(xhr.responseText||'Update rejected.'));
xhr.onerror=xhr.ontimeout=()=>reject(Error('Connection lost. Reconnect and check the installed version before retrying.'));xhr.send(data)})}
document.querySelector('#form').onsubmit=async e=>{e.preventDefault();const file=fileInput.files[0];if(!file||busy||!info)return;
const controller=info.relay&&target.value==='controller',name=controller?'Controller':info.device;
if(!confirm('Install firmware on '+name+' and restart it?'))return;
lock(true);let restarting=false,prepared=false;document.querySelector('#progress').value=0;
try{const header=new Uint8Array(await file.slice(0,64).arrayBuffer());const expected=controller||info.device==='Controller'?1:2;
if(header.length!==64||String.fromCharCode(...header.slice(0,8))!=='BSQOTA1\0'||header[8]!==expected||new DataView(header.buffer).getUint32(12,true)+64!==file.size)throw Error('Choose a complete '+name+' .bsfw package.');
if(controller){prepared=true;await prepareController()}else if(!info.available)throw Error('Install the OTA partition layout by USB first.');
status.textContent=await upload(file,controller);restarting=!controller;if(restarting)status.textContent+=' Reconnect if needed and reload to confirm the version.';else status.textContent+=' You can update the touchscreen from this same page.';
}catch(error){status.textContent=error.message}finally{if(prepared){try{await network(false)}catch(error){/* Controller also restores its network after timeout/reboot. */}}lock(restarting)}};
async function poll(){if(!busy){try{await getInfo()}catch(error){document.querySelector('#device').textContent=error.message}}setTimeout(poll,3000)}poll();
</script></html>)HTML";
