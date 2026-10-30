#pragma once
// The page the Cardputer serves to a phone or computer on its hotspot (src/webshare.cpp).
// Plain HTML/CSS/JS, no external files, so it works with no internet.
static const char WEBSHARE_PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>SmartBoot files</title>
<style>
:root{color-scheme:dark;--bg:#0e1117;--s:#1a1f2b;--s2:#262c3b;--t:#edf0f7;--m:#8c95a8;--a:#4f8cff;--ok:#3dd68c;--bad:#ff5c6c}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--t);font:16px/1.4 system-ui,sans-serif}
main{max-width:720px;margin:0 auto;padding:16px}
header{display:flex;justify-content:space-between;align-items:baseline;gap:12px}
h1{margin:0;font-size:22px}
#free{color:var(--m);font-size:14px}
#crumbs{margin:12px 0;color:var(--m);word-break:break-all}
#crumbs a{color:var(--a);text-decoration:none}
#drop{background:var(--s);border:2px dashed var(--s2);border-radius:14px;padding:16px;text-align:center}
#drop.over{border-color:var(--a)}
#drop p{margin:0 0 10px;color:var(--m)}
button,.btn{display:inline-block;background:var(--s2);color:var(--t);border:0;border-radius:10px;padding:10px 14px;margin:3px;font:inherit;cursor:pointer}
.go{background:var(--a);color:#fff}
.sm{padding:5px 10px;font-size:14px}
#queue{margin:12px 0}
.q{background:var(--s);border-radius:10px;padding:8px 12px;margin:6px 0;font-size:14px}
.q .n{display:flex;justify-content:space-between;gap:8px}
.q .n span{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.bar{height:5px;background:var(--s2);border-radius:3px;margin-top:6px}
.bar i{display:block;height:100%;width:0;background:var(--a);border-radius:3px}
.q.done .bar i{background:var(--ok)}.q.err{color:var(--bad)}.q.err .bar i{background:var(--bad)}
ul{list-style:none;margin:12px 0;padding:0}
li{display:flex;align-items:center;gap:8px;background:var(--s);border-radius:10px;padding:8px 12px;margin:6px 0}
li .nm{flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;cursor:pointer}
li .nm.f{cursor:default}
li .sz{color:var(--m);font-size:13px}
li a.dl{color:var(--a);text-decoration:none;font-size:14px;padding:5px 8px}
#empty{color:var(--m);text-align:center;padding:20px}
#toast{position:fixed;left:50%;bottom:18px;transform:translateX(-50%);background:var(--s2);padding:10px 16px;border-radius:10px;opacity:0;transition:opacity .2s;pointer-events:none}
#toast.on{opacity:1}
</style></head><body><main>
<header><h1>SmartBoot</h1><span id="free"></span></header>
<div id="crumbs"></div>
<div id="drop">
<p>Drop files here, or choose them</p>
<label class="btn go">Add files<input id="pick" type="file" multiple hidden></label>
<label class="btn">Add a folder<input id="pickdir" type="file" webkitdirectory hidden></label>
<button id="mk">New folder</button>
</div>
<div id="queue"></div>
<ul id="list"></ul>
<div id="empty" hidden>This folder is empty.</div>
</main><div id="toast"></div>
<script>
const $=s=>document.querySelector(s),enc=encodeURIComponent;
let cwd='/';
const join=(a,b)=>a==='/'?'/'+b:a+'/'+b;
const fmt=n=>n<1024?n+' B':n<1048576?(n/1024).toFixed(0)+' KB':n<1073741824?(n/1048576).toFixed(1)+' MB':(n/1073741824).toFixed(2)+' GB';
function el(tag,cls,text){const e=document.createElement(tag);if(cls)e.className=cls;if(text!=null)e.textContent=text;return e}
let tt;function toast(m){const t=$('#toast');t.textContent=m;t.classList.add('on');clearTimeout(tt);tt=setTimeout(()=>t.classList.remove('on'),2600)}
async function post(u){const r=await fetch(u,{method:'POST'});if(!r.ok)throw new Error(await r.text()||r.status);}
async function go(p){
  cwd=p;let items=[],off=0,free=-1,more=true;
  while(more){
    const r=await fetch('/ls?p='+enc(p)+'&o='+off);
    if(!r.ok){toast('Cannot open that folder');if(p!=='/')return go('/');break}
    const j=await r.json();items=items.concat(j.e);off+=j.e.length;more=j.more&&j.e.length;free=j.free;
  }
  items.sort((a,b)=>b.d-a.d||a.n.localeCompare(b.n,undefined,{numeric:true,sensitivity:'base'}));
  const c=$('#crumbs');c.textContent='';
  let acc='';const root=el('a',null,'SD card');root.href='#';root.onclick=e=>{e.preventDefault();go('/')};c.append(root);
  p.split('/').filter(Boolean).forEach(s=>{acc+='/'+s;const t=acc,a=el('a',null,s);a.href='#';a.onclick=e=>{e.preventDefault();go(t)};c.append(' / ',a)});
  $('#free').textContent=free>=0?fmt(free)+' free':'';
  const ul=$('#list');ul.textContent='';$('#empty').hidden=items.length>0;
  for(const it of items){
    const path=join(p,it.n),li=el('li');
    const nm=el('span','nm'+(it.d?'':' f'),(it.d?'\u{1F4C1} ':'')+it.n);
    if(it.d)nm.onclick=()=>go(path);
    li.append(nm);
    if(!it.d){li.append(el('span','sz',fmt(it.s)));const d=el('a','dl','Save');d.href='/dl?p='+enc(path);d.download=it.n;li.append(d)}
    const rn=el('button','sm','Rename');rn.onclick=async()=>{const n=prompt('New name',it.n);if(!n||n===it.n)return;try{await post('/mv?p='+enc(path)+'&to='+enc(join(p,n)));go(p)}catch(e){toast('Rename failed')}};
    const rm=el('button','sm','Delete');rm.onclick=async()=>{if(!confirm('Delete "'+it.n+'"'+(it.d?' and everything in it':'')+'?'))return;try{await post('/rm?p='+enc(path));go(p)}catch(e){toast('Delete failed')}};
    li.append(rn,rm);ul.append(li);
  }
}
function send(f,dir,row){
  return new Promise(res=>{
    const x=new XMLHttpRequest(),bar=row.querySelector('i'),st=row.querySelector('em');
    x.open('POST','/up?p='+enc(dir));
    x.upload.onprogress=e=>{if(e.lengthComputable){bar.style.width=(e.loaded/e.total*100)+'%';st.textContent=fmt(e.loaded)+' of '+fmt(e.total)}};
    x.onload=()=>{if(x.status===200){row.classList.add('done');bar.style.width='100%';st.textContent='Done'}else{row.classList.add('err');st.textContent=x.responseText||'Failed'}res()};
    x.onerror=()=>{row.classList.add('err');st.textContent='Connection lost';res()};
    const d=new FormData();d.append('file',f,f.name);x.send(d);
  });
}
let busy=false;
async function upload(files){
  if(busy){toast('Wait for the current upload');return}
  busy=true;const q=$('#queue');q.textContent='';const base=cwd;
  const rows=[...files].map(f=>{
    const r=el('div','q'),n=el('div','n'),name=f.webkitRelativePath||f.name;
    n.append(el('span',null,name),el('em',null,fmt(f.size)));
    const b=el('div','bar');b.append(el('i'));r.append(n,b);q.append(r);return[f,name,r];
  });
  for(const[f,name,r]of rows){
    const i=name.lastIndexOf('/'),dir=i<0?base:join(base,name.slice(0,i));
    await send(f,dir,r);
  }
  busy=false;go(cwd);
}
$('#pick').onchange=e=>{upload(e.target.files);e.target.value=''};
$('#pickdir').onchange=e=>{upload(e.target.files);e.target.value=''};
$('#mk').onclick=async()=>{const n=prompt('Folder name');if(!n)return;try{await post('/mkdir?p='+enc(join(cwd,n)));go(cwd)}catch(e){toast('Could not create the folder')}};
const dz=$('#drop');
dz.ondragover=e=>{e.preventDefault();dz.classList.add('over')};
dz.ondragleave=()=>dz.classList.remove('over');
dz.ondrop=e=>{e.preventDefault();dz.classList.remove('over');if(e.dataTransfer.files.length)upload(e.dataTransfer.files)};
go('/');
</script></body></html>
)HTML";
