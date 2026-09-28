#pragma once
#include <Arduino.h>
static const char kWebRemotePage[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>BlueSquid remote</title><style>
body{font:17px system-ui;background:#0d1114;color:#f4f7f8;margin:0;padding:20px;max-width:650px;margin:auto}h1{font-size:26px}section{background:#20262b;border-radius:16px;padding:16px;margin:12px 0}button{padding:12px 24px;margin:6px;border:0;border-radius:10px;font:inherit;background:#35d4e8}button:disabled,input:disabled{opacity:.4}input[type=range]{width:100%;margin:20px 0}small{color:#9aa7ae}#error{color:#ffbe55;min-height:25px}label{display:block;margin:10px 0}</style>
<h1>BlueSquid</h1><p id="connection">Connecting…</p><p id="error" role="status"></p><div id="controls"></div>
<section><b>Power</b><p id="power">--</p></section>
<script>
let token='',busy=false;
const root=document.querySelector('#controls');
async function command(kind,target,value){if(busy)return;busy=true;document.querySelector('#error').textContent='';try{
 const r=await fetch('/api/command',{method:'POST',headers:{'X-BlueSquid-Token':token,'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({kind,target,value})});
 if(!r.ok)throw Error('Command not sent. Check the Controller connection.');
 document.querySelector('#error').textContent='Command sent; waiting for feedback.';
}catch(e){document.querySelector('#error').textContent=e.message}finally{busy=false}}
function row(kind,index,title){const s=document.createElement('section');const h=document.createElement('b');h.textContent=title;s.append(h);const state=document.createElement('p');s.append(state);
 for(const [label,value] of [['On',1],['Off',0]]){const b=document.createElement('button');b.textContent=label;b.onclick=()=>command(kind,index,value);s.append(b)}
 const range=document.createElement('input');range.type='range';range.min=0;range.max=100;
 if(kind==='rgb'){range.onchange=()=>command('rgbLevel',index,range.value);s.append(range)}
 if(kind==='rgb'){for(const [label,key] of [['Colour','rgbColour'],['Warm white','rgbWhite'],['Cool white','rgbCoolWhite']]){const l=document.createElement('label'),c=document.createElement('input');c.type='checkbox';c.dataset.key=key;c.onchange=()=>command(key,index,c.checked?1:0);l.append(c,document.createTextNode(' '+label));s.append(l)}
 const c=document.createElement('input');c.type='color';c.onchange=()=>{const n=parseInt(c.value.slice(1),16);command('rgbHex',index,n)};s.append(c)}
 root.append(s);return {s,state,range,h}}
const rgb=Array.from({length:4},(_,i)=>row('rgb',i,'RGB Light '+(i+1)));
const aux=[0,1,2,3].map(i=>row('output',i,'Output '+(i+1)));
const inverter=row('inverter',0,'Inverter'),charger=row('charger',0,'Shore charger');
function updateRow(row,data,online){row.h.textContent=data.label;row.state.textContent=data.available&&online?(data.on?'On':'Off'):'Unavailable';row.s.querySelectorAll('input,button').forEach(x=>x.disabled=!online||!data.available);if(document.activeElement!==row.range)row.range.value=data.level||0;
 row.s.querySelectorAll('[data-key]').forEach(x=>{x.checked=x.dataset.key==='rgbColour'?data.colour:x.dataset.key==='rgbWhite'?data.white:data.coolWhite;x.parentElement.hidden=!data.full||(data.rgbOnly&&x.dataset.key!=='rgbColour')});row.s.querySelectorAll('[type=color]').forEach(x=>x.hidden=!data.full)}
async function poll(){try{const r=await fetch('/api/status',{cache:'no-store'});if(!r.ok)throw Error();const d=await r.json();token=d.token;document.querySelector('#connection').textContent=d.online?'Controller connected':'Controller offline';
 rgb.forEach((x,i)=>updateRow(x,d.rgb[i],d.online));aux.forEach((x,i)=>updateRow(x,d.outputs[i],d.online));updateRow(inverter,d.inverter,d.online);updateRow(charger,d.charger,d.online);
 document.querySelector('#power').textContent=d.energyValid?`${d.soc}% · ${d.voltage} V · Solar ${d.solar} W · DC/DC ${d.dcdc} W · AC ${d.ac} W`:'Cerbo unavailable';
 }catch(e){document.querySelector('#connection').textContent='Hotspot connection lost';root.querySelectorAll('input,button').forEach(x=>x.disabled=true)}finally{setTimeout(poll,1000)}}poll();
</script></html>)HTML";
