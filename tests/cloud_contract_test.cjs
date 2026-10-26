// Node 22.13+, C++ compiler, and the website checkout's existing npm dependencies.
// Uses the actual firmware encoder and actual ingest handler, with in-memory test ownership only.
const fs=require('node:fs'),os=require('node:os'),path=require('node:path');
const {execFileSync}=require('node:child_process');
const {createRequire}=require('node:module');
const {createHash}=require('node:crypto');
const {DatabaseSync}=require('node:sqlite');
const assert=require('node:assert/strict');
const root=path.resolve(process.argv[2]||process.cwd());
const {buildSync}=createRequire(path.join(root,'package.json'))('esbuild');
const temp=fs.mkdtempSync(path.join(os.tmpdir(),'skymind-cloud-contract-'));
(async()=>{try{
  const binary=path.join(temp,'encoder');
  execFileSync(process.env.CXX||'c++',['-std=c++17','-Wall','-Wextra','-Werror',path.join(__dirname,'cloud_protocol_test.cpp'),'-o',binary]);
  for(const [name,source] of [['contract','shared/environment.ts'],['ingest','functions/api/xaerova/environment/ingest.ts']])buildSync({entryPoints:[path.join(root,source)],outfile:path.join(temp,name+'.cjs'),bundle:true,platform:'node',format:'cjs',logLevel:'silent'});
  const rules=require(path.join(temp,'contract.cjs'));
  const handler=require(path.join(temp,'ingest.cjs')).onRequestPost;
  const token='xenv_'+'0'.repeat(64),hash=createHash('sha256').update(token).digest('hex');
  let cases=0;
  for(const mode of ['zero','fixture','binary','paused','invalid','rollover']){
    const packet=JSON.parse(execFileSync(binary,[mode,String(Date.now())],{encoding:'utf8'}));
    assert.ok(rules.parseTelemetry(packet),mode);
    const sql=new DatabaseSync(':memory:');
    try{
      sql.exec('PRAGMA foreign_keys=ON');
      for(const migration of ['0010_xaerova_users.sql','0011_xaerova_devices.sql','0023_environment_telemetry.sql'])sql.exec(fs.readFileSync(path.join(root,'migrations',migration),'utf8'));
      sql.exec("INSERT INTO xaerova_users(user_id,email,created_at) VALUES('test-owner','test@example.invalid','now'); INSERT INTO xaerova_devices(device_id,user_id,registration_token,status,paired_at) VALUES('contract-test-device','test-owner','test-registration','active','now')");
      sql.prepare('INSERT INTO xaerova_environment(device_id,owner_user_id,credential_hash,provisioned_at) VALUES(?,?,?,?)').run(packet.deviceId,'test-owner',hash,Date.now());
      function statement(query,args=[]){return{bind:(...values)=>statement(query,values),first:async()=>sql.prepare(query).get(...args)||null};}
      async function send(body){return handler({request:new Request('https://test.example/api/xaerova/environment/ingest',{method:'POST',headers:{'Content-Type':'application/json',Authorization:'Bearer '+token},body:JSON.stringify(body)}),env:{DB:{prepare:statement}}});}
      assert.equal((await send(packet)).status,200,mode+' ingestion');
      const row=sql.prepare('SELECT received_at,telemetry_json FROM xaerova_environment').get();
      const telemetry=JSON.parse(row.telemetry_json),snapshot={telemetry,receivedAt:row.received_at};
      if(mode==='zero')for(const key of rules.SENSOR_KEYS)assert.equal(rules.sensorState(snapshot,key,row.received_at).value,0);
      if(mode==='fixture'){assert.equal(telemetry.sensors.humidity.value,64);assert.equal(telemetry.sensors.light.value,50);assert.equal(telemetry.sensors.temperature.value,25.562);}
      if(mode==='binary'){assert.equal(telemetry.sensors.rain.value,1);assert.equal(telemetry.sensors.water_ingress.value,1);}
      if(mode==='paused'){for(const key of rules.SENSOR_KEYS.filter(key=>key!=='temperature'))assert.equal(telemetry.sensors[key].valid,false);assert.equal(telemetry.sensors.temperature.valid,true);}
      if(mode==='invalid')for(const key of rules.SENSOR_KEYS){assert.equal(telemetry.sensors[key].valid,false);assert.equal(telemetry.sensors[key].value,null);}
      if(mode==='rollover'){assert.ok(telemetry.uptimeMs>0xffffffff);assert.equal(telemetry.sensors.wind.ageMs,750);}
      assert.equal((await send(packet)).status,409,'duplicate must not refresh receipt');
      assert.equal(sql.prepare('SELECT received_at FROM xaerova_environment').get().received_at,row.received_at);
      assert.equal((await send({...packet,sequence:1,capturedAt:Date.now()-6000})).status,422);
      assert.equal((await send({...packet,sequence:1,capturedAt:Date.now()+3000})).status,422);
      assert.equal(rules.connectionState(snapshot,row.received_at+30000),'offline');
      for(const key of rules.SENSOR_KEYS)assert.equal(rules.sensorState(snapshot,key,row.received_at+11000).value,null);
      cases++;
    }finally{sql.close();}
  }
  console.log('PASS: '+cases+' actual firmware encoder cases accepted by the real ingest handler; zeros, conversions, binary values, invalid/stale/paused values, 64-bit uptime, duplicate ordering and capture-time windows verified. No live backend or hardware calls.');
}finally{fs.rmSync(temp,{recursive:true,force:true});}})().catch(error=>{console.error(error.message);process.exitCode=1;});
