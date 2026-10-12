// UI integration test against a simulated device API; this does not test hardware.
const {chromium}=require('playwright');
const fs=require('node:fs'),path=require('node:path'),http=require('node:http'),assert=require('node:assert/strict');
const root=path.resolve(__dirname,'..');
const key='0123456789abcdef0123456789abcdef';
const config={ssid:'Test Wi-Fi',timezone:'EET-2EEST,M3.5.0/3,M10.5.0/4',brightness:60,stations:[{name:'Українське радіо',url:'https://example.org/radio.mp3'}],alarms:Array.from({length:8},()=>({enabled:false,hour:7,minute:30,weekdays:127,station:-1}))};
const state={mode:0,volume:35,station:-1,paused:false,ringing:false,sd:true,timeValid:true,epoch:1791709200,timer:0,sleep:0,recordingSeconds:0,micPeak:0,message:'',wifiConnected:true,batteryPresent:true,batteryPercent:73,usbPower:true,charging:true};
const commands=[];
const server=http.createServer(async(req,res)=>{
    if(req.url==='/'){res.setHeader('Content-Type','text/html');return res.end(fs.readFileSync(path.join(root,'main/web/index.html')))}
    if(req.headers['x-device-token']!==key){res.statusCode=401;return res.end('Unauthorized')}
    res.setHeader('Content-Type','application/json');
    if(req.url==='/api/settings')return res.end(JSON.stringify(config));
    if(req.url==='/api/status')return res.end(JSON.stringify({...config,...state}));
    if(req.url==='/api/recordings')return res.end(JSON.stringify({ready:true,freeBytes:10e9,files:[{name:'rec-test.wav',bytes:32044,seconds:1}]}));
    if(req.url==='/api/command'){let body='';for await(const chunk of req)body+=chunk;const cmd=JSON.parse(body);commands.push(cmd);if(cmd.action==='configure')Object.assign(config,Object.fromEntries(Object.entries(cmd).filter(([k])=>k!=='action')));if(cmd.action==='timer')state.timer=cmd.seconds;return res.end('{"ok":true}')}
    res.statusCode=404;res.end('Not found');
});
(async()=>{
    await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
    const executablePath=['C:/Program Files/Google/Chrome/Application/chrome.exe','C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe','C:/Program Files/Microsoft/Edge/Application/msedge.exe'].find(p=>fs.existsSync(p));
    const browser=await chromium.launch({headless:true,...(executablePath?{executablePath}:{})});
    try {
        const page=await browser.newPage({viewport:{width:390,height:844}});const errors=[];page.on('pageerror',e=>errors.push(e.message));
        await page.goto(`http://127.0.0.1:${server.address().port}`);
        await page.locator('#token').fill(key);await page.locator('#login').click();await page.locator('#app').waitFor({state:'visible'});
        assert.match(await page.locator('#powerStatus').textContent(),/73%.*заряджання/);
        await page.locator('#station').selectOption('0');await page.locator('#play').click();await page.waitForTimeout(100);
        assert(commands.some(c=>c.action==='play'&&c.station===0));
        await page.locator('#timerMinutes').fill('3');await page.locator('#timerStart').click();await page.waitForTimeout(100);assert(commands.some(c=>c.action==='timer'&&c.seconds===180));
        await page.locator('#timerPause').click();await page.locator('#timerRepeat').click();await page.waitForTimeout(100);
        assert(commands.some(c=>c.action==='timer_pause'));assert(commands.some(c=>c.action==='timer_repeat'));
        const out=path.join(root,'.tools','screenshots');fs.mkdirSync(out,{recursive:true});await page.screenshot({path:path.join(out,'player-mobile.png'),fullPage:true});
        await page.locator('[data-tab="settings"]').click();await page.locator('#addStation').click();const row=page.locator('#stationRows .station').last();
        await row.locator('input').nth(0).fill('<img src=x onerror=alert(1)>');await row.locator('input').nth(1).fill('https://example.org/second.mp3');await page.locator('#saveStations').click();await page.waitForTimeout(200);
        assert.equal(config.stations.length,2);assert.equal(await page.locator('#stationRows img').count(),0);
        await page.locator('#autoRotate').check();await page.locator('#liftWake').check();await page.locator('#nightQuiet').check();
        await page.locator('#nightStart').fill('22:30');await page.locator('#nightEnd').fill('06:45');await page.locator('#rotationOffset').selectOption('1');
        await page.locator('#saveDisplay').click();await page.waitForTimeout(100);assert(config.autoRotate&&config.liftWake&&config.nightQuiet);
        assert.equal(config.nightStart,1350);assert.equal(config.nightEnd,405);assert.equal(config.rotationOffset,1);
        await page.locator('[data-tab="alarms"]').click();const alarm=page.locator('.alarm').first();await alarm.locator('input[type=checkbox]').first().check();await alarm.locator('input[type=time]').fill('08:45');await page.locator('#saveAlarms').click();await page.waitForTimeout(100);assert.equal(config.alarms[0].hour,8);assert.equal(config.alarms[0].minute,45);assert.equal(config.alarms[0].enabled,true);
        const updated=page.locator('.alarm').first();await updated.locator('input[type=checkbox]').last().check();
        await updated.locator('input[type=number]').nth(0).fill('70');await updated.locator('input[type=number]').nth(1).fill('5');await updated.locator('input[type=number]').nth(2).fill('60');
        await page.locator('#saveAlarms').click();await page.waitForTimeout(100);
        assert.equal(config.alarms[0].oneShot,true);assert.equal(config.alarms[0].volume,70);assert.equal(config.alarms[0].snooze,5);assert.equal(config.alarms[0].ramp,60);
        await page.locator('[data-tab="recorder"]').click();await page.locator('#files .file').filter({hasText:'rec-test.wav'}).waitFor();await page.screenshot({path:path.join(out,'recorder-mobile.png'),fullPage:true});
        assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth),false);
        assert.deepEqual(errors,[]);console.log('Browser tests passed: mobile layout, login, playback commands, timers, station editing, alarms, recording list, text escaping');
    } finally {await browser.close()}
})().catch(e=>{console.error(e);process.exitCode=1}).finally(()=>server.close());
