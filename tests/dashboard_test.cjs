const {JSDOM,VirtualConsole}=require('jsdom');
const fs=require('fs'),assert=require('assert');
const html=fs.readFileSync(require('path').join(__dirname,'../dashboard-preview.html'),'utf8');
const names=['Wind','Precipitation','Light','Humidity','Water ingress','Temperature'];
const units=['m/s','','lx','% RH','','°C'];let requests=[],offline=false,errors=[];
let config={revision:0,token:'test-session',persistent:true,buses:[0,1].map(()=>({baud:9600,framing:0,automatic:false})),sensors:[0,1,2,3,4].map(()=>({enabled:false,id:1,fn:3,reg:0,format:0,swapBytes:false,scale:1,offset:0,normalRaw:0,alarmRaw:1}))};
let data={revision:0,setupPaused:true,setup:{active:false,saved:false,message:'Choose a sensor',tx:'',rx:'',transport:''},firmware:'1.0.0',uptime:60,wifi:true,ip:'192.168.0.42',ap:false,rssi:-54,sensors:names.map((name,i)=>({name,unit:units[i],status:i===5?'Live':'Run guided setup',valid:i===5,value:i===5?24.6:null,age:i===5?1:null,good:i===5?4:0,bad:0,raw:''})),probe:{number:0,state:'Idle',tx:'',rx:'',error:'',values:''}};
const vc=new VirtualConsole();vc.on('jsdomError',e=>errors.push(e.message));
const dom=new JSDOM(html,{url:'http://skymind.test/',runScripts:'dangerously',virtualConsole:vc,beforeParse(w){w.setTimeout=()=>0;w.fetch=async(path,opts={})=>{if(offline)throw Error('offline');if(opts.method==='POST'){requests.push({path,opts});return {ok:true,json:async()=>({ok:true})};}return {ok:true,json:async()=>structuredClone(path==='/api/config'?config:data)};}}});
const w=dom.window,d=w.document,$=id=>d.getElementById(id);
(async()=>{
 await new Promise(r=>setImmediate(r));
 assert.equal($('pauseForm').querySelector('button').textContent,'1. RS485 polling is paused');
 assert.equal($('pauseForm').querySelector('button').disabled,true);
 assert.equal(d.querySelectorAll('.card').length,6);assert.equal($('setupSensor').value,'1');assert.equal($('setupSensor').options.length,5);assert.equal($('value5').textContent,'24.6');
 for(let i=0;i<5;i++){assert.equal($('value'+i).textContent,'—');assert.equal($('state'+i).textContent,'Run guided setup');}
 data.sensors.forEach((s,i)=>Object.assign(s,{valid:true,value:[0,0,875,62.5,0,24.6][i],status:'Live',age:1,good:10}));
 w.renderStatus(data);assert.equal($('value0').textContent,'0.0');assert.equal($('value1').textContent,'Dry');assert.equal($('value4').textContent,'Dry');
 data.sensors[1].value=1;data.sensors[4].value=1;w.renderStatus(data);
 assert.equal($('value1').textContent,'Rain');assert.equal($('value4').textContent,'Wet');assert.equal(d.querySelectorAll('.card.alarm').length,2);
 Object.assign(data.sensors[4],{valid:false,value:null,status:'No reply',bad:1});w.renderStatus(data);
 assert.equal($('value4').textContent,'—');assert.equal($('state4').textContent,'No reply');assert(!$('card4').classList.contains('alarm'));
 const f=d.querySelector('#sensorForms form');f.querySelector('[name=scale]').value='0.1';f.querySelector('[name=enabled]').checked=true;
 await w.postForm(f,'/api/sensor');assert.equal(requests.at(-1).opts.headers['X-SkyMind-Token'],'test-session');assert.equal(requests.at(-1).opts.body.get('scale'),'0.1');assert.equal(requests.at(-1).opts.body.get('enabled'),'1');assert.equal(requests.at(-1).opts.body.get('swapBytes'),'0');
 assert.equal(f.querySelector('.result').textContent,'Saved on ESP32.');
 offline=true;await w.refresh();assert.equal($('connection').textContent,'Dashboard offline');for(let i=0;i<6;i++)assert.equal($('value'+i).textContent,'—');
 // Rain is selectable and sends the same explicit isolation confirmation.
 const rainForm=$('commissionForm');rainForm.querySelector('[name=sensor]').value='1';rainForm.querySelector('[name=isolated]').checked=true;
 offline=false;await w.postForm(rainForm,'/api/commission');assert.equal(requests.at(-1).opts.body.get('sensor'),'1');
 // Setup sends only the selected profile, current ID and an explicit isolation check.
 const setup=$('commissionForm');setup.querySelector('[name=isolated]').checked=true;
 setup.querySelector('[name=sensor]').value='3';
 offline=false;await w.postForm(setup,'/api/commission');
 assert.equal(requests.at(-1).opts.body.get('sensor'),'3');assert.equal(requests.at(-1).opts.body.get('currentId'),'1');
 assert.equal(requests.at(-1).opts.body.get('isolated'),'1');assert.equal(setup.querySelector('[name=isolated]').checked,false);
 data.setup.active=true;data.setup.message='Waiting for the light sensor';w.renderStatus(data);
 assert.equal(setup.querySelector('button').disabled,true);assert.equal($('resumeForm').querySelector('button').disabled,true);
 data.setup.active=false;data.setupPaused=false;w.renderStatus(data);assert.equal(setup.querySelector('button').disabled,true);
 assert.equal($('pauseForm').querySelector('button').disabled,false);
 assert.equal($('pauseForm').querySelector('button').textContent,'1. Pause RS485 polling for setup');
 assert.deepEqual(errors,[]);w.close();
 console.log('DOM tests passed: six cards, unset measurements, D4 temperature display, genuine zero and dry, wet alarms, no-reply clearing, setup POST token and values, and all readings cleared on network loss.');
})().catch(e=>{console.error(e);w.close();process.exit(1)});
