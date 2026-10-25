import fs from 'node:fs';
import crypto from 'node:crypto';
import { pathToFileURL } from 'node:url';
const c=JSON.parse(fs.readFileSync(process.argv[2],'utf8'));
const bytes=fs.readFileSync(c.module.path), sha=b=>crypto.createHash('sha256').update(b).digest('hex');
if(bytes.length!==c.module.bytes||sha(bytes)!==c.module.sha256)throw Error('actual module pin');
const {operationBudget}=await import(pathToFileURL(c.module.path).href);
const policy={schema:1,task:'TEST_ONLY',categories:{review:2,inspection:3},operations:{
  'source.review':{category:'review',mode:'source-review'},
  'gate.review':{category:'review',mode:'gate-review'},
  'image.inspect':{category:'inspection',mode:'image-inspect'}}};
const request=(operation='gate.review',category='review',mode='gate-review')=>({task:'TEST_ONLY',operation,category,control:{operation_id:operation,mode}});
const history=(operation='gate.review',complete=false)=>({task:'TEST_ONLY',operation_id:operation,
  operation_mode:policy.operations[operation].mode,category:policy.operations[operation].category,complete});
let assertions=0;const checks=[];
const check=(name,ok)=>{assertions++;checks.push({name,ok:!!ok});if(!ok)throw Error(name)};
const rejects=(name,f)=>{let caught=false;try{f()}catch{caught=true}check(name,caught)};
const saved=JSON.stringify(policy);
check('empty history admitted',operationBudget(policy,request(),[]).used_before===0);
check('incomplete counted',operationBudget(policy,request(),[history()]).used_after===2);
check('failed complete counted',operationBudget(policy,request(),[{...history(),complete:true,result_ok:false}]).used_after===2);
check('shared category counted',operationBudget(policy,request(),[history('source.review')]).used_after===2);
check('other category independent',operationBudget(policy,request(),[history('image.inspect')]).used_before===0);
rejects('exhausted despite new stage/helper',()=>operationBudget(policy,{...request(),stage:'renamed-99',helper:'new.mjs'},[history(),history('source.review')]));
rejects('104 category hop rejected before cap borrowing',()=>operationBudget(policy,request('gate.review','inspection'),[history(),history('source.review')]));
rejects('operation alias cannot disguise gate mode',()=>operationBudget(policy,request('image.inspect','inspection','gate-review'),[]));
rejects('unknown operation',()=>operationBudget(policy,request('gate.renamed'),[]));
rejects('missing operation',()=>operationBudget(policy,{task:'TEST_ONLY',category:'review',control:{mode:'gate-review'}},[]));
rejects('missing control id',()=>operationBudget(policy,{...request(),control:{mode:'gate-review'}},[]));
rejects('wrong control id',()=>operationBudget(policy,{...request(),control:{operation_id:'image.inspect',mode:'gate-review'}},[]));
rejects('wrong control mode',()=>operationBudget(policy,request('gate.review','review','image-inspect'),[]));
rejects('cross task request',()=>operationBudget(policy,{...request(),task:'OTHER'},[]));
rejects('cross task history',()=>operationBudget(policy,request(),[{...history(),task:'OTHER'}]));
rejects('missing history operation',()=>operationBudget(policy,request(),[{task:'TEST_ONLY',category:'review',complete:false}]));
rejects('lying stored category',()=>operationBudget(policy,request(),[{...history(),category:'inspection'}]));
rejects('lying stored mode',()=>operationBudget(policy,request(),[{...history(),operation_mode:'image-inspect'}]));
rejects('nonboolean complete',()=>operationBudget(policy,request(),[{...history(),complete:null}]));
rejects('nonarray history',()=>operationBudget(policy,request(),null));
rejects('inherited operation name',()=>operationBudget(policy,request('toString'),[]));
rejects('empty table',()=>operationBudget({...policy,operations:{}},request(),[]));
rejects('duplicate mode',()=>operationBudget({...policy,operations:{...policy.operations,alias:{category:'inspection',mode:'gate-review'}}},request(),[]));
rejects('unknown routed category',()=>operationBudget({...policy,operations:{...policy.operations,alias:{category:'other',mode:'alias'}}},request(),[]));
for(const cap of [0,-1,1.5,NaN,Infinity,'2',10001])rejects('bad cap '+String(cap),()=>operationBudget({...policy,categories:{...policy.categories,review:cap}},request(),[]));
rejects('wrong schema',()=>operationBudget({...policy,schema:2},request(),[]));
rejects('null policy',()=>operationBudget(null,request(),[]));
const one=operationBudget(policy,request(),[]);one.category='inspection';
check('return cannot mutate routing',operationBudget(policy,request(),[]).category==='review');
check('policy not mutated',JSON.stringify(policy)===saved);
for(let i=0;i<512;i++){
 const rows=[history('source.review',i%2===0)];
 if(operationBudget(policy,request(),rows).remaining_after!==0)throw Error('repeat route');
 try{operationBudget(policy,request(),[...rows,history()]);throw Error('repeat incorrectly allowed')}catch(e){if(!String(e).includes('category exhausted'))throw e}
}
check('512 admission/exhaustion cycles',true);
const out={ok:true,assertions,checks,stability_cycles:512,subject:{...c.module,sha256:sha(bytes)},real_module_imported:true,
  source_review_still_required:true,target_execution:false};
fs.writeFileSync(c.result_path,JSON.stringify(out,null,2)+'\n',{flag:'wx'});
console.log(JSON.stringify({ok:true,assertions,stability_cycles:512}));
