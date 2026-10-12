const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const source=fs.readFileSync(require('node:path').join(__dirname,'../main/web/index.html'),'utf8').match(/<script>([\s\S]*?)<\/script>/)[1];
const elements=new Map();
function element(){return {value:'',hidden:false,textContent:'',children:[],classList:{toggle(){}},querySelectorAll(){return []}}}
const context={console,sessionStorage:{getItem(){return null},setItem(){},removeItem(){}},document:{getElementById(id){if(!elements.has(id))elements.set(id,element());return elements.get(id)},querySelectorAll(){return []},createElement(){return element()}},setInterval(){},setTimeout(){},fetch:async()=>{throw Error('network not mocked')},URL,Error};
vm.createContext(context);vm.runInContext(source,context);
assert.equal(vm.runInContext('seconds(3661)',context),'01:01:01');
assert.equal(vm.runInContext('seconds(-1)',context),'00:00:00');
context.document.getElementById('timerMinutes').value='5';assert.equal(vm.runInContext("minutes('timerMinutes')",context),300);
for(const value of ['0','1441','NaN','1.5']){elements.get('timerMinutes').value=value;assert.throws(()=>vm.runInContext("minutes('timerMinutes')",context))}
(async()=>{let called;context.fetch=async(path,options)=>{called={path,options};return {ok:true,json:async()=>({ok:true})}};
vm.runInContext("token='0123456789abcdef0123456789abcdef'",context);
await vm.runInContext("command('timer',{seconds:300})",context);
assert.equal(called.path,'/api/command');assert.equal(called.options.headers['X-Device-Token'],'0123456789abcdef0123456789abcdef');assert.deepEqual(JSON.parse(called.options.body),{action:'timer',seconds:300});
context.fetch=async()=>({ok:false,status:401,text:async()=> 'Unauthorized'});await assert.rejects(vm.runInContext("api('/api/status')",context),/Unauthorized/);assert.equal(elements.get('auth').hidden,false);assert.equal(elements.get('app').hidden,true);
console.log('web tests passed: timer validation, command JSON, authentication and unauthorized state');})().catch(e=>{console.error(e);process.exitCode=1});
