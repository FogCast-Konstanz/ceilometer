/* Same vendored WebGL-Plot renderer as the board bring-up/RDS GUI. No CDN. */

'use strict';

const $=id=>document.getElementById(id);

let token=sessionStorage.getItem('ceilometerToken')||'',status=null,profile=null,lastContact=0,lastSerial=-1,polling=false,actionPending=false;

let plot=null,gl=null,resize=null;

const format=(v,d=1)=>Number.isFinite(Number(v))?Number(v).toLocaleString(undefined,{maximumFractionDigits:d}):'—';

function text(id,value){$(id).textContent=value;}

function error(message){$('error').hidden=!message;message&&(text('error',message));}

async function api(path,body){

  const headers={};if(token)headers.Authorization=`Bearer ${token}`;

  if(body!==undefined)headers['Content-Type']='application/json';

  const response=await fetch(path,{method:body===undefined?'GET':'POST',headers,body:body===undefined?undefined:JSON.stringify(body),cache:'no-store',signal:AbortSignal.timeout(5000)});

  const data=await response.json();

  if(!response.ok){if(response.status===401&&!$('login').open)$('login').showModal();throw Error(data.error||`Request failed (${response.status})`);}

  return data;

}

function metric(id,value,unit){text(id,value===null||value===undefined?'—':`${format(value)} ${unit}`);}

function drawStatus(){

  const fresh=Date.now()-lastContact<3500,s=status;

  text('connection',fresh?(s?.simulation?'Simulation · no hardware':'Pi connected'):'Pi connection lost');$('connectionDot').className=`dot ${fresh?'good':'bad'}`;

  const confirmed=fresh&&s?.connected&&s?.stop_confirmed&&!s?.enabled;

  const labels={DISABLED:'Instrument disabled',STARTING:'Starting',WARMING:'Temperature stabilization',MEASURING:'Measuring',STOPPING:'Disabling',FAULT:'Instrument fault',DISCONNECTED:'Pico disconnected'};

  text('state',fresh?(labels[s?.state]||'Connecting'):'Status unavailable');

  text('message',fresh?s?.message||'Connecting':'Shutdown cannot be confirmed while the Pi is unreachable.');

  $('stateDot').className=`dot large ${!fresh||s?.fault?'bad':confirmed||s?.state==='MEASURING'?'good':'warn'}`;

  text('shutdown',confirmed?'Laser, SiPM bias and TEC off · board confirmed':fresh&&s?.enabled?'Outputs enabled · disable before servicing':'Shutdown not confirmed');

  $('enable').disabled=actionPending||!confirmed||!!s?.fault||!!s?.enable_inhibit;
  $('enable').title=s?.enable_inhibit||'';
  if(confirmed&&s?.enable_inhibit)text('message',`Measurements locked: ${s.enable_inhibit}`);

  $('mode').disabled=!!s?.enabled||actionPending;

  $('reset').hidden=!s?.fault;$('reset').disabled=actionPending||!confirmed;

  if(!s)return;

  if(s.enabled)$('mode').value=s.mode;

  metric('laserTemp',fresh&&s.thermal?.valid?s.thermal.temperature_c:null,'°C');

  const rt=s.telemetry.receiver_temp_mc;

  const rtFresh=rt&&rt.valid&&Date.now()-Date.parse(rt.received_utc)+rt.age_us/1000<5000;

  metric('receiverTemp',fresh&&rtFresh?Number(rt.value)/1000:null,'°C');

  metric('biasTarget',fresh&&s.bias?.enabled&&s.bias?.valid?s.bias.target_v:null,'V');

  const recent=profile&&Date.now()-Date.parse(profile.received_utc)<Math.max(3500,(s.configuration.measurement_period_ms?.value||1000)*2);

  metric('countRate',fresh&&s.enabled&&recent?profile.histogram.reduce((a,b)=>a+Number(b),0)/profile.exposure_s:null,'cps');

  metric('uptime',s.runtime?s.runtime.uptime_us/1e6:null,'s');
  text('bootReason',s.runtime?(s.runtime.watchdog_reboot?'Watchdog reset':'Power / other reset'):'Unavailable');
  text('usbDisconnects',s.usb_disconnects||0);text('picoReboots',s.pico_reboots||0);
  metric('exposure',profile?.exposure_s,'s');
  metric('wallTime',profile?.wall_s,'s');
  metric('deadTime',profile?.dead_time_s,'s');
  text('acquisitionMode',profile?.acquisition_mode==='finite_burst'?'Finite bursts':profile?.acquisition_mode||'Waiting');

  text('release',s.deployment?.release_id?.slice(0,12)||'Development');
  text('profiles',format(s.profiles,0));text('dropped',format(s.dropped_results,0));

  text('prn',s.configuration.prn_length?.value?`${s.configuration.prn_length.value} chips`:'—');

  text('chipRate',s.configuration.chip_hz?.value?`${format(s.configuration.chip_hz.value/1e6,2)} MHz`:'—');

  text('period',s.configuration.measurement_period_ms?.value?`${format(s.configuration.measurement_period_ms.value/1000,3)} s`:'—');

  text('tecOutput',fresh&&s.thermal?.enabled?`${format(s.thermal.output_mv)} mV`:'Off');

  text('processing',s.stream?`${format(s.stream.max_processing_us,0)} µs`:'—');text('flags',s.stream?String(s.stream.error_flags):'—');

  text('lastReceived',s.last_received_utc?new Date(s.last_received_utc).toLocaleTimeString():'—');

  text('disk',s.disk_free_bytes===null||s.disk_free_bytes===undefined?'—':`${format(s.disk_free_bytes/2**30,1)} GiB`);

  $('outputs').replaceChildren();

  for(const [label,on] of [['Laser armed',s.board?.armed],['Bias enabled',s.bias?.enabled],['TEC enabled',s.thermal?.enabled]]){

    const row=document.createElement('span'),dot=document.createElement('i');dot.className=`dot ${fresh&&on?'warn':''}`;row.append(dot,document.createTextNode(fresh?`${label}: ${on?'yes':'no'}`:`${label}: unknown`));$('outputs').append(row);

  }

  renderTelemetry();

}

function renderTelemetry(){

  if(!status)return;

  const filter=$('filter').value.toLowerCase(),rows=[];

  const entries=Object.entries(status.telemetry).filter(([name])=>name.includes(filter));

  for(const [name,v]of entries){

    const tr=document.createElement('tr'),age=Date.now()-Date.parse(v.received_utc)+v.age_us/1000;

    const valid=Date.now()-lastContact<3500&&v.valid&&age<5000;

    for(const value of [name,String(v.value),v.unit,valid?'Valid':v.valid?'Stale':'Invalid']){const td=document.createElement('td');td.textContent=value;tr.append(td);}

    if(!valid)tr.lastChild.className='invalid';rows.push(tr);

  }

  $('telemetry').replaceChildren(...rows);text('telemetryCount',`${Object.keys(status.telemetry).length} signals`);

}

function drawProfile(){

  if(!profile)return;

  text('profileMeta',`Profile ${profile.id} · ${profile.length} chips · received ${new Date(profile.received_utc).toLocaleTimeString()}`);

  $('download').disabled=false;

  try{

    if(!plot){

      gl=webglplot.setupCanvasAndWebGL($('profilePlot'),{backgroundColor:[.035,.065,.09,1],antialias:false});

      plot=new webglplot.WebglLinePlot(gl,1);

      plot.initLines([{points:new Float32Array(4095*2),color:[.40,.86,.78,1],thickness:1,enabled:true}]);

      resize=new ResizeObserver(()=>renderPlot());resize.observe($('profilePlot'));

    }

    renderPlot();$('plotEmpty').hidden=true;

  }catch(e){text('plotEmpty','Plot unavailable; profile download remains available.');$('plotEmpty').hidden=false;}

}

function renderPlot(){

  if(!plot||!profile)return;

  webglplot.handleCanvasResize($('profilePlot'),gl);webglplot.clearCanvas(gl);

  const values=profile.correlation_counts_per_second,symlog=$('scale').value==='symlog';

  const transform=v=>symlog?Math.sign(v)*Math.log10(1+Math.abs(v)):v;

  const inverse=v=>symlog?Math.sign(v)*(10**Math.abs(v)-1):v;

  const scaled=values.map(transform);let low=Math.min(...scaled),high=Math.max(...scaled);

  if(low===high){low-=1;high+=1;}const padding=(high-low)*.08;low-=padding;high+=padding;

  const points=new Float32Array(4095*2);points.fill(NaN);

  for(let i=0;i<values.length;i++){points[i*2]=i/(values.length-1)*2-1;points[i*2+1]=(scaled[i]-low)/(high-low)*2-1;}

  // The renderer has a fixed point capacity; NaNs suppress unused 1023-mode vertices.

  plot.updateLinePoints(0,points);plot.draw();

  text('yMax',format(inverse(high),0));text('yMid',format(inverse((high+low)/2),0));text('yMin',format(inverse(low),0));

  text('xStart',format(profile.range_m[0]/1000,2));text('xEnd',format(profile.range_m.at(-1)/1000,2));

}

async function poll(){

  if(polling)return;polling=true;

  try{

    status=await api('/api/status');lastContact=Date.now();drawStatus();

    if(status.profile_serial!==lastSerial){profile=await api('/api/profile');lastSerial=status.profile_serial;if(profile)drawProfile();}

  }catch(e){error(e.message);drawStatus();}finally{polling=false;}

}

async function control(path,body={}){

  if(path!=='disable')actionPending=true;drawStatus();error('');

  try{status=await api(`/api/${path}`,body);lastContact=Date.now();drawStatus();}

  catch(e){error(e.message);}finally{actionPending=false;void poll();}

}

$('enable').addEventListener('click',()=>control('enable',{mode:$('mode').value}));

$('disable').addEventListener('click',()=>control('disable'));

$('reset').addEventListener('click',()=>control('reset-fault'));

$('filter').addEventListener('input',renderTelemetry);$('scale').addEventListener('change',drawProfile);

$('access').addEventListener('click',()=>$('login').showModal());$('cancelLogin').addEventListener('click',()=>$('login').close());

$('loginForm').addEventListener('submit',event=>{event.preventDefault();token=$('token').value;sessionStorage.setItem('ceilometerToken',token);$('token').value='';$('login').close();error('');void poll();});

$('download').addEventListener('click',()=>{if(!profile)return;const url=URL.createObjectURL(new Blob([JSON.stringify(profile)],{type:'application/json'}));const a=document.createElement('a');a.href=url;a.download=`ceilometer-${profile.boot_id}-${profile.id}.json`;a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);});

setInterval(()=>{drawStatus();void poll();},1000);void poll();
