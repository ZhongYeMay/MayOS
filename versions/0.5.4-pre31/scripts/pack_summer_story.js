#!/usr/bin/env node
// Build-time data export ONLY. Native MayKernel never executes JavaScript.
// Reads the unmodified trusted original story and emits UTF-8 C data literals.
"use strict";
const fs=require("node:fs");
const path=require("node:path");
const vm=require("node:vm");
const args=process.argv.slice(2);
if(args.length!==2){
  console.error("Usage: node scripts/pack_summer_story.js <original-site/story.js> <output-header>");
  process.exit(2);
}
const [sourceFile,outputFile]=args;
const source=fs.readFileSync(sourceFile,"utf8");
const sandbox=vm.createContext(Object.create(null));
vm.runInContext(source+"\n;globalThis.__mayosExport={STORY,CHARACTERS,ENDINGS};",
                sandbox,{timeout:2500,filename:path.basename(sourceFile)});
const game=sandbox.__mayosExport;
const entries=Object.entries(game.STORY);
if(entries.length<50 || Object.keys(game.ENDINGS).length!==6)
  throw new Error("Original game nodes or six endings missing");
const escapeC=(value)=>{
  // JSON string representation is accepted by the C compiler as a UTF-8
  // literal; C source does not alter the original dialogue text.
  return JSON.stringify(typeof value==="string"?value:"");
};
let header=[
"/* Generated from original site/story.js. Dialogues/targets unchanged. */",
"#ifndef MAYOS_ORIGINAL_SUMMER_STORY_H",
"#define MAYOS_ORIGINAL_SUMMER_STORY_H",
"typedef struct {",
"  const char *id, *route, *who, *title, *text, *next;",
"  const char *branch[3];",
"  int pose, ending;",
"} MayGameStoryNode;",
"static const MayGameStoryNode maygame_story[]={"
];
for(const [id,node] of entries) {
  const branches=(node.choices||[]).map(choice=>choice[1]);
  if(branches.length>3)throw new Error("More than 3 original choices");
  const fields=[id,node.route||"",node.who||"",node.title||"",node.text||"",
                typeof node.next==="string"?node.next:""];
  const branchTargets=Array.from({length:3},(_,i)=>branches[i]||"");
  header.push("  {"+fields.map(escapeC).join(",")+
      ",{"+branchTargets.map(escapeC).join(",")+"},"+
      (Number.isInteger(node.pose)?node.pose:0)+","+(node.ending?1:0)+"},");
}
header.push("};");
header.push("#define MAYGAME_STORY_COUNT (sizeof(maygame_story)/sizeof(maygame_story[0]))");
header.push("#endif");
fs.mkdirSync(path.dirname(outputFile),{recursive:true});
fs.writeFileSync(outputFile,header.join("\n")+"\n","utf8");
console.log("Exported "+entries.length+" untouched story nodes and "+
           Object.keys(game.ENDINGS).length+" endings for MayKernel");
