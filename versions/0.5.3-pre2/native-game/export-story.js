'use strict';
const fs=require('fs'),vm=require('vm'),path=require('path');
const src=process.argv[2],out=process.argv[3];
if(!src||!out)throw Error('Usage: node export-story.js original/site/story.js native-game/story_data.h');
const source=fs.readFileSync(src,'utf8');
const {STORY,ENDINGS,CHARACTERS}=vm.runInNewContext(source+';({STORY,ENDINGS,CHARACTERS})');
const entries=Object.entries(STORY);
const ids=new Set(entries.map(([id])=>id));
for(const [id,node] of entries){
 for(const dest of [node.next,node.matureNext,...(node.choices||[]).map(c=>c[1])]){
  if(dest&&!ids.has(dest))throw Error(id+' references missing '+dest);
 }
}
for(const end of Object.keys(ENDINGS))if(!ids.has(end))throw Error('Missing ending '+end);
const reachable=new Set();
function visit(id){if(reachable.has(id))return;reachable.add(id);const n=STORY[id];if(!n)return;
 for(const d of [n.next,n.matureNext,...(n.choices||[]).map(c=>c[1])])if(d)visit(d);
}
visit('p0');
for(const end of Object.keys(ENDINGS))if(!reachable.has(end))throw Error('Unreachable ending '+end);
const q=s=>JSON.stringify(s===undefined||s===null?'':String(s));
const a=['/* Generated from original story.js. DO NOT EDIT game content. */',
 '#ifndef SUMMER_STORY_DATA_H','#define SUMMER_STORY_DATA_H','#include "engine.h"',
 'static const SummerNode summer_original_story[] = {'];
for(const [id,n] of entries){
 const ch=n.choices||[];
 if(ch.length>3)throw Error('More than three choices: '+id);
 a.push(' { '+[id,n.route,n.bg,n.who,n.text,n.next,n.matureNext].map(q).join(', ')+
  ', {'+[0,1,2].map(i=>q(ch[i]?.[0])).join(', ')+'}, {'+
  [0,1,2].map(i=>q(ch[i]?.[1])).join(', ')+'}, '+
  Number(n.pose||0)+', '+ch.length+', '+(n.ending?1:0)+', '+q(n.action||'still')+' },');
}
a.push('};','static const size_t summer_original_story_count = sizeof(summer_original_story)/sizeof(summer_original_story[0]);','#endif');
fs.mkdirSync(path.dirname(out),{recursive:true});fs.writeFileSync(out,a.join('\n')+'\n');
console.log('Exported '+entries.length+' original nodes, '+Object.keys(ENDINGS).length+' endings; no dialogue modified.');
